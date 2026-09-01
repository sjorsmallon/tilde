#include "../shared/lighting.hpp"
#include "../shared/lightmap_bake.hpp"
#include "../shared/lightmap_sidecar.hpp"
#include "../shared/lightmap_solve.hpp"

#include <optional>

#include <filesystem>
#include <memory>
#include <vector>

#include <cassert>
#include <cmath>
#include <cstdio>

namespace
{

void a_box_gets_one_chart_per_face()
{
  shared::map_t map;
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({0, 0, 0}, {64, 64, 64})});

  const std::vector<shared::lightmap_chart_t> charts =
      shared::build_lightmap_charts(map, {});

  assert(charts.size() == 6);

  // 128 units at one texel per 4 units is 32, plus a 2-texel gutter each side.
  for (const shared::lightmap_chart_t &chart : charts)
  {
    assert(chart.atlas_rect.width == 36);
    assert(chart.atlas_rect.height == 36);
    assert(std::abs(chart.world_units_per_texel - 4.f) < 1e-4f);
  }
}

// The flattening is an isometry, so a face's texel count follows its WORLD
// extent on each axis and nothing else -- that is what "u and v are unit
// length" buys, and a projection onto a dominant axis would not.
void chart_size_follows_the_face_extent()
{
  shared::map_t map;
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({0, 0, 0}, {64, 32, 16})});

  const std::vector<shared::lightmap_chart_t> charts =
      shared::build_lightmap_charts(map, {});
  assert(charts.size() == 6);

  int face_count_by_texel_area[3] = {0, 0, 0};
  for (const shared::lightmap_chart_t &chart : charts)
  {
    const int covered_width  = chart.atlas_rect.width - 4;
    const int covered_height = chart.atlas_rect.height - 4;
    const int area = covered_width * covered_height;

    // 128x64, 128x32 and 64x32 world units at 4 units per texel.
    if (area == 32 * 16) ++face_count_by_texel_area[0];
    else if (area == 32 * 8) ++face_count_by_texel_area[1];
    else if (area == 16 * 8) ++face_count_by_texel_area[2];
    else assert(false && "unexpected chart size");
  }
  assert(face_count_by_texel_area[0] == 2);
  assert(face_count_by_texel_area[1] == 2);
  assert(face_count_by_texel_area[2] == 2);
}

// A chart's origin is a real world position, and it is the corner the texels
// are measured from -- so it has to lie ON the face's plane.
void a_chart_origin_lies_on_its_face_plane()
{
  shared::map_t map;
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({500, -300, 128}, {64, 64, 64})});

  const std::vector<shared::lightmap_chart_t> charts =
      shared::build_lightmap_charts(map, {});
  assert(charts.size() == 6);

  for (const shared::lightmap_chart_t &chart : charts)
  {
    const float distance_off_plane =
        linalg::dot(chart.origin - chart.plane.point, chart.plane.normal);
    assert(std::abs(distance_off_plane) < 1e-2f);
  }
}

// Snapping the anchor down to a texel boundary is what makes a bake stable, so
// every chart origin must sit on the texel grid.
void a_chart_origin_is_snapped_to_the_texel_grid()
{
  shared::map_t map;
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({37.5f, 11.25f, -3.f}, {50, 50, 50})});

  const std::vector<shared::lightmap_chart_t> charts =
      shared::build_lightmap_charts(map, {});
  assert(charts.size() == 6);

  for (const shared::lightmap_chart_t &chart : charts)
  {
    const linalg::vec3 reference = chart.plane.point;
    const linalg::vec3 offset = chart.origin - reference;
    const float along_u = linalg::dot(offset, chart.tangent_u) / chart.world_units_per_texel;
    const float along_v = linalg::dot(offset, chart.tangent_v) / chart.world_units_per_texel;
    assert(std::abs(along_u - std::round(along_u)) < 1e-3f);
    assert(std::abs(along_v - std::round(along_v)) < 1e-3f);
  }
}

// lightmap_scale is a per-face density multiplier, so doubling it halves the
// world size of a texel and doubles the covered count.
void lightmap_scale_multiplies_the_density()
{
  shared::map_t map;
  shared::brush_geometry_t brush = shared::make_box_brush({0, 0, 0}, {64, 64, 64});
  shared::sync_face_surfaces(brush);
  for (shared::face_surface_t &face : brush.face_surfaces)
    face.lightmap_scale = 2.f;
  map.geometry.push_back({map.next_uid++, brush});

  const std::vector<shared::lightmap_chart_t> charts =
      shared::build_lightmap_charts(map, {});
  assert(charts.size() == 6);

  for (const shared::lightmap_chart_t &chart : charts)
  {
    assert(std::abs(chart.world_units_per_texel - 2.f) < 1e-4f);
    assert(chart.atlas_rect.width == 64 + 4);
  }
}

// A nodraw face draws nothing, so it has nothing to light.
void a_face_that_emits_no_geometry_gets_no_chart()
{
  shared::map_t map;
  shared::brush_geometry_t brush = shared::make_box_brush({0, 0, 0}, {64, 64, 64});
  shared::sync_face_surfaces(brush);
  brush.face_surfaces[0].emits_geometry = false;
  map.geometry.push_back({map.next_uid++, brush});

  const std::vector<shared::lightmap_chart_t> charts =
      shared::build_lightmap_charts(map, {});
  assert(charts.size() == 5);
}

// A placement has to land inside its page and overlap nothing else on it --
// the two properties every consumer of the atlas depends on.
void packing_places_every_chart_without_overlap()
{
  shared::map_t map;
  for (int i = 0; i < 40; ++i)
  {
    map.geometry.push_back({map.next_uid++,
                            shared::make_box_brush({(float)i * 300.f, 0, 0},
                                                   {64.f + (float)i, 32, 96})});
  }

  shared::lightmap_bake_settings_t settings;
  settings.atlas_size_in_texels = 128;

  std::vector<shared::lightmap_chart_t> charts =
      shared::build_lightmap_charts(map, settings);
  assert(charts.size() == 240);

  const shared::lightmap_atlas_t atlas = shared::pack_lightmap_charts(charts, settings);
  assert(atlas.page_count > 1);
  assert(atlas.size_in_texels == 128);

  for (const shared::lightmap_chart_t &chart : charts)
  {
    assert(chart.page >= 0 && chart.page < atlas.page_count);
    assert(chart.atlas_rect.min_x >= 0 && chart.atlas_rect.min_y >= 0);
    assert(chart.atlas_rect.min_x + chart.atlas_rect.width <= atlas.size_in_texels);
    assert(chart.atlas_rect.min_y + chart.atlas_rect.height <= atlas.size_in_texels);
  }

  for (size_t a = 0; a < charts.size(); ++a)
  {
    for (size_t b = a + 1; b < charts.size(); ++b)
    {
      if (charts[a].page != charts[b].page) continue;
      const bool apart_in_x =
          charts[a].atlas_rect.min_x + charts[a].atlas_rect.width <= charts[b].atlas_rect.min_x ||
          charts[b].atlas_rect.min_x + charts[b].atlas_rect.width <= charts[a].atlas_rect.min_x;
      const bool apart_in_y =
          charts[a].atlas_rect.min_y + charts[a].atlas_rect.height <= charts[b].atlas_rect.min_y ||
          charts[b].atlas_rect.min_y + charts[b].atlas_rect.height <= charts[a].atlas_rect.min_y;
      assert(apart_in_x || apart_in_y);
    }
  }
}

// A chart bigger than a whole page can never be placed, and saying so is the
// point: quietly leaving it at page -1 is a face that bakes black.
void a_chart_too_big_for_a_page_fails_loudly()
{
  shared::map_t map;
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({0, 0, 0}, {64, 64, 64})});

  shared::lightmap_bake_settings_t settings;
  settings.atlas_size_in_texels = 8;

  std::vector<shared::lightmap_chart_t> charts =
      shared::build_lightmap_charts(map, settings);
  const shared::lightmap_atlas_t atlas = shared::pack_lightmap_charts(charts, settings);
  assert(atlas.page_count == 0);
}

// The polygon is in chart space, so it has to fall inside the rect the chart
// was sized to hold it.
void a_chart_polygon_fits_inside_its_own_rect()
{
  shared::map_t map;
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({13.f, -7.f, 90.f}, {64, 40, 24})});

  const std::vector<shared::lightmap_chart_t> charts =
      shared::build_lightmap_charts(map, {});
  assert(charts.size() == 6);

  for (const shared::lightmap_chart_t &chart : charts)
  {
    assert(chart.polygon.size() >= 3);
    const int covered_width  = chart.atlas_rect.width - 4;
    const int covered_height = chart.atlas_rect.height - 4;
    for (const linalg::vec2 &point : chart.polygon)
    {
      const float u = point.x / chart.world_units_per_texel;
      const float v = point.y / chart.world_units_per_texel;
      assert(u >= -1e-3f && u <= (float)covered_width + 1e-3f);
      assert(v >= -1e-3f && v <= (float)covered_height + 1e-3f);
    }
  }
}

// The size cap must lower DENSITY, never truncate the rect: a truncated rect
// leaves the face mapping past its own allocation and sampling its neighbour.
void the_chart_size_cap_lowers_density_instead_of_truncating()
{
  shared::map_t map;
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({0, 0, 0}, {2048, 64, 64})});

  shared::lightmap_bake_settings_t settings;
  const std::vector<shared::lightmap_chart_t> charts =
      shared::build_lightmap_charts(map, settings);
  assert(charts.size() == 6);

  bool saw_a_capped_chart = false;
  for (const shared::lightmap_chart_t &chart : charts)
  {
    assert(chart.atlas_rect.width <= settings.max_chart_extent_in_texels);
    assert(chart.atlas_rect.height <= settings.max_chart_extent_in_texels);

    if (chart.atlas_rect.width > settings.max_chart_extent_in_texels / 2)
      saw_a_capped_chart = true;

    const int covered_width = shared::chart_covered_width(chart, settings);
    const int covered_height = shared::chart_covered_height(chart, settings);
    for (const linalg::vec2 &point : chart.polygon)
    {
      assert(point.x / chart.world_units_per_texel <= (float)covered_width + 1e-3f);
      assert(point.y / chart.world_units_per_texel <= (float)covered_height + 1e-3f);
    }
  }
  assert(saw_a_capped_chart);
}

// The UV a vertex carries is (u, v, LAYER), and it must land on the same texel
// the bake wrote -- these are the two ends of the one mapping.
void a_vertex_uv_lands_on_its_own_chart()
{
  shared::map_t map;
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({256, -64, 32}, {64, 48, 96})});

  shared::lightmap_bake_settings_t settings;
  std::vector<shared::lightmap_chart_t> charts =
      shared::build_lightmap_charts(map, settings);
  const shared::lightmap_atlas_t atlas = shared::pack_lightmap_charts(charts, settings);
  assert(atlas.page_count == 1);

  for (const shared::lightmap_chart_t &chart : charts)
  {
    const int covered_width = shared::chart_covered_width(chart, settings);
    const int covered_height = shared::chart_covered_height(chart, settings);

    for (int texel_y = 0; texel_y < covered_height; ++texel_y)
    {
      for (int texel_x = 0; texel_x < covered_width; ++texel_x)
      {
        const linalg::vec3 world = shared::texel_world_position(chart, texel_x, texel_y);
        const linalg::vec3 uv =
            shared::lightmap_uv_for(chart, settings, atlas, world);

        assert((int)uv.z == chart.page);

        const float atlas_x = uv.x * (float)atlas.size_in_texels;
        const float atlas_y = uv.y * (float)atlas.size_in_texels;
        const int expected_x = chart.atlas_rect.min_x + settings.gutter_in_texels + texel_x;
        const int expected_y = chart.atlas_rect.min_y + settings.gutter_in_texels + texel_y;

        assert(std::abs(atlas_x - ((float)expected_x + 0.5f)) < 1e-2f);
        assert(std::abs(atlas_y - ((float)expected_y + 0.5f)) < 1e-2f);
      }
    }
  }
}

// --- Persistence and the write-back -----------------------------------------

shared::lightmap_t bake_one_box(shared::map_t &map)
{
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({0, 0, 0}, {64, 64, 64})});

  shared::lightmap_t lightmap;
  lightmap.charts = shared::build_lightmap_charts(map, lightmap.settings);
  lightmap.atlas = shared::pack_lightmap_charts(lightmap.charts, lightmap.settings);
  lightmap.irradiance_pages.allocate(lightmap.atlas,
                                     shared::lightmap_pixel_format_t::Rgb9e5);
  lightmap.irradiance_pages.store(0, 0, 0, {0.5f, 0.25f, 2.f});

  // The visibility half, written by hand for the same reason the irradiance one
  // is: this fixture is about what SURVIVES a round trip, not about what a solve
  // would have chosen.
  lightmap.light_uids = {11, 22, 33};
  lightmap.visibility_pages.allocate(lightmap.atlas,
                                     shared::lightmap_pixel_format_t::Unorm8x4);
  lightmap.visibility_pages.store_visibility(0, 0, 0, {{1.f, 0.5f, 0.f, 0.25f}});
  for (shared::lightmap_chart_t &chart : lightmap.charts)
  {
    chart.light_slots[0] = 2;
    chart.light_slots[1] = 0;
  }

  shared::set_lightmap_geometry_id(lightmap);
  return lightmap;
}

// Everything a MESH is built from has to survive the round trip exactly: the
// vertex UVs are derived from it, and a float off by an ulp is a face sampling
// the wrong texel forever.
void a_sidecar_round_trips_every_chart()
{
  shared::map_t map;
  const shared::lightmap_t baked = bake_one_box(map);

  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() / "tilde_lightmap_test";
  std::filesystem::create_directories(directory);
  const std::string map_path = (directory / "round_trip.source").generic_string();

  const uint32_t hash = shared::compute_map_content_hash(map);
  shared::save_lightmap_sidecar(map_path, baked, hash);
  const shared::lightmap_t loaded = shared::load_lightmap_sidecar(map_path, hash);

  assert(loaded.charts.size() == baked.charts.size());
  assert(loaded.atlas.page_count == baked.atlas.page_count);
  assert(loaded.atlas.size_in_texels == baked.atlas.size_in_texels);
  assert(loaded.irradiance_pages.bytes.size() == baked.irradiance_pages.bytes.size());
  assert(loaded.irradiance_pages.format == baked.irradiance_pages.format);

  // The second page set and the table that gives its channels a meaning. A
  // sidecar that carried the pixels and not the table would hand every chart
  // four unnamed numbers.
  assert(loaded.light_uids == baked.light_uids);
  assert(loaded.visibility_pages.format == shared::lightmap_pixel_format_t::Unorm8x4);
  assert(loaded.visibility_pages.bytes == baked.visibility_pages.bytes);

  const Array<float, shared::LIGHTMAP_LIGHTS_PER_CHART> coverage =
      loaded.visibility_pages.load_visibility(0, 0, 0);
  assert(coverage[0] == 1.f);
  assert(std::abs(coverage[1] - 0.5f) < 0.01f);
  assert(coverage[2] == 0.f);
  assert(std::abs(coverage[3] - 0.25f) < 0.01f);

  // Four bytes a texel, and the texel that was written comes back as the same
  // three floats -- a sidecar that sized itself by texels rather than by bytes
  // reads three quarters of a page and calls it a whole one.
  assert(loaded.irradiance_pages.bytes.size() == loaded.irradiance_pages.texel_count() * 4);
  const linalg::vec3 stored = loaded.irradiance_pages.load(0, 0, 0);
  const linalg::vec3 expected = baked.irradiance_pages.load(0, 0, 0);
  assert(stored.x == expected.x && stored.y == expected.y && stored.z == expected.z);

  for (size_t i = 0; i < loaded.charts.size(); ++i)
  {
    const shared::lightmap_chart_t &before = baked.charts[i];
    const shared::lightmap_chart_t &after = loaded.charts[i];

    assert(after.object_uid == before.object_uid);
    assert(after.page == before.page);
    assert(after.atlas_rect.min_x == before.atlas_rect.min_x);
    assert(after.atlas_rect.min_y == before.atlas_rect.min_y);
    assert(after.atlas_rect.width == before.atlas_rect.width);
    assert(after.atlas_rect.height == before.atlas_rect.height);
    assert(after.world_units_per_texel == before.world_units_per_texel);
    assert(after.origin.x == before.origin.x && after.origin.y == before.origin.y &&
           after.origin.z == before.origin.z);
    assert(after.plane.normal.x == before.plane.normal.x);
    assert(after.tangent_u.x == before.tangent_u.x);
    assert(after.tangent_v.z == before.tangent_v.z);

    for (uint32_t slot = 0; slot < shared::LIGHTMAP_LIGHTS_PER_CHART; ++slot)
      assert(after.light_slots[slot] == before.light_slots[slot]);

    // The POLYGON is deliberately not stored -- it is the bake's own coverage
    // test and nothing downstream reads it.
    assert(after.polygon.empty());
  }

  // Which is why the id is computed from what IS stored: a reload that changed
  // it would rebuild every cached mesh on every map load.
  assert(loaded.geometry_id == baked.geometry_id);

  std::filesystem::remove_all(directory);
}

// A rebake moves charts in the atlas without moving a vertex or changing a
// material, so the id is the only thing that can tell the mesh cache to rebuild.
void the_geometry_id_follows_the_packing()
{
  shared::map_t map;
  const shared::lightmap_t coarse = bake_one_box(map);

  shared::lightmap_t fine;
  fine.settings.texels_per_world_unit = 0.5f;
  fine.charts = shared::build_lightmap_charts(map, fine.settings);
  fine.atlas = shared::pack_lightmap_charts(fine.charts, fine.settings);
  shared::set_lightmap_geometry_id(fine);
  assert(fine.geometry_id != coarse.geometry_id);

  // ...and identical input gives an identical id, so an unchanged rebake
  // correctly rebuilds nothing. A counter could not say that.
  shared::lightmap_t again;
  again.charts = shared::build_lightmap_charts(map, again.settings);
  again.atlas = shared::pack_lightmap_charts(again.charts, again.settings);
  again.light_uids = coarse.light_uids;
  for (size_t i = 0; i < again.charts.size(); ++i)
    again.charts[i].light_slots = coarse.charts[i].light_slots;
  shared::set_lightmap_geometry_id(again);
  assert(again.geometry_id == coarse.geometry_id);

  // A chart that kept a DIFFERENT light is a different mesh, because the slots
  // ride the vertices -- so reassigning one has to rebuild it, exactly as a
  // chart moving in the atlas does. Nothing else about the bake distinguishes
  // the two.
  shared::lightmap_t reassigned = again;
  reassigned.charts[0].light_slots[0] = 1;
  shared::set_lightmap_geometry_id(reassigned);
  assert(reassigned.geometry_id != again.geometry_id);
}

// Two brushes can share a plane -- a floor and the slab beneath it -- and a plane
// alone would let one wear the other's lighting.
void a_chart_never_crosses_to_another_object()
{
  shared::map_t map;
  const shared::entity_uid_t lower = map.next_uid++;
  map.geometry.push_back({lower, shared::make_box_brush({0, 0, 0}, {64, 8, 64})});
  const shared::entity_uid_t upper = map.next_uid++;
  map.geometry.push_back({upper, shared::make_box_brush({0, 32, 0}, {64, 8, 64})});

  shared::lightmap_t lightmap;
  lightmap.charts = shared::build_lightmap_charts(map, lightmap.settings);
  lightmap.atlas = shared::pack_lightmap_charts(lightmap.charts, lightmap.settings);

  const Plane top_of_lower{{0, 8, 0}, {0, 1, 0}};

  const shared::lightmap_chart_t *from_lower =
      shared::find_chart(lightmap, lower, top_of_lower);
  const shared::lightmap_chart_t *from_upper =
      shared::find_chart(lightmap, upper, top_of_lower);

  // The distance tolerance is deliberately loose -- a face slid along its own
  // normal by a drag is still that face -- so the upper brush answers with its
  // OWN nearest parallel face rather than with nothing. That is exactly why the
  // uid is in the key: what must never happen is one of them answering with the
  // other's chart.
  assert(from_lower && from_lower->object_uid == lower);
  assert(from_upper && from_upper->object_uid == upper);
  assert(from_lower != from_upper);
}

// The whole point of the sidecar: a vertex carries where to sample from.
void a_lit_face_writes_uvs_and_a_strange_brush_writes_none()
{
  shared::map_t map;
  const shared::lightmap_t lightmap = bake_one_box(map);
  const shared::brush_geometry_t &brush =
      std::get<shared::brush_geometry_t>(map.geometry[0].value);
  const shared::entity_uid_t uid = map.geometry[0].uid;

  const std::vector<std::string> materials{""};

  // No bake at all: the array stays empty and the mesh uploads what it always did.
  const assets::mesh_asset_t unlit = shared::generate_brush_mesh(brush, materials);
  assert(!unlit.is_lightmapped());

  const assets::mesh_asset_t lit =
      shared::generate_brush_mesh(brush, materials, {&lightmap, uid});
  assert(lit.is_lightmapped());
  assert(lit.lightmap_uv.size() == lit.vertices.size());

  for (const linalg::vec3 &uv : lit.lightmap_uv)
  {
    assert(uv.x >= 0.f && uv.x <= 1.f);
    assert(uv.y >= 0.f && uv.y <= 1.f);
    assert(uv.z >= 0.f && uv.z < (float)lightmap.atlas.page_count);
  }

  // A brush the bake never reached carries no array at all, rather than one full
  // of zeros -- zero is texel (0, 0) of page 0, and it belongs to somebody else.
  const shared::geometry_value_t stranger_value =
      shared::make_box_brush({4096, 0, 0}, {8, 8, 8});
  const shared::brush_geometry_t &stranger_brush =
      std::get<shared::brush_geometry_t>(stranger_value);
  const assets::mesh_asset_t stranger =
      shared::generate_brush_mesh(stranger_brush, materials, {&lightmap, 9999});
  assert(!stranger.is_lightmapped());
}


// --- The pixel format --------------------------------------------------------

// The whole reason RGB9E5 was chosen over RGB8: a dark corner and a floodlit
// wall each get full precision at their own scale, so one bake holds both. At
// eight bits a channel the dim end here is a single step from black.
void rgb9e5_holds_every_scale_a_bake_produces()
{
  const float values[] = {1e-5f, 1e-3f, 0.05f, 0.5f, 1.f, 17.f, 900.f, 60000.f};

  for (float value : values)
  {
    const linalg::vec3 round_tripped =
        shared::unpack_rgb9e5(shared::pack_rgb9e5({value, value, value}));

    // Nine mantissa bits is one part in 512, so half a step is the bound and
    // 0.2% covers it at every one of those decades.
    assert(std::abs(round_tripped.x - value) <= value * 0.002f);
    assert(round_tripped.x == round_tripped.y && round_tripped.y == round_tripped.z);
  }

  assert(shared::pack_rgb9e5({0.f, 0.f, 0.f}) == 0u);
  const linalg::vec3 zero = shared::unpack_rgb9e5(0u);
  assert(zero.x == 0.f && zero.y == 0.f && zero.z == 0.f);
}

// The exponent is SHARED, taken from the brightest channel -- so the bright one
// keeps its precision and a much dimmer one beside it pays for it. That is the
// trade the format makes, and it is worth pinning: a bake needing the dim
// channel exact needs a different format, not a different call site.
void rgb9e5_takes_its_exponent_from_the_brightest_channel()
{
  const linalg::vec3 lopsided =
      shared::unpack_rgb9e5(shared::pack_rgb9e5({1000.f, 0.5f, 0.f}));

  assert(std::abs(lopsided.x - 1000.f) <= 1000.f * 0.002f);
  // The dim channel survives, coarsely: one step of the shared exponent here is
  // about two units, so half a step is the most it can be off by.
  assert(std::abs(lopsided.y - 0.5f) <= 2.f);
  assert(lopsided.z == 0.f);

  // Clamped rather than wrapped -- an over-bright texel is white, never dark.
  const linalg::vec3 clamped =
      shared::unpack_rgb9e5(shared::pack_rgb9e5({1e9f, 0.f, 0.f}));
  assert(clamped.x >= shared::RGB9E5_MAX_VALUE * 0.99f);

  // Rounding the brightest channel up to 2^9 must carry the exponent rather than
  // overflow its mantissa into the channel above it.
  const float just_under = std::nextafter(2.f, 0.f);
  const linalg::vec3 carried =
      shared::unpack_rgb9e5(shared::pack_rgb9e5({just_under, 0.f, 0.f}));
  assert(std::abs(carried.x - 2.f) < 0.01f);
  assert(carried.y == 0.f && carried.z == 0.f);
}

// --- The solve, and its two modes --------------------------------------------

shared::map_t map_with_a_floor_and_a_light(
    float light_height, float intensity,
    entities::Light_Mode mode = entities::Light_Mode::Baked)
{
  shared::map_t map;
  map.geometry.push_back(
      {map.next_uid++, shared::make_box_brush({0, -8, 0}, {128, 8, 128})});

  std::shared_ptr<entities::Point_Light_Entity> light =
      std::make_shared<entities::Point_Light_Entity>();
  light->position = {0, light_height, 0};
  light->range = 1024.f;
  light->light.color = {1.f, 1.f, 1.f};
  light->light.intensity = intensity;
  light->light.mode = mode;
  map.entities.push_back({map.next_uid++, light});

  return map;
}

shared::lightmap_t pack_for(const shared::map_t &map)
{
  shared::lightmap_t lightmap;
  lightmap.charts = shared::build_lightmap_charts(map, lightmap.settings);
  lightmap.atlas = shared::pack_lightmap_charts(lightmap.charts, lightmap.settings);
  return lightmap;
}

// Pack and solve, which is what every assertion below is about -- a bake fills
// both page sets, the resolve table and every chart's slots, so the whole
// lightmap is the answer rather than one buffer out of it.
shared::lightmap_t bake_for(const shared::map_t &map,
                            const shared::lightmap_solve_settings_t &solve = {},
                            shared::lightmap_visibility_masks_t *masks = nullptr)
{
  shared::lightmap_t lightmap = pack_for(map);
  shared::bake_lightmap(map, lightmap, solve, masks);
  return lightmap;
}

// The texel directly under the light, which is the one both modes have the most
// to say about.
linalg::vec3 texel_under_the_light(const shared::lightmap_t &lightmap)
{
  const shared::lightmap_pages_t &pages = lightmap.irradiance_pages;

  for (const shared::lightmap_chart_t &chart : lightmap.charts)
  {
    if (chart.plane.normal.y < 0.9f) continue;

    const int x = chart.atlas_rect.min_x + lightmap.settings.gutter_in_texels +
                  shared::chart_covered_width(chart, lightmap.settings) / 2;
    const int y = chart.atlas_rect.min_y + lightmap.settings.gutter_in_texels +
                  shared::chart_covered_height(chart, lightmap.settings) / 2;
    return pages.load(chart.page, x, y);
  }
  assert(false && "the floor has no upward face");
  return {0.f, 0.f, 0.f};
}

// Visibility is a MODE, not a second implementation -- so it writes the same
// RGB9E5 pages, and every lit texel is exactly white. Anything else means the
// falloff leaked into the arm that exists to have none.
void the_visibility_mode_writes_white_or_nothing()
{
  const shared::map_t map = map_with_a_floor_and_a_light(64.f, 1.f);

  shared::lightmap_solve_settings_t solve;
  solve.mode = shared::lightmap_solve_mode_t::Visibility;

  const shared::lightmap_t lightmap = bake_for(map, solve);
  const shared::lightmap_pages_t &pages = lightmap.irradiance_pages;

  assert(pages.format == shared::lightmap_pixel_format_t::Rgb9e5);
  assert(pages.bytes.size() == pages.texel_count() * 4);

  const linalg::vec3 lit = texel_under_the_light(lightmap);
  assert(lit.x == 1.f && lit.y == 1.f && lit.z == 1.f);

  size_t lit_count = 0;
  for (int y = 0; y < pages.size_in_texels; ++y)
    for (int x = 0; x < pages.size_in_texels; ++x)
    {
      const linalg::vec3 texel = pages.load(0, x, y);
      if (texel.x == 0.f && texel.y == 0.f && texel.z == 0.f) continue;
      assert(texel.x == 1.f && texel.y == 1.f && texel.z == 1.f);
      ++lit_count;
    }
  assert(lit_count > 0);
}

// And the direct mode is the same walk with the contribution put back: the same
// texels lit, now carrying a value that FALLS OFF. A light twice as far away is
// the check, because it separates "the falloff ran" from "a constant was
// written".
void the_direct_mode_falls_off_and_scales_with_intensity()
{
  const shared::lightmap_solve_settings_t solve;

  const shared::map_t near_map = map_with_a_floor_and_a_light(64.f, 1.f);
  const linalg::vec3 near_texel = texel_under_the_light(bake_for(near_map, solve));

  const shared::map_t far_map = map_with_a_floor_and_a_light(128.f, 1.f);
  const linalg::vec3 far_texel = texel_under_the_light(bake_for(far_map, solve));

  const shared::map_t bright_map = map_with_a_floor_and_a_light(64.f, 4.f);
  const linalg::vec3 bright_texel = texel_under_the_light(bake_for(bright_map, solve));

  assert(near_texel.x > 0.f);
  assert(far_texel.x > 0.f);
  assert(far_texel.x < near_texel.x);

  // Not white, which is what the visibility mode would have written here.
  assert(near_texel.x != 1.f);

  // Radiance is colour * intensity and enters the sum linearly, so four times the
  // intensity is four times the irradiance at the same texel.
  assert(std::abs(bright_texel.x - near_texel.x * 4.f) <= near_texel.x * 0.02f);
}

// lighting_def.md ss2's correctness requirement, and the only assertion that can
// tell it landed: the mode decides WHERE a light is evaluated, so Dynamic must
// leave the atlas alone and Mixed must not. Without the filter every light in the
// map is in the atlas AND in the runtime array, and a level lit by both is lit
// twice -- which reads as "the bake is too bright" long before anyone suspects
// double counting.
void the_light_mode_decides_what_reaches_the_atlas()
{
  const shared::lightmap_solve_settings_t solve;

  // The same floor and light every time; only the mode of the light, and whether
  // a SECOND light stands beside it, change.
  const auto texel_for = [&](entities::Light_Mode mode,
                             std::optional<entities::Light_Mode> second) {
    shared::map_t map = map_with_a_floor_and_a_light(64.f, 1.f, mode);
    if (second)
    {
      std::shared_ptr<entities::Point_Light_Entity> extra =
          std::make_shared<entities::Point_Light_Entity>();
      extra->position = {0, 64, 0};
      extra->range = 1024.f;
      extra->light.intensity = 1.f;
      extra->light.mode = *second;
      map.entities.push_back({map.next_uid++, extra});
    }

    return texel_under_the_light(bake_for(map, solve));
  };

  const linalg::vec3 baked = texel_for(entities::Light_Mode::Baked, {});
  assert(baked.x > 0.f);

  // Mixed is baked AND analytic, so the atlas half must be identical to Baked's.
  const linalg::vec3 mixed = texel_for(entities::Light_Mode::Mixed, {});
  assert(mixed.x == baked.x && mixed.y == baked.y && mixed.z == baked.z);

  // A Dynamic light standing in the same spot contributes NOTHING here; it is the
  // runtime array's, and adding it to both would be the double count.
  const linalg::vec3 with_dynamic =
      texel_for(entities::Light_Mode::Baked, entities::Light_Mode::Dynamic);
  assert(with_dynamic.x == baked.x);

  // Where a second BAKED light does land, which is what proves the assertion
  // above is measuring the mode rather than a solve that ignores second lights.
  const linalg::vec3 with_baked =
      texel_for(entities::Light_Mode::Baked, entities::Light_Mode::Baked);
  assert(with_baked.x > baked.x * 1.5f);
}

// A texel with a lid over it is dark in BOTH modes: the shadow ray is a gate
// they share, which is the whole reason the binary solve is worth keeping as a
// debug view of the real one.
void an_occluder_darkens_both_modes()
{
  shared::map_t map = map_with_a_floor_and_a_light(64.f, 1.f);
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({0, 32, 0}, {32, 4, 32})});

  const shared::lightmap_solve_mode_t modes[] = {
      shared::lightmap_solve_mode_t::Direct_Light,
      shared::lightmap_solve_mode_t::Visibility};

  for (shared::lightmap_solve_mode_t mode : modes)
  {
    shared::lightmap_solve_settings_t solve;
    solve.mode = mode;

    const linalg::vec3 shadowed = texel_under_the_light(bake_for(map, solve));
    assert(shadowed.x == 0.f && shadowed.y == 0.f && shadowed.z == 0.f);
  }
}


// --- The per-light visibility masks ------------------------------------------

const shared::lightmap_chart_t *upward_chart(const shared::lightmap_t &lightmap)
{
  for (const shared::lightmap_chart_t &chart : lightmap.charts)
    if (chart.plane.normal.y > 0.9f) return &chart;
  assert(false && "the floor has no upward face");
  return nullptr;
}

shared::entity_uid_t only_light_uid(const shared::map_t &map)
{
  for (const shared::map_entity_t &entry : map.entities)
    if (entry.entity && shared::try_light_of(*entry.entity)) return entry.uid;
  assert(false && "the map holds no light");
  return 0;
}

// The covered texel of the top face whose world position is nearest (x, *, 0),
// as an atlas coordinate -- which is what a mask is indexed by.
void top_face_texel_nearest(const shared::lightmap_t &lightmap, float world_x,
                            int &out_atlas_x, int &out_atlas_y)
{
  const shared::lightmap_chart_t &chart = *upward_chart(lightmap);
  const int gutter = lightmap.settings.gutter_in_texels;

  float best = 1e30f;
  for (int y = 0; y < shared::chart_covered_height(chart, lightmap.settings); ++y)
    for (int x = 0; x < shared::chart_covered_width(chart, lightmap.settings); ++x)
    {
      if (!shared::texel_is_inside_face(chart, x, y)) continue;

      const linalg::vec3 position = shared::texel_world_position(chart, x, y);
      const float distance = std::abs(position.x - world_x) + std::abs(position.z);
      if (distance >= best) continue;

      best = distance;
      out_atlas_x = chart.atlas_rect.min_x + gutter + x;
      out_atlas_y = chart.atlas_rect.min_y + gutter + y;
    }

  assert(best < 1e29f);
}

// A slot is named by the light it is of, and what it holds is the shadow: the lid
// blocks the texel under the light and blocks nothing out at the slab's edge.
void a_mask_slot_is_named_by_its_light_and_carries_the_shadow()
{
  shared::map_t map = map_with_a_floor_and_a_light(64.f, 1.f);
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({0, 32, 0}, {32, 4, 32})});

  shared::lightmap_visibility_masks_t masks;
  const shared::lightmap_t lightmap = bake_for(map, {}, &masks);

  assert(masks.slot_count() == 1);
  assert(masks.light_uids[0] == only_light_uid(map));
  assert(masks.size_in_texels == lightmap.atlas.size_in_texels);
  assert(masks.page_count == lightmap.atlas.page_count);

  const shared::lightmap_chart_t &chart = *upward_chart(lightmap);

  int shadowed_x = 0, shadowed_y = 0;
  top_face_texel_nearest(lightmap, 0.f, shadowed_x, shadowed_y);
  assert(masks.coverage[masks.index_of(0, chart.page, shadowed_x, shadowed_y)] == 0.f);

  int clear_x = 0, clear_y = 0;
  top_face_texel_nearest(lightmap, 100.f, clear_x, clear_y);
  assert(masks.coverage[masks.index_of(0, chart.page, clear_x, clear_y)] == 1.f);
}

// The one that is invisible until it isn't (lighting_def.md ss14 step 6). The light
// sits just BELOW the floor's plane and off past its edge, so N.L against the flat
// face normal is negative and the irradiance is zero -- while the shadow ray, which
// leaves the slab over its edge, is completely clear. A mask gated on `reaches`
// would bake zero here, and the surface it kills is the normal-mapped one whose
// shaded normal does face the light.
void a_mask_is_not_gated_on_the_flat_face_normal()
{
  shared::map_t map;
  map.geometry.push_back(
      {map.next_uid++, shared::make_box_brush({0, -8, 0}, {128, 8, 128})});

  std::shared_ptr<entities::Point_Light_Entity> light =
      std::make_shared<entities::Point_Light_Entity>();
  light->position = {400.f, -1.f, 0.f};
  light->range = 1024.f;
  light->light.intensity = 1.f;
  map.entities.push_back({map.next_uid++, light});

  shared::lightmap_visibility_masks_t masks;
  const shared::lightmap_t lightmap = bake_for(map, {}, &masks);

  const shared::lightmap_chart_t &chart = *upward_chart(lightmap);
  int texel_x = 0, texel_y = 0;
  top_face_texel_nearest(lightmap, 100.f, texel_x, texel_y);

  const linalg::vec3 irradiance =
      lightmap.irradiance_pages.load(chart.page, texel_x, texel_y);
  assert(irradiance.x == 0.f && irradiance.y == 0.f && irradiance.z == 0.f);

  assert(masks.coverage[masks.index_of(0, chart.page, texel_x, texel_y)] == 1.f);
}

// Asking for masks costs shadow rays the sum does not need, and it must cost
// nothing else: the pixels are the same bytes either way, in both modes.
void asking_for_masks_does_not_move_a_pixel()
{
  shared::map_t map = map_with_a_floor_and_a_light(64.f, 1.f);
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({0, 32, 0}, {32, 4, 32})});

  const shared::lightmap_solve_mode_t modes[] = {
      shared::lightmap_solve_mode_t::Direct_Light,
      shared::lightmap_solve_mode_t::Visibility};

  for (shared::lightmap_solve_mode_t mode : modes)
  {
    shared::lightmap_solve_settings_t solve;
    solve.mode = mode;

    const shared::lightmap_t without = bake_for(map, solve);

    shared::lightmap_visibility_masks_t masks;
    const shared::lightmap_t with = bake_for(map, solve, &masks);

    assert(!masks.empty());
    assert(without.irradiance_pages.bytes == with.irradiance_pages.bytes);

    // And the visibility half is the same both ways too: what a chart keeps is
    // ranked from the same walk, so the debug output cannot steer the bake.
    assert(without.visibility_pages.bytes == with.visibility_pages.bytes);
  }
}

// Every chart on the page whose plane faces some way other than up. A light
// overhead reaches none of them, so they bake black -- which makes them the
// no-bleed half of the gutter test.
std::vector<const shared::lightmap_chart_t *>
charts_facing_away_from_up(const shared::lightmap_t &lightmap)
{
  std::vector<const shared::lightmap_chart_t *> away;
  for (const shared::lightmap_chart_t &chart : lightmap.charts)
    if (chart.plane.normal.y < 0.9f) away.push_back(&chart);
  return away;
}

// --- The stored visibility: slots, the resolve table, and the cap ------------

// A point light of the given intensity, standing over the floor.
shared::entity_uid_t add_light(shared::map_t &map, const linalg::vec3 &position,
                               float intensity)
{
  std::shared_ptr<entities::Point_Light_Entity> light =
      std::make_shared<entities::Point_Light_Entity>();
  light->position = position;
  light->range = 4096.f;
  light->light.color = {1.f, 1.f, 1.f};
  light->light.intensity = intensity;

  const shared::entity_uid_t uid = map.next_uid++;
  map.entities.push_back({uid, light});
  return uid;
}

shared::map_t map_with_a_floor()
{
  shared::map_t map;
  map.geometry.push_back(
      {map.next_uid++, shared::make_box_brush({0, -8, 0}, {128, 8, 128})});
  return map;
}

// The two halves of a stored visibility, and neither is any use without the
// other: a channel holds a coverage, and the chart's slot is what says which
// light that coverage is OF. The lid over the middle of the floor is what makes
// the two distinguishable -- it darkens one light's channel and not the other's.
void a_chart_names_its_lights_and_stores_their_coverage()
{
  shared::map_t map = map_with_a_floor();
  const shared::entity_uid_t overhead = add_light(map, {0, 64, 0}, 1.f);
  const shared::entity_uid_t off_to_the_side = add_light(map, {400, 64, 0}, 1.f);
  map.geometry.push_back(
      {map.next_uid++, shared::make_box_brush({0, 32, 0}, {32, 4, 32})});

  const shared::lightmap_t lightmap = bake_for(map);

  assert(lightmap.light_uids.size() == 2);
  assert(lightmap.light_uids[0] == overhead);
  assert(lightmap.light_uids[1] == off_to_the_side);
  assert(lightmap.visibility_pages.format == shared::lightmap_pixel_format_t::Unorm8x4);
  assert(lightmap.visibility_pages.bytes.size() ==
         lightmap.visibility_pages.texel_count() * 4);

  const shared::lightmap_chart_t &chart = *upward_chart(lightmap);

  // Both lights reach the top face, so both are kept -- and the slot ORDER is
  // the ranking, strongest first, which puts the one standing over the middle of
  // the floor ahead of the one 400 units off to the side.
  assert(chart.light_slots[0] == 0);
  assert(chart.light_slots[1] == 1);
  assert(chart.light_slots[2] == shared::LIGHTMAP_NO_LIGHT_SLOT);
  assert(chart.light_slots[3] == shared::LIGHTMAP_NO_LIGHT_SLOT);

  int shadowed_x = 0, shadowed_y = 0;
  top_face_texel_nearest(lightmap, 0.f, shadowed_x, shadowed_y);
  const Array<float, shared::LIGHTMAP_LIGHTS_PER_CHART> shadowed =
      lightmap.visibility_pages.load_visibility(chart.page, shadowed_x, shadowed_y);

  // Under the lid: the overhead light is blocked and the one off to the side
  // reaches it, which is the whole reason a mask is per LIGHT.
  assert(shadowed[0] == 0.f);
  assert(shadowed[1] == 1.f);

  int clear_x = 0, clear_y = 0;
  top_face_texel_nearest(lightmap, 100.f, clear_x, clear_y);
  const Array<float, shared::LIGHTMAP_LIGHTS_PER_CHART> clear =
      lightmap.visibility_pages.load_visibility(chart.page, clear_x, clear_y);
  assert(clear[0] == 1.f);
  assert(clear[1] == 1.f);

  // An unclaimed slot stores ZERO, which reads as fully occluded. One left at 1
  // is a light nobody baked shining through every wall.
  assert(clear[2] == 0.f);
  assert(clear[3] == 0.f);
}

// The cap is a CLIFF and the policy is to drop, loudly. What must not happen is
// the drop being silent, or the four kept being the four the map happened to
// declare first -- so the weakest light stands closest to the face and would win
// any ranking by coverage rather than by delivery.
void a_chart_keeps_its_strongest_lights_and_drops_the_rest()
{
  shared::map_t map = map_with_a_floor();

  const shared::entity_uid_t brightest = add_light(map, {0, 400, 0}, 64.f);
  add_light(map, {40, 400, 0}, 32.f);
  add_light(map, {-40, 400, 0}, 16.f);
  add_light(map, {0, 400, 40}, 8.f);
  const shared::entity_uid_t dimmest = add_light(map, {0, 24, 0}, 0.0001f);

  const shared::lightmap_t lightmap = bake_for(map);
  assert(lightmap.light_uids.size() == 5);

  const shared::lightmap_chart_t &chart = *upward_chart(lightmap);

  // Four kept, the fifth gone, and the strongest first. The dimmest one is the
  // one dropped even though it is nearest, because what a slot is ranked by is
  // what the light DELIVERS.
  assert(chart.light_slots[0] == 0);
  for (uint32_t slot = 0; slot < shared::LIGHTMAP_LIGHTS_PER_CHART; ++slot)
  {
    assert(chart.light_slots[slot] != shared::LIGHTMAP_NO_LIGHT_SLOT);
    assert(lightmap.light_uids[(size_t)chart.light_slots[slot]] != dimmest);
  }
  assert(lightmap.light_uids[(size_t)chart.light_slots[0]] == brightest);
}

// A light the author switched to Dynamic is in neither the atlas nor the resolve
// table, so a slot cannot name one -- the table is what the runtime gather pass
// resolves against, and an entry it can never match is a slot that silently
// darkens a face.
void the_resolve_table_holds_only_baked_lights()
{
  shared::map_t map = map_with_a_floor();
  const shared::entity_uid_t baked = add_light(map, {0, 64, 0}, 1.f);

  std::shared_ptr<entities::Point_Light_Entity> dynamic_light =
      std::make_shared<entities::Point_Light_Entity>();
  dynamic_light->position = {0, 64, 0};
  dynamic_light->range = 4096.f;
  dynamic_light->light.intensity = 1.f;
  dynamic_light->light.mode = entities::Light_Mode::Dynamic;
  map.entities.push_back({map.next_uid++, dynamic_light});

  const shared::lightmap_t lightmap = bake_for(map);

  assert(lightmap.light_uids.size() == 1);
  assert(lightmap.light_uids[0] == baked);

  const shared::lightmap_chart_t &chart = *upward_chart(lightmap);
  assert(chart.light_slots[0] == 0);
  assert(chart.light_slots[1] == shared::LIGHTMAP_NO_LIGHT_SLOT);
}

// A face NO ray from the light can reach keeps NO slot: a coverage of zero
// everywhere is a light that is not this face's, and spending one of four on it
// is how the fifth real one gets dropped.
//
// The underside of the slab, not its sides: a side face is one the light does
// not reach and DOES arrive at, since a ray leaving its top edge clears the slab
// entirely -- which is the ungated mask working, and is the case
// a_mask_is_not_gated_on_the_flat_face_normal pins.
void a_face_no_light_reaches_keeps_no_slot()
{
  shared::map_t map = map_with_a_floor();
  add_light(map, {0, 64, 0}, 1.f);

  const shared::lightmap_t lightmap = bake_for(map);

  const shared::lightmap_chart_t *underside = nullptr;
  for (const shared::lightmap_chart_t &chart : lightmap.charts)
    if (chart.plane.normal.y < -0.9f) underside = &chart;
  assert(underside);

  for (uint32_t slot = 0; slot < shared::LIGHTMAP_LIGHTS_PER_CHART; ++slot)
    assert(underside->light_slots[slot] == shared::LIGHTMAP_NO_LIGHT_SLOT);
}

// Coverage is not lighting: it is gated on the shadow ray and the light
// ARRIVING, and on nothing the mode has an opinion about. So the two modes write
// different irradiance and the same visibility, byte for byte.
void the_two_modes_store_the_same_visibility()
{
  shared::map_t map = map_with_a_floor();
  add_light(map, {0, 64, 0}, 1.f);
  map.geometry.push_back(
      {map.next_uid++, shared::make_box_brush({0, 32, 0}, {32, 4, 32})});

  shared::lightmap_solve_settings_t direct;
  shared::lightmap_solve_settings_t visibility;
  visibility.mode = shared::lightmap_solve_mode_t::Visibility;

  const shared::lightmap_t direct_bake = bake_for(map, direct);
  const shared::lightmap_t visibility_bake = bake_for(map, visibility);

  assert(direct_bake.irradiance_pages.bytes != visibility_bake.irradiance_pages.bytes);
  assert(direct_bake.visibility_pages.bytes == visibility_bake.visibility_pages.bytes);
}

// A failed bake must leave no half of an older one: a slot naming a light this
// run never looked at is exactly the disagreement the resolve table exists to
// prevent.
void a_bake_with_no_light_clears_what_the_last_one_left()
{
  shared::map_t map = map_with_a_floor();
  add_light(map, {0, 64, 0}, 1.f);

  shared::lightmap_t lightmap = bake_for(map);
  assert(!lightmap.light_uids.empty());
  assert(upward_chart(lightmap)->light_slots[0] != shared::LIGHTMAP_NO_LIGHT_SLOT);

  // Same charts, same atlas, and every light now Dynamic -- so there is nothing
  // to bake and the whole of the last bake has to go.
  for (shared::map_entity_t &entry : map.entities)
    if (entities::Point_Light_Entity *light =
            entities::entity_as<entities::Point_Light_Entity>(entry.entity.get()))
      light->light.mode = entities::Light_Mode::Dynamic;

  shared::bake_lightmap(map, lightmap, {});

  assert(lightmap.light_uids.empty());
  assert(lightmap.irradiance_pages.empty());
  assert(lightmap.visibility_pages.empty());
  for (const shared::lightmap_chart_t &chart : lightmap.charts)
    for (uint32_t slot = 0; slot < shared::LIGHTMAP_LIGHTS_PER_CHART; ++slot)
      assert(chart.light_slots[slot] == shared::LIGHTMAP_NO_LIGHT_SLOT);
}


// What ss10 decided, and the only test that can tell the decision landed: an
// authored intensity of 1 is the irradiance the light delivers at one metre. A
// true inverse square in inches puts the same number at 1/1550 of that, which
// bakes black at every distance an author would place a lamp.
//
// Not exactly 1: the texel sampled is a couple of units off the axis and the
// windowed falloff is fractionally under an unwindowed one, so the tolerance is
// there to allow a texel's worth of geometry, not a factor.
void intensity_is_the_irradiance_at_the_reference_distance()
{
  const shared::map_t map =
      map_with_a_floor_and_a_light(shared::LIGHT_REFERENCE_DISTANCE, 1.f);
  const linalg::vec3 lit = texel_under_the_light(bake_for(map));

  assert(std::abs(lit.x - 1.f) < 0.1f);
  assert(std::abs(lit.y - 1.f) < 0.1f);
  assert(std::abs(lit.z - 1.f) < 0.1f);
}

// lightmap.hpp promised the gutter would be "dilated into later" and later never
// happened, so it stayed at the zero it was allocated with and bilinear filtering
// pulled black in at every chart edge. Both halves are asserted, and the second is
// the one worth having: the fill is confined to the chart's own rect, so a lit
// face cannot carry light across the seam into the neighbour the packer put
// flush against it.
void the_gutter_is_filled_from_the_chart_that_owns_it()
{
  const shared::map_t map = map_with_a_floor_and_a_light(64.f, 1.f);

  const auto is_all_zero = [](const shared::lightmap_pages_t &pages,
                              const shared::lightmap_chart_t &chart) {
    for (int y = 0; y < chart.atlas_rect.height; ++y)
      for (int x = 0; x < chart.atlas_rect.width; ++x)
      {
        const linalg::vec3 texel = pages.load(chart.page, chart.atlas_rect.min_x + x,
                                              chart.atlas_rect.min_y + y);
        if (texel.x != 0.f || texel.y != 0.f || texel.z != 0.f) return false;
      }
    return true;
  };

  shared::lightmap_solve_settings_t undilated;
  undilated.dilate_into_the_gutter = false;
  const shared::lightmap_t undilated_bake = bake_for(map, undilated);
  const shared::lightmap_pages_t &without = undilated_bake.irradiance_pages;

  const shared::lightmap_t lightmap = bake_for(map);
  const shared::lightmap_pages_t &with = lightmap.irradiance_pages;

  const shared::lightmap_chart_t *lit_chart = nullptr;
  for (const shared::lightmap_chart_t &chart : lightmap.charts)
    if (chart.plane.normal.y > 0.9f) lit_chart = &chart;
  assert(lit_chart);

  // A gutter corner: outside the covered region on both axes, so nothing but the
  // fill can ever have written it.
  const int corner_x = lit_chart->atlas_rect.min_x;
  const int corner_y = lit_chart->atlas_rect.min_y;
  assert(without.load(lit_chart->page, corner_x, corner_y).x == 0.f);
  assert(with.load(lit_chart->page, corner_x, corner_y).x > 0.f);

  for (int y = 0; y < lit_chart->atlas_rect.height; ++y)
    for (int x = 0; x < lit_chart->atlas_rect.width; ++x)
      assert(with.load(lit_chart->page, lit_chart->atlas_rect.min_x + x,
                       lit_chart->atlas_rect.min_y + y)
                 .x > 0.f);

  const std::vector<const shared::lightmap_chart_t *> away =
      charts_facing_away_from_up(lightmap);
  assert(!away.empty());
  for (const shared::lightmap_chart_t *chart : away) assert(is_all_zero(with, *chart));
}

// A texel is an AREA, and one sample at its centre can only answer a shadow edge
// crossing it yes or no -- which is what makes a hard shadow stair-step along the
// texel grid. The visibility mode is where that shows up exactly rather than
// approximately: one sample writes 0 or 1 and nothing else, and NxN writes the
// fraction of the texel that can see the light.
void supersampling_resolves_a_shadow_edge_the_centre_sample_cannot()
{
  shared::map_t map = map_with_a_floor_and_a_light(64.f, 1.f);
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({0, 32, 0}, {30, 4, 30})});

  const auto partial_texel_count = [&](int samples_per_edge) {
    shared::lightmap_solve_settings_t solve;
    solve.mode = shared::lightmap_solve_mode_t::Visibility;
    solve.samples_per_texel_edge = samples_per_edge;
    solve.dilate_into_the_gutter = false;

    const shared::lightmap_t lightmap = bake_for(map, solve);
    const shared::lightmap_pages_t &pages = lightmap.irradiance_pages;

    size_t partial = 0;
    for (int page = 0; page < pages.page_count; ++page)
      for (int y = 0; y < pages.size_in_texels; ++y)
        for (int x = 0; x < pages.size_in_texels; ++x)
        {
          const float value = pages.load(page, x, y).x;
          if (value > 0.01f && value < 0.99f) ++partial;
        }
    return partial;
  };

  assert(partial_texel_count(1) == 0);
  assert(partial_texel_count(4) > 0);
}

// The jitter is derived from the atlas position rather than drawn from a shared
// sequence, and the chart loop runs on as many threads as the machine has. Both
// of those are things a rebake can only be seen to have got wrong by rebaking:
// the same map at the same settings has to produce the same bytes.
void a_rebake_reproduces_itself_byte_for_byte()
{
  const shared::map_t map = map_with_a_floor_and_a_light(64.f, 1.f);

  shared::lightmap_solve_settings_t solve;
  solve.samples_per_texel_edge = 4;

  const shared::lightmap_t first = bake_for(map, solve);
  const shared::lightmap_t second = bake_for(map, solve);

  assert(!first.irradiance_pages.bytes.empty());
  assert(first.irradiance_pages.bytes == second.irradiance_pages.bytes);

  // The visibility half is under the same obligation, and it has one more way to
  // differ: which four lights a chart keeps is a ranking, so a tie broken by
  // whichever thread got there first would be a bake that differs from itself.
  assert(!second.visibility_pages.bytes.empty());
  assert(first.visibility_pages.bytes == second.visibility_pages.bytes);
  for (size_t i = 0; i < first.charts.size(); ++i)
    for (uint32_t slot = 0; slot < shared::LIGHTMAP_LIGHTS_PER_CHART; ++slot)
      assert(first.charts[i].light_slots[slot] == second.charts[i].light_slots[slot]);
}

} // namespace

int main()
{
  a_box_gets_one_chart_per_face();
  chart_size_follows_the_face_extent();
  a_chart_origin_lies_on_its_face_plane();
  a_chart_origin_is_snapped_to_the_texel_grid();
  lightmap_scale_multiplies_the_density();
  a_face_that_emits_no_geometry_gets_no_chart();
  packing_places_every_chart_without_overlap();
  a_chart_too_big_for_a_page_fails_loudly();
  a_chart_polygon_fits_inside_its_own_rect();
  the_chart_size_cap_lowers_density_instead_of_truncating();
  a_vertex_uv_lands_on_its_own_chart();
  a_sidecar_round_trips_every_chart();
  the_geometry_id_follows_the_packing();
  a_chart_never_crosses_to_another_object();
  a_lit_face_writes_uvs_and_a_strange_brush_writes_none();
  rgb9e5_holds_every_scale_a_bake_produces();
  rgb9e5_takes_its_exponent_from_the_brightest_channel();
  the_visibility_mode_writes_white_or_nothing();
  the_direct_mode_falls_off_and_scales_with_intensity();
  the_light_mode_decides_what_reaches_the_atlas();
  an_occluder_darkens_both_modes();
  a_mask_slot_is_named_by_its_light_and_carries_the_shadow();
  a_mask_is_not_gated_on_the_flat_face_normal();
  asking_for_masks_does_not_move_a_pixel();
  intensity_is_the_irradiance_at_the_reference_distance();
  the_gutter_is_filled_from_the_chart_that_owns_it();
  supersampling_resolves_a_shadow_edge_the_centre_sample_cannot();
  a_rebake_reproduces_itself_byte_for_byte();
  a_chart_names_its_lights_and_stores_their_coverage();
  a_chart_keeps_its_strongest_lights_and_drops_the_rest();
  the_resolve_table_holds_only_baked_lights();
  a_face_no_light_reaches_keeps_no_slot();
  the_two_modes_store_the_same_visibility();
  a_bake_with_no_light_clears_what_the_last_one_left();

  std::printf("lightmap_bake_test passed\n");
  return 0;
}
