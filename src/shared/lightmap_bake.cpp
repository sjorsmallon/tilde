#include "lightmap_bake.hpp"

#include "brush.hpp"
#include "log.hpp"
#include "map_geometry.hpp"

#define STB_RECT_PACK_IMPLEMENTATION
#include "stb_rect_pack.h"

#include <algorithm>
#include <cmath>
#include <cstring>
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
    if (get_kind(object.value) != geometry_kind_t::Brush)
    {
      log_warning("[lightmap] object {} is a static mesh and has no planar faces to "
                  "flatten; it gets no charts.", object.uid);
      continue;
    }

    const brush_geometry_t &brush = std::get<brush_geometry_t>(object.value);

    const std::optional<brush_polyhedron_t> polyhedron =
        try_build_brush_polyhedron(brush.hull_points);
    if (!polyhedron)
    {
      log_error("[lightmap] brush {} did not hull; it gets no charts.", object.uid);
      continue;
    }

    for (const brush_face_t &face : polyhedron->faces)
    {
      const face_surface_t *surface = find_face_surface(brush, face.plane);

      if (surface && !surface->emits_geometry)
        continue;

      // 1. basis
      linalg::vec3 tangent_u;
      linalg::vec3 tangent_v;
      brush_face_grid_tangents(face.plane.normal, tangent_u, tangent_v);

      // 2. project
      const linalg::vec3 reference = polyhedron->vertices[face.vertex_indices[0]];

      float min_u = 0.f, max_u = 0.f, min_v = 0.f, max_v = 0.f;
      for (size_t i = 0; i < face.vertex_indices.size(); ++i)
      {
        const linalg::vec3 offset_from_reference =
            polyhedron->vertices[face.vertex_indices[i]] - reference;
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

      const float lightmap_scale = surface ? surface->lightmap_scale : 1.f;
      if (lightmap_scale <= 0.f)
      {
        log_error("[lightmap] brush {} has a face with lightmap_scale {}; it gets no "
                  "chart.", object.uid, lightmap_scale);
        continue;
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
        log_error("[lightmap] brush {} produced a {}x{} chart past the {} cap after the "
                  "density was already lowered to fit; it gets no chart.", object.uid,
                  width_in_texels, height_in_texels, settings.max_chart_extent_in_texels);
        continue;
      }

      // 6. record
      lightmap_chart_t chart;
      chart.polygon.reserve(face.vertex_indices.size());
      for (uint32_t vertex_index : face.vertex_indices)
      {
        const linalg::vec3 offset_from_reference =
            polyhedron->vertices[vertex_index] - reference;
        chart.polygon.push_back(
            {linalg::dot(offset_from_reference, tangent_u) - snapped_min_u,
             linalg::dot(offset_from_reference, tangent_v) - snapped_min_v});
      }

      chart.object_uid = object.uid;
      chart.plane = face.plane;
      chart.tangent_u = tangent_u;
      chart.tangent_v = tangent_v;
      chart.origin =
          reference + tangent_u * snapped_min_u + tangent_v * snapped_min_v;
      chart.world_units_per_texel = world_units_per_texel;
      chart.atlas_rect.width = width_in_texels;
      chart.atlas_rect.height = height_in_texels;

      charts.push_back(std::move(chart));
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
bool chart_space_is_inside_face(const lightmap_chart_t &chart,
                                const linalg::vec2 &point)
{
  if (chart.polygon.size() < 3) return false;

  bool inside = false;
  for (size_t i = 0, j = chart.polygon.size() - 1; i < chart.polygon.size(); j = i++)
  {
    const linalg::vec2 &a = chart.polygon[i];
    const linalg::vec2 &b = chart.polygon[j];
    if ((a.y > point.y) == (b.y > point.y)) continue;
    const float crossing_x = (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x;
    if (point.x < crossing_x) inside = !inside;
  }
  return inside;
}

linalg::vec3 texel_world_position(const lightmap_chart_t &chart, int texel_x, int texel_y)
{
  return chart_space_to_world(chart,
                              texel_center_in_chart_space(chart, texel_x, texel_y));
}

bool texel_is_inside_face(const lightmap_chart_t &chart, int texel_x, int texel_y)
{
  return chart_space_is_inside_face(
      chart, texel_center_in_chart_space(chart, texel_x, texel_y));
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

linalg::vec3 lightmap_uv_for(const lightmap_chart_t &chart,
                             const lightmap_bake_settings_t &settings,
                             const lightmap_atlas_t &atlas,
                             const linalg::vec3 &world_position)
{
  const linalg::vec3 offset_from_origin = world_position - chart.origin;
  const linalg::vec2 chart_space = {linalg::dot(offset_from_origin, chart.tangent_u),
                                    linalg::dot(offset_from_origin, chart.tangent_v)};

  const linalg::vec2 texel = chart_space_to_atlas_texel(chart, settings, chart_space);
  const float size = (float)atlas.size_in_texels;
  return {texel.x / size, texel.y / size, (float)chart.page};
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
                                lightmap_pixel_format_t pixel_format)
{
  format = pixel_format;
  size_in_texels = atlas.size_in_texels;
  page_count = atlas.page_count;
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
  }

  lightmap.geometry_id = hash;
}

} // namespace shared
