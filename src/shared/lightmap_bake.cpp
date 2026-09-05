#include "lightmap_bake.hpp"

#include "brush.hpp"
#include "log.hpp"
#include "map_geometry.hpp"
#include "span.hpp"

#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rect_pack.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <variant>

namespace shared
{

namespace
{

int chart_extent_in_texels(float extent_in_world_units, float world_units_per_texel,
                           int gutter_in_texels)
{
  const int covered = (int)std::ceil(extent_in_world_units / world_units_per_texel);
  return std::max(covered, 1) + 2 * gutter_in_texels;
}

// The size cap is a cap on DENSITY, never a truncation of the rect. Clamping the
// rect instead leaves the face mapping into more texels than were allocated for
// it, so its UVs run off the end and sample whatever the packer put next door --
// silently, and only on faces big enough to trip it.
//
// The `- 1` pays for the anchor snap below, which can push the extent up by at
// most one texel after this has already chosen the density.
float density_that_fits(float extent_u, float extent_v, float world_units_per_texel,
                        int max_covered_in_texels)
{
  const float largest_extent = std::max(extent_u, extent_v);
  const float smallest_that_fits =
      largest_extent / (float)(max_covered_in_texels - 1);
  return std::max(world_units_per_texel, smallest_that_fits);
}

linalg::vec2 to_chart_space(const lightmap_chart_t &chart, const linalg::vec3 &world_position)
{
  const linalg::vec3 offset_from_origin = world_position - chart.origin;
  return {linalg::dot(offset_from_origin, chart.tangent_u),
          linalg::dot(offset_from_origin, chart.tangent_v)};
}

// Steps 1 through 6 for ONE planar set of world points, with neither coverage
// shape filled: the caller projects its polygon or its triangles through
// to_chart_space afterwards, which is the record's own projection run again.
std::optional<lightmap_chart_t> try_build_chart(entity_uid_t object_uid, const Plane &plane,
                                                Span<const linalg::vec3> points,
                                                float lightmap_scale,
                                                const lightmap_bake_settings_t &settings,
                                                int max_covered_in_texels)
{
  // 1. basis
  linalg::vec3 tangent_u;
  linalg::vec3 tangent_v;
  brush_face_grid_tangents(plane.normal, tangent_u, tangent_v);

  // 2. project
  const linalg::vec3 reference = points[0];

  float min_u = 0.f, max_u = 0.f, min_v = 0.f, max_v = 0.f;
  for (size_t i = 0; i < points.count; ++i)
  {
    const linalg::vec3 offset_from_reference = points[i] - reference;
    const float u = linalg::dot(offset_from_reference, tangent_u);
    const float v = linalg::dot(offset_from_reference, tangent_v);

    // 3. bounds
    if (i == 0)
    {
      min_u = max_u = u;
      min_v = max_v = v;
      continue;
    }
    min_u = std::min(min_u, u);
    max_u = std::max(max_u, u);
    min_v = std::min(min_v, v);
    max_v = std::max(max_v, v);
  }

  if (lightmap_scale <= 0.f)
  {
    log_error("[lightmap] object {} has a face with lightmap_scale {}; it gets no chart.",
              object_uid, lightmap_scale);
    return std::nullopt;
  }

  const float world_units_per_texel = density_that_fits(
      max_u - min_u, max_v - min_v,
      1.f / (settings.texels_per_world_unit * lightmap_scale), max_covered_in_texels);

  // 4. anchor
  const float snapped_min_u =
      std::floor(min_u / world_units_per_texel) * world_units_per_texel;
  const float snapped_min_v =
      std::floor(min_v / world_units_per_texel) * world_units_per_texel;

  // 5. size
  const int width_in_texels = chart_extent_in_texels(
      max_u - snapped_min_u, world_units_per_texel, settings.gutter_in_texels);
  const int height_in_texels = chart_extent_in_texels(
      max_v - snapped_min_v, world_units_per_texel, settings.gutter_in_texels);

  if (width_in_texels > settings.max_chart_extent_in_texels ||
      height_in_texels > settings.max_chart_extent_in_texels)
  {
    log_error("[lightmap] object {} produced a {}x{} chart past the {} cap after the "
              "density was already lowered to fit; it gets no chart.", object_uid,
              width_in_texels, height_in_texels, settings.max_chart_extent_in_texels);
    return std::nullopt;
  }

  // 6. record
  lightmap_chart_t chart;
  chart.object_uid = object_uid;
  chart.plane = plane;
  chart.tangent_u = tangent_u;
  chart.tangent_v = tangent_v;
  chart.origin = reference + tangent_u * snapped_min_u + tangent_v * snapped_min_v;
  chart.world_units_per_texel = world_units_per_texel;
  chart.atlas_rect.width = width_in_texels;
  chart.atlas_rect.height = height_in_texels;
  return chart;
}

void build_brush_charts(entity_uid_t object_uid, const brush_geometry_t &brush,
                        const lightmap_bake_settings_t &settings, int max_covered_in_texels,
                        std::vector<lightmap_chart_t> &charts)
{
  const std::optional<brush_polyhedron_t> polyhedron =
      try_build_brush_polyhedron(brush.hull_points);
  if (!polyhedron)
  {
    log_error("[lightmap] brush {} did not hull; it gets no charts.", object_uid);
    return;
  }

  std::vector<linalg::vec3> points;
  for (const brush_face_t &face : polyhedron->faces)
  {
    const face_surface_t *surface = find_face_surface(brush, face.plane);
    if (surface && !surface->emits_geometry)
      continue;

    points.clear();
    for (uint32_t vertex_index : face.vertex_indices)
      points.push_back(polyhedron->vertices[vertex_index]);

    std::optional<lightmap_chart_t> chart =
        try_build_chart(object_uid, face.plane, points, surface ? surface->lightmap_scale : 1.f,
                        settings, max_covered_in_texels);
    if (!chart) continue;

    chart->polygon.reserve(points.size());
    for (const linalg::vec3 &point : points)
      chart->polygon.push_back(to_chart_space(*chart, point));

    charts.push_back(std::move(*chart));
  }
}

// A static mesh's charts are xatlas's (lightmap_unwrap_plan.md step 2): the
// unwrap is the chart's coverage and its twin, and the record keeps a plane
// only as a descriptor -- the reach report names a face by its normal -- never
// as a key. The size steps are try_build_chart's over the unwrap's extent; the
// anchor snap is not needed, since chart space already starts at the min corner.
void build_static_mesh_charts(entity_uid_t object_uid, const static_mesh_geometry_t &static_mesh,
                              const lightmap_bake_settings_t &settings,
                              int max_covered_in_texels, std::vector<lightmap_chart_t> &charts)
{
  std::vector<chart_unwrap_t> unwraps =
      unwrap_static_mesh(static_mesh, settings.texels_per_world_unit);
  if (unwraps.empty())
  {
    log_warning("[lightmap] static mesh {} unwrapped to nothing; it gets no charts.",
                object_uid);
    return;
  }
  const std::vector<vertex_xnu> world = static_mesh_world_vertices(static_mesh);

  for (chart_unwrap_t& unwrap : unwraps)
  {
    linalg::vec2 extent{0.f, 0.f};
    for (const unwrapped_vertex_t& vertex : unwrap.vertices)
      extent = {std::max(extent.x, vertex.uv.x), std::max(extent.y, vertex.uv.y)};

    const float world_units_per_texel = density_that_fits(
        extent.x, extent.y, 1.f / settings.texels_per_world_unit, max_covered_in_texels);
    const int width_in_texels =
        chart_extent_in_texels(extent.x, world_units_per_texel, settings.gutter_in_texels);
    const int height_in_texels =
        chart_extent_in_texels(extent.y, world_units_per_texel, settings.gutter_in_texels);
    if (width_in_texels > settings.max_chart_extent_in_texels ||
        height_in_texels > settings.max_chart_extent_in_texels)
    {
      log_error("[lightmap] static mesh {} produced a {}x{} chart past the {} cap after "
                "the density was already lowered to fit; it gets no chart.",
                object_uid, width_in_texels, height_in_texels,
                settings.max_chart_extent_in_texels);
      continue;
    }

    lightmap_chart_t chart;
    chart.object_uid = object_uid;
    chart.world_units_per_texel = world_units_per_texel;
    chart.atlas_rect.width = width_in_texels;
    chart.atlas_rect.height = height_in_texels;

    chart.triangles.reserve(unwrap.indices.size());
    chart.twins.reserve(unwrap.faces.size());
    linalg::vec3 area_normal{0.f, 0.f, 0.f};
    linalg::vec3 centroid{0.f, 0.f, 0.f};
    for (size_t t = 0; t < unwrap.faces.size(); ++t)
    {
      chart_triangle_twin_t twin;
      for (uint32_t corner = 0; corner < 3; ++corner)
      {
        const unwrapped_vertex_t& vertex = unwrap.vertices[unwrap.indices[t * 3 + corner]];
        if (vertex.xref >= world.size())
          fatal_error("[lightmap] static mesh {}'s unwrap names vertex {} of {}.",
                      object_uid, vertex.xref, world.size());
        chart.triangles.push_back(vertex.uv);
        twin.corners[corner] = world[vertex.xref].position;
        twin.normals[corner] = world[vertex.xref].normal;
        centroid = centroid + twin.corners[corner];
      }
      area_normal = area_normal + linalg::cross(twin.corners[1] - twin.corners[0],
                                                twin.corners[2] - twin.corners[0]);
      chart.twins.push_back(twin);
    }

    const float area_length = linalg::length(area_normal);
    chart.plane.normal = area_length > 1e-6f ? area_normal * (1.f / area_length)
                                             : chart.twins[0].normals[0];
    chart.plane.point = centroid * (1.f / (float)chart.triangles.size());
    chart.unwrap = std::move(unwrap);

    charts.push_back(std::move(chart));
  }
}

} // namespace

std::vector<lightmap_chart_t>
build_lightmap_charts(const map_t &map, const lightmap_bake_settings_t &settings)
{
  std::vector<lightmap_chart_t> charts;

  if (settings.texels_per_world_unit <= 0.f)
  {
    log_error("[lightmap] texels_per_world_unit is {}, which cannot size a chart.",
              settings.texels_per_world_unit);
    return charts;
  }

  const int max_covered_in_texels =
      settings.max_chart_extent_in_texels - 2 * settings.gutter_in_texels;
  if (max_covered_in_texels < 2)
  {
    log_error("[lightmap] a {}-texel chart cap with a {}-texel gutter leaves no room "
              "for a face.", settings.max_chart_extent_in_texels, settings.gutter_in_texels);
    return charts;
  }

  for (const map_geometry_t &object : map.geometry)
  {
    switch (get_kind(object.value))
    {
    case geometry_kind_t::Brush:
      build_brush_charts(object.uid, std::get<brush_geometry_t>(object.value), settings,
                         max_covered_in_texels, charts);
      break;

    case geometry_kind_t::Static_Mesh:
      build_static_mesh_charts(object.uid, std::get<static_mesh_geometry_t>(object.value),
                               settings, max_covered_in_texels, charts);
      break;
    }
  }

  return charts;
}

lightmap_atlas_t pack_lightmap_charts(std::vector<lightmap_chart_t> &charts,
                                      const lightmap_bake_settings_t &settings)
{
  lightmap_atlas_t atlas;
  atlas.size_in_texels = settings.atlas_size_in_texels;

  if (atlas.size_in_texels <= 0)
  {
    log_error("[lightmap] atlas_size_in_texels is {}, which can hold nothing.",
              atlas.size_in_texels);
    return {};
  }

  std::vector<size_t> remaining;
  remaining.reserve(charts.size());
  for (size_t i = 0; i < charts.size(); ++i)
  {
    charts[i].page = -1;

    if (charts[i].atlas_rect.width > atlas.size_in_texels ||
        charts[i].atlas_rect.height > atlas.size_in_texels)
    {
      log_error("[lightmap] a chart of brush {} is {}x{} texels and no {}x{} page can "
                "ever hold it; lower texels_per_world_unit or raise "
                "atlas_size_in_texels.",
                charts[i].object_uid, charts[i].atlas_rect.width,
                charts[i].atlas_rect.height, atlas.size_in_texels, atlas.size_in_texels);
      return {};
    }
    remaining.push_back(i);
  }

  std::vector<stbrp_node> nodes((size_t)atlas.size_in_texels);
  std::vector<stbrp_rect> rects;

  while (!remaining.empty())
  {
    rects.clear();
    rects.reserve(remaining.size());
    for (size_t chart_index : remaining)
    {
      stbrp_rect rect = {};
      rect.id = (int)chart_index;
      rect.w = (stbrp_coord)charts[chart_index].atlas_rect.width;
      rect.h = (stbrp_coord)charts[chart_index].atlas_rect.height;
      rects.push_back(rect);
    }

    stbrp_context context;
    stbrp_init_target(&context, atlas.size_in_texels, atlas.size_in_texels,
                      nodes.data(), (int)nodes.size());
    stbrp_pack_rects(&context, rects.data(), (int)rects.size());

    std::vector<size_t> still_unplaced;
    for (const stbrp_rect &rect : rects)
    {
      if (!rect.was_packed)
      {
        still_unplaced.push_back((size_t)rect.id);
        continue;
      }
      lightmap_chart_t &chart = charts[(size_t)rect.id];
      chart.page = atlas.page_count;
      chart.atlas_rect.min_x = rect.x;
      chart.atlas_rect.min_y = rect.y;
    }

    ++atlas.page_count;

    if (still_unplaced.size() == remaining.size())
    {
      log_error("[lightmap] a full {}x{} page took none of the {} remaining charts; "
                "packing cannot make progress.",
                atlas.size_in_texels, atlas.size_in_texels, remaining.size());
      return {};
    }
    remaining = std::move(still_unplaced);
  }

  return atlas;
}

linalg::vec2 texel_center_in_chart_space(const lightmap_chart_t &chart, int texel_x,
                                         int texel_y)
{
  return {((float)texel_x + 0.5f) * chart.world_units_per_texel,
          ((float)texel_y + 0.5f) * chart.world_units_per_texel};
}

linalg::vec3 chart_space_to_world(const lightmap_chart_t &chart,
                                  const linalg::vec2 &chart_space)
{
  return chart.origin + chart.tangent_u * chart_space.x +
         chart.tangent_v * chart_space.y;
}

// Even-odd crossing rather than a winding-sign test: the chart basis is picked
// from the normal alone, so cross(u, v) is the normal on only some faces and the
// polygon's handedness in chart space is not fixed.
bool polygon_contains(Span<const linalg::vec2> polygon, const linalg::vec2& point)
{
  bool inside = false;
  for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
  {
    const linalg::vec2& a = polygon[i];
    const linalg::vec2& b = polygon[j];
    if ((a.y > point.y) == (b.y > point.y)) continue;
    const float crossing_x = (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x;
    if (point.x < crossing_x) inside = !inside;
  }
  return inside;
}

float twice_signed_area(const linalg::vec2& a, const linalg::vec2& b, const linalg::vec2& p)
{
  return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}

texel_sample_t sample_chart(const lightmap_chart_t& chart, const linalg::vec2& chart_space)
{
  texel_sample_t sample;

  if (chart.polygon.size() >= 3)
  {
    if (!polygon_contains(Span<const linalg::vec2>(chart.polygon.data(),
                                                   (uint32_t)chart.polygon.size()),
                          chart_space))
      return sample;
    sample.on_surface = true;
    sample.position = chart_space_to_world(chart, chart_space);
    sample.normal = chart.plane.normal;
    return sample;
  }

  if (chart.twins.size() * 3 != chart.triangles.size())
    fatal_error("[lightmap] object {}'s chart carries {} triangles and {} twins; a triangle "
                "chart carries one twin per triangle.",
                chart.object_uid, chart.triangles.size() / 3, chart.twins.size());

  // Signs rather than a winding order, for the reason polygon_contains gives.
  for (size_t twin_index = 0; twin_index < chart.twins.size(); ++twin_index)
  {
    const linalg::vec2& a = chart.triangles[twin_index * 3 + 0];
    const linalg::vec2& b = chart.triangles[twin_index * 3 + 1];
    const linalg::vec2& c = chart.triangles[twin_index * 3 + 2];
    const float weight_c = twice_signed_area(a, b, chart_space);
    const float weight_a = twice_signed_area(b, c, chart_space);
    const float weight_b = twice_signed_area(c, a, chart_space);
    const bool any_negative = weight_a < 0.f || weight_b < 0.f || weight_c < 0.f;
    const bool any_positive = weight_a > 0.f || weight_b > 0.f || weight_c > 0.f;
    if (any_negative && any_positive) continue;

    const float total = weight_a + weight_b + weight_c;
    if (total == 0.f) continue;

    const chart_triangle_twin_t& twin = chart.twins[twin_index];
    const float inverse_total = 1.f / total;
    sample.on_surface = true;
    sample.position = twin.corners[0] * (weight_a * inverse_total) +
                      twin.corners[1] * (weight_b * inverse_total) +
                      twin.corners[2] * (weight_c * inverse_total);
    const linalg::vec3 blended_normal = twin.normals[0] * (weight_a * inverse_total) +
                                        twin.normals[1] * (weight_b * inverse_total) +
                                        twin.normals[2] * (weight_c * inverse_total);
    const float length = linalg::length(blended_normal);
    sample.normal = length > 1e-6f ? blended_normal * (1.f / length) : chart.plane.normal;
    return sample;
  }

  return sample;
}

texel_sample_t sample_texel(const lightmap_chart_t& chart, int texel_x, int texel_y)
{
  return sample_chart(chart, texel_center_in_chart_space(chart, texel_x, texel_y));
}

linalg::vec2 chart_space_to_atlas_texel(const lightmap_chart_t &chart,
                                        const lightmap_bake_settings_t &settings,
                                        const linalg::vec2 &chart_space)
{
  const float gutter = (float)settings.gutter_in_texels;
  return {(float)chart.atlas_rect.min_x + gutter +
              chart_space.x / chart.world_units_per_texel,
          (float)chart.atlas_rect.min_y + gutter +
              chart_space.y / chart.world_units_per_texel};
}

linalg::vec3 lightmap_uv_from_chart_space(const lightmap_chart_t &chart,
                                          const lightmap_bake_settings_t &settings,
                                          const lightmap_atlas_t &atlas,
                                          const linalg::vec2 &chart_space)
{
  const linalg::vec2 texel = chart_space_to_atlas_texel(chart, settings, chart_space);
  const float size = (float)atlas.size_in_texels;
  return {texel.x / size, texel.y / size, (float)chart.page};
}

linalg::vec3 lightmap_uv_for(const lightmap_chart_t &chart,
                             const lightmap_bake_settings_t &settings,
                             const lightmap_atlas_t &atlas,
                             const linalg::vec3 &world_position)
{
  return lightmap_uv_from_chart_space(chart, settings, atlas,
                                      to_chart_space(chart, world_position));
}

} // namespace shared

namespace shared
{

const lightmap_chart_t *find_chart(const lightmap_t &lightmap, entity_uid_t object_uid,
                                   const Plane &plane)
{
  const lightmap_chart_t *best = nullptr;
  face_key_match_t best_match;

  for (const lightmap_chart_t &chart : lightmap.charts)
  {
    if (chart.object_uid != object_uid)
      continue;

    const face_key_match_t match = match_face_key(chart.plane, plane);
    if (!match.matched || !match.is_better_than(best_match))
      continue;

    best = &chart;
    best_match = match;
  }

  return best;
}

size_t count_charts_keeping_light(const lightmap_t &lightmap, int16_t slot)
{
  if (slot == LIGHTMAP_NO_LIGHT_SLOT) return 0;

  size_t kept_by = 0;
  for (const lightmap_chart_t &chart : lightmap.charts)
    for (int16_t kept : chart.light_slots)
      if (kept == slot) ++kept_by;
  return kept_by;
}

int16_t find_baked_light_slot(const lightmap_t &lightmap, entity_uid_t light_uid)
{
  for (size_t slot = 0; slot < lightmap.light_uids.size(); ++slot)
    if (lightmap.light_uids[slot] == light_uid) return (int16_t)slot;

  return LIGHTMAP_NO_LIGHT_SLOT;
}

} // namespace shared

namespace shared
{

// The pages are sized from the ATLAS, so what the packer decided and what the
// bake writes into cannot disagree about how many pages there are or how big
// they are.
void lightmap_pages_t::allocate(const lightmap_atlas_t &atlas,
                                lightmap_pixel_format_t pixel_format,
                                int layers_per_atlas_page)
{
  format = pixel_format;
  size_in_texels = atlas.size_in_texels;
  page_count = atlas.page_count * std::max(layers_per_atlas_page, 1);
  bytes.assign(texel_count() * (size_t)bytes_per_texel(format), 0);
}

namespace
{

// A page set answers in the vocabulary of the role its FORMAT is for, and asking
// in the other one is a caller bug rather than a value to coerce -- an
// irradiance read of a coverage texel is four scalars reinterpreted as a shared
// exponent, which is plausible garbage.
size_t checked_offset(const lightmap_pages_t &pages, int page, int x, int y,
                      lightmap_pixel_format_t expected, const char *role)
{
  if (pages.format != expected)
    fatal_error("[lightmap] a {} access to pages in format {}.", role,
                (uint32_t)pages.format);

  const size_t offset = pages.byte_offset_of(page, x, y);
  if (offset + (size_t)bytes_per_texel(pages.format) > pages.bytes.size())
    fatal_error("[lightmap] texel ({}, {}) on page {} is outside {} byte(s) of pages.",
                x, y, page, pages.bytes.size());
  return offset;
}

} // namespace

void lightmap_pages_t::store(int page, int x, int y, const linalg::vec3 &linear_rgb)
{
  const size_t offset =
      checked_offset(*this, page, x, y, lightmap_pixel_format_t::Rgb9e5, "irradiance");

  const uint32_t word = pack_rgb9e5(linear_rgb);
  std::memcpy(&bytes[offset], &word, sizeof(word));
}

linalg::vec3 lightmap_pages_t::load(int page, int x, int y) const
{
  const size_t offset =
      checked_offset(*this, page, x, y, lightmap_pixel_format_t::Rgb9e5, "irradiance");

  uint32_t word = 0;
  std::memcpy(&word, &bytes[offset], sizeof(word));
  return unpack_rgb9e5(word);
}

// The quantization is the plain one: a fraction in [0, 1] scaled to a byte and
// rounded. No sRGB encode and no tone map -- a coverage is data, and the sampler
// that reads it must hand the shader back the fraction that was written.
static_assert(LIGHTMAP_LIGHTS_PER_CHART == 4,
              "Unorm8x4 is four scalars in four bytes. Growing the per-chart light "
              "count needs a pixel format beside it, not a wider read of this one.");

void lightmap_pages_t::store_visibility(
    int page, int x, int y, const Array<float, LIGHTMAP_LIGHTS_PER_CHART> &coverage)
{
  const size_t offset =
      checked_offset(*this, page, x, y, lightmap_pixel_format_t::Unorm8x4, "visibility");

  for (uint32_t slot = 0; slot < LIGHTMAP_LIGHTS_PER_CHART; ++slot)
    bytes[offset + (size_t)slot] =
        (uint8_t)std::clamp((int)std::lround(coverage[slot] * 255.f), 0, 255);
}

Array<float, LIGHTMAP_LIGHTS_PER_CHART> lightmap_pages_t::load_visibility(int page, int x,
                                                                         int y) const
{
  const size_t offset =
      checked_offset(*this, page, x, y, lightmap_pixel_format_t::Unorm8x4, "visibility");

  Array<float, LIGHTMAP_LIGHTS_PER_CHART> coverage;
  for (uint32_t slot = 0; slot < LIGHTMAP_LIGHTS_PER_CHART; ++slot)
    coverage[slot] = (float)bytes[offset + (size_t)slot] * (1.f / 255.f);
  return coverage;
}


void lightmap_pages_t::store_l1(int page, int x, int y, const linalg::vec3 &l0,
                                const Array<linalg::vec3, SH_L1_LAYERS_PER_PAGE> &l1)
{
  for (int axis = 0; axis < SH_L1_LAYERS_PER_PAGE; ++axis)
  {
    const size_t offset =
        checked_offset(*this, page * SH_L1_LAYERS_PER_PAGE + axis, x, y,
                       lightmap_pixel_format_t::Unorm8x4, "SH L1");

    bytes[offset + 0] = encode_sh_l1_component(l1[axis].x, l0.x);
    bytes[offset + 1] = encode_sh_l1_component(l1[axis].y, l0.y);
    bytes[offset + 2] = encode_sh_l1_component(l1[axis].z, l0.z);
    bytes[offset + 3] = 255;
  }
}

Array<linalg::vec3, SH_L1_LAYERS_PER_PAGE>
lightmap_pages_t::load_l1(int page, int x, int y, const linalg::vec3 &l0) const
{
  Array<linalg::vec3, SH_L1_LAYERS_PER_PAGE> l1;
  for (int axis = 0; axis < SH_L1_LAYERS_PER_PAGE; ++axis)
  {
    const size_t offset =
        checked_offset(*this, page * SH_L1_LAYERS_PER_PAGE + axis, x, y,
                       lightmap_pixel_format_t::Unorm8x4, "SH L1");

    l1[axis] = {decode_sh_l1_component(bytes[offset + 0], l0.x),
                decode_sh_l1_component(bytes[offset + 1], l0.y),
                decode_sh_l1_component(bytes[offset + 2], l0.z)};
  }
  return l1;
}

void probe_volume_t::allocate(const probe_grid_t &probe_grid)
{
  grid = probe_grid;
  l0_bytes.assign(grid.probe_count() * 4, 0);
  l1_bytes.assign(grid.probe_count() * 4 * (size_t)SH_L1_LAYERS_PER_PAGE, 0);
  visibility_slots = NO_PROBE_VISIBILITY_SLOTS;
  visibility_bytes.assign(grid.probe_count() * 4, 255);
}

void probe_volume_t::store_visibility(size_t index,
                                      const Array<float, PROBE_VISIBILITY_CHANNELS> &coverage)
{
  if (index >= grid.probe_count())
    fatal_error("[lightmap] probe {} is outside a volume of {}", index, grid.probe_count());

  for (uint32_t channel = 0; channel < PROBE_VISIBILITY_CHANNELS; ++channel)
    visibility_bytes[index * 4 + channel] =
        (uint8_t)std::clamp((int)std::lround(coverage[channel] * 255.f), 0, 255);
}

Array<float, PROBE_VISIBILITY_CHANNELS> probe_volume_t::load_visibility(size_t index) const
{
  if (index >= grid.probe_count())
    fatal_error("[lightmap] probe {} is outside a volume of {}", index, grid.probe_count());

  Array<float, PROBE_VISIBILITY_CHANNELS> coverage;
  for (uint32_t channel = 0; channel < PROBE_VISIBILITY_CHANNELS; ++channel)
    coverage[channel] = (float)visibility_bytes[index * 4 + channel] * (1.f / 255.f);
  return coverage;
}

void probe_volume_t::store(size_t index, const indirect_sh_l1_t &value)
{
  if (index >= grid.probe_count())
    fatal_error("[lightmap] probe {} is outside a volume of {}", index, grid.probe_count());

  const uint32_t word = pack_rgb9e5(value.l0);
  std::memcpy(l0_bytes.data() + index * 4, &word, 4);

  // Quantized L0 for the normalization, so the decode divides by what the
  // reader will actually hold.
  const linalg::vec3 stored_l0 = unpack_rgb9e5(word);
  for (int axis = 0; axis < SH_L1_LAYERS_PER_PAGE; ++axis)
  {
    const size_t offset = ((size_t)axis * grid.probe_count() + index) * 4;
    l1_bytes[offset + 0] = encode_sh_l1_component(value.l1[axis].x, stored_l0.x);
    l1_bytes[offset + 1] = encode_sh_l1_component(value.l1[axis].y, stored_l0.y);
    l1_bytes[offset + 2] = encode_sh_l1_component(value.l1[axis].z, stored_l0.z);
    l1_bytes[offset + 3] = 255;
  }
}

indirect_sh_l1_t probe_volume_t::load(size_t index) const
{
  if (index >= grid.probe_count())
    fatal_error("[lightmap] probe {} is outside a volume of {}", index, grid.probe_count());

  indirect_sh_l1_t value;
  uint32_t word = 0;
  std::memcpy(&word, l0_bytes.data() + index * 4, 4);
  value.l0 = unpack_rgb9e5(word);

  for (int axis = 0; axis < SH_L1_LAYERS_PER_PAGE; ++axis)
  {
    const size_t offset = ((size_t)axis * grid.probe_count() + index) * 4;
    value.l1[axis] = {decode_sh_l1_component(l1_bytes[offset + 0], value.l0.x),
                      decode_sh_l1_component(l1_bytes[offset + 1], value.l0.y),
                      decode_sh_l1_component(l1_bytes[offset + 2], value.l0.z)};
  }
  return value;
}

void set_lightmap_geometry_id(lightmap_t &lightmap)
{
  uint32_t hash = 2166136261u;
  const auto mix = [&](const void *bytes, size_t count) {
    const uint8_t *at = (const uint8_t *)bytes;
    for (size_t i = 0; i < count; ++i)
    {
      hash ^= at[i];
      hash *= 16777619u;
    }
  };

  mix(&lightmap.settings, sizeof(lightmap.settings));
  mix(&lightmap.atlas, sizeof(lightmap.atlas));

  // The resolve table and the slots are in, and the PIXELS are still out. A
  // vertex CARRIES its chart's slots, so a rebake that reassigns one has to
  // rebuild the mesh; a rebake that only moves a texel's value still must not.
  for (entity_uid_t light_uid : lightmap.light_uids)
    mix(&light_uid, sizeof(light_uid));

  for (const lightmap_chart_t &chart : lightmap.charts)
  {
    mix(chart.light_slots.data, sizeof(chart.light_slots.data));
    mix(&chart.object_uid, sizeof(chart.object_uid));
    mix(&chart.plane, sizeof(chart.plane));
    mix(&chart.tangent_u, sizeof(chart.tangent_u));
    mix(&chart.tangent_v, sizeof(chart.tangent_v));
    mix(&chart.origin, sizeof(chart.origin));
    mix(&chart.world_units_per_texel, sizeof(chart.world_units_per_texel));
    mix(&chart.atlas_rect, sizeof(chart.atlas_rect));
    mix(&chart.page, sizeof(chart.page));

    // The unwrap is what a mesh vertex is built from, so it is in too.
    for (const unwrapped_vertex_t &vertex : chart.unwrap.vertices)
    {
      mix(&vertex.xref, sizeof(vertex.xref));
      mix(&vertex.uv, sizeof(vertex.uv));
    }
    for (uint32_t index : chart.unwrap.indices) mix(&index, sizeof(index));
    for (uint32_t face : chart.unwrap.faces) mix(&face, sizeof(face));
  }

  lightmap.geometry_id = hash;
}

} // namespace shared
