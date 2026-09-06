#include "../shared/lighting.hpp"
#include "../shared/lightmap_bake.hpp"
#include "../shared/lightmap_gpu.hpp"
#include "../shared/lightmap_lights.hpp"
#include "../shared/lightmap_probes.hpp"
#include "../shared/lightmap_reflections.hpp"
#include "../shared/environment_brdf.hpp"
#include "../shared/lightmap_sidecar.hpp"
#include "../shared/lightmap_solve.hpp"
#include "../shared/lightmap_trace.hpp"

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
        const linalg::vec3 world = shared::sample_texel(chart, texel_x, texel_y).position;
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

  // The indirect pair. `bake_one_box` traces nothing, so what is pinned here is
  // the ABSENT case: four page sets are written whatever the bake produced, and a
  // reader that stopped at the ones it found would take the next field's bytes
  // for pixels.
  assert(loaded.indirect_l0_pages.bytes.empty());
  assert(loaded.indirect_l1_pages.bytes.empty());

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
  assert(lit.lightmap.size() == lit.vertices.size());

  for (const shared::vertex_lightmap_t &vertex : lit.lightmap)
  {
    assert(vertex.uv.x >= 0.f && vertex.uv.x <= 1.f);
    assert(vertex.uv.y >= 0.f && vertex.uv.y <= 1.f);
    assert(vertex.uv.z >= 0.f && vertex.uv.z < (float)lightmap.atlas.page_count);

    // A vertex carries its CHART's slots verbatim, which is what lets the
    // shader tell which light each channel of the visibility texel it names is
    // of. The fixture wrote {2, 0} onto every chart, so a vertex disagreeing
    // with that is one that lost its chart on the way through the mesh.
    assert(vertex.light_slots[0] == 2);
    assert(vertex.light_slots[1] == 0);
    assert(vertex.light_slots[2] == shared::LIGHTMAP_NO_LIGHT_SLOT);
    assert(vertex.light_slots[3] == shared::LIGHTMAP_NO_LIGHT_SLOT);
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
    entities::Light_Mode mode = entities::Light_Mode::Baked, float source_radius = 0.f)
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
  light->light.source_radius = source_radius;
  map.entities.push_back({map.next_uid++, light});

  return map;
}

// The four lights any chart will rank above whatever else the map holds, so the
// light under test is the FIFTH and is DROPPED. That is what makes it the
// RESIDUAL, and the residual is the whole of what the irradiance pages hold
// after lighting_def.md ss14 step 6 -- every light a chart keeps is shaded
// analytically at runtime against its baked visibility, and summing one here as
// well is the ss2 double-count.
//
// Straight overhead, so they reach the top face and no other: a side face is
// perpendicular to them, `reaches` is false there, and every "this chart bakes
// black" assertion below stays true.
void add_the_four_lights_that_outrank_everything(shared::map_t &map)
{
  for (uint32_t index = 0; index < shared::LIGHTMAP_LIGHTS_PER_CHART; ++index)
  {
    std::shared_ptr<entities::Point_Light_Entity> light =
        std::make_shared<entities::Point_Light_Entity>();
    light->position = {0, 400, 0};
    light->range = 4096.f;
    light->light.color = {1.f, 1.f, 1.f};
    light->light.intensity = 5000.f;
    map.entities.push_back({map.next_uid++, light});
  }
}

// The light under test is slot 0 and is ranked last, so the irradiance under it
// is ITS contribution and nothing else's.
shared::map_t map_with_a_floor_and_a_residual_light(float light_height, float intensity,
                                                   float source_radius = 0.f)
{
  shared::map_t map = map_with_a_floor_and_a_light(
      light_height, intensity, entities::Light_Mode::Baked, source_radius);
  add_the_four_lights_that_outrank_everything(map);
  return map;
}

// Defined with the visibility tests below, which is where they belong -- the
// mode and residual assertions above want them too.
const shared::lightmap_chart_t *upward_chart(const shared::lightmap_t &lightmap);
shared::map_t map_with_a_floor();

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
  shared::bake_lightmap(map, lightmap, solve, nullptr, masks);
  return lightmap;
}

// Where the texel directly under the light IS -- the one both modes have the
// most to say about. A place rather than a value, because the two page sets
// answer about the same place and a second walk could disagree about which.
struct atlas_position_t
{
  int page = 0;
  int x = 0;
  int y = 0;
};

atlas_position_t texel_position_under_the_light(const shared::lightmap_t &lightmap)
{
  for (const shared::lightmap_chart_t &chart : lightmap.charts)
  {
    if (chart.plane.normal.y < 0.9f) continue;

    return {chart.page,
            chart.atlas_rect.min_x + lightmap.settings.gutter_in_texels +
                shared::chart_covered_width(chart, lightmap.settings) / 2,
            chart.atlas_rect.min_y + lightmap.settings.gutter_in_texels +
                shared::chart_covered_height(chart, lightmap.settings) / 2};
  }
  assert(false && "the floor has no upward face");
  return {};
}

linalg::vec3 texel_under_the_light(const shared::lightmap_t &lightmap)
{
  const atlas_position_t at = texel_position_under_the_light(lightmap);
  return lightmap.irradiance_pages.load(at.page, at.x, at.y);
}

Array<float, shared::LIGHTMAP_LIGHTS_PER_CHART>
visibility_under_the_light(const shared::lightmap_t &lightmap)
{
  const atlas_position_t at = texel_position_under_the_light(lightmap);
  return lightmap.visibility_pages.load_visibility(at.page, at.x, at.y);
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
//
// Measured on a RESIDUAL light, because that is the only thing the irradiance
// pages hold now. The falloff itself is unchanged -- it is the same sum over the
// same rays -- but which lights enter it is not, and a fixture with one light in
// it would assert that the atlas is empty rather than that the maths is right.
void the_direct_mode_falls_off_and_scales_with_intensity()
{
  const shared::lightmap_solve_settings_t solve;

  const shared::map_t near_map = map_with_a_floor_and_a_residual_light(64.f, 1.f);
  const linalg::vec3 near_texel = texel_under_the_light(bake_for(near_map, solve));

  const shared::map_t far_map = map_with_a_floor_and_a_residual_light(128.f, 1.f);
  const linalg::vec3 far_texel = texel_under_the_light(bake_for(far_map, solve));

  const shared::map_t bright_map = map_with_a_floor_and_a_residual_light(64.f, 4.f);
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

// lighting_def.md ss2's correctness requirement, and what step 6 did to it: the
// mode still decides WHERE a light is evaluated, but the answer for a Baked one
// moved. Every light a chart keeps -- Baked and Mixed alike -- is now shaded
// ANALYTICALLY against its baked visibility, so the atlas holds no irradiance
// for either and the whole of what it contributes is the shadow.
//
// Which leaves the bake unable to tell Baked from Mixed at all, and correctly
// so: the difference between them is a GATHER decision now, and it is pinned by
// a_mixed_light_is_in_the_array_twice_and_a_baked_one_once below.
void the_light_mode_decides_where_a_light_is_evaluated()
{
  const shared::lightmap_solve_settings_t solve;

  const auto bake_with = [&](entities::Light_Mode mode,
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

    return bake_for(map, solve);
  };

  // Both halves have to be asserted and they say opposite things: an irradiance
  // of zero is what stops mesh_lit.frag counting the light twice, and a coverage
  // of 1 is what it shades the surface WITH. Either alone passes while the light
  // is missing from the bake entirely.
  for (entities::Light_Mode mode : {entities::Light_Mode::Baked, entities::Light_Mode::Mixed})
  {
    const shared::lightmap_t lightmap = bake_with(mode, {});
    assert(lightmap.light_uids.size() == 1);
    assert(texel_under_the_light(lightmap).x == 0.f);
    assert(visibility_under_the_light(lightmap)[0] == 1.f);
  }

  // A Dynamic light standing in the same spot reaches NEITHER page set and is
  // not in the resolve table -- so no chart spends a slot on it, and the runtime
  // shades it with no baked shadow at all.
  const shared::lightmap_t with_dynamic =
      bake_with(entities::Light_Mode::Baked, entities::Light_Mode::Dynamic);
  assert(with_dynamic.light_uids.size() == 1);
  assert(upward_chart(with_dynamic)->light_slots[1] == shared::LIGHTMAP_NO_LIGHT_SLOT);

  // Where a second BAKED light does land, which is what proves the assertions
  // above are measuring the mode rather than a solve that ignores second lights.
  const shared::lightmap_t with_baked =
      bake_with(entities::Light_Mode::Baked, entities::Light_Mode::Baked);
  assert(with_baked.light_uids.size() == 2);
  assert(upward_chart(with_baked)->light_slots[1] != shared::LIGHTMAP_NO_LIGHT_SLOT);
  assert(visibility_under_the_light(with_baked)[1] == 1.f);
}

// The other end of the N+1 cliff, and the reason step 6 could retire the flat
// arm without losing a light: the fifth light on a face is DROPPED from the
// chart's slots and lands in the irradiance instead. Before the residual fold it
// was dropped and gone, which on a face with five lights was darker than the
// flat bake it replaced.
void a_light_a_chart_drops_becomes_the_residual()
{
  const shared::map_t map = map_with_a_floor_and_a_residual_light(64.f, 1.f);
  const shared::lightmap_t lightmap = bake_for(map);

  assert(lightmap.light_uids.size() == shared::LIGHTMAP_LIGHTS_PER_CHART + 1);

  // Slot 0 is the light under test and it is the one NOT kept, because what a
  // slot is ranked by is what the light delivers.
  const shared::lightmap_chart_t &chart = *upward_chart(lightmap);
  for (uint32_t slot = 0; slot < shared::LIGHTMAP_LIGHTS_PER_CHART; ++slot)
    assert(chart.light_slots[slot] != 0);

  // ...and its contribution is in the atlas rather than lost.
  assert(texel_under_the_light(lightmap).x > 0.f);

  // The four it KEPT contribute none of it, which is the half that makes the
  // assertion above mean "the residual" rather than "some light got summed":
  // take the fifth away and the irradiance is empty.
  shared::map_t without_the_residual = map_with_a_floor();
  add_the_four_lights_that_outrank_everything(without_the_residual);
  const shared::lightmap_t kept_only = bake_for(without_the_residual);

  assert(kept_only.light_uids.size() == shared::LIGHTMAP_LIGHTS_PER_CHART);
  assert(texel_under_the_light(kept_only).x == 0.f);
  assert(visibility_under_the_light(kept_only)[0] == 1.f);
}

// A texel with a lid over it is dark in BOTH modes: the shadow ray is a gate
// they share, which is the whole reason the binary solve is worth keeping as a
// debug view of the real one.
//
// Asserted on the VISIBILITY, which is what carries a kept light's shadow now --
// on the irradiance the same assertion would pass against an atlas that holds
// nothing at all. The residual half is asserted underneath it, on a light the
// chart drops, so both things the gate feeds are covered.
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

    const shared::lightmap_t lightmap = bake_for(map, solve);
    assert(visibility_under_the_light(lightmap)[0] == 0.f);

    // The binary mode's picture of the same rays, which is the one page set that
    // still says something about a kept light.
    if (mode == shared::lightmap_solve_mode_t::Visibility)
      assert(texel_under_the_light(lightmap).x == 0.f);
  }

  shared::map_t residual_map = map_with_a_floor_and_a_residual_light(64.f, 1.f);
  residual_map.geometry.push_back(
      {residual_map.next_uid++, shared::make_box_brush({0, 32, 0}, {32, 4, 32})});
  assert(texel_under_the_light(bake_for(residual_map)).x == 0.f);
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
      const shared::texel_sample_t sample = shared::sample_texel(chart, x, y);
      if (!sample.on_surface) continue;

      const linalg::vec3 position = sample.position;
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

  // Four brighter ones overhead, so the light under test is slot 0 and is the
  // one the chart DROPS -- which is what keeps the irradiance assertion below
  // measuring the `reaches` gate. A light the chart kept would bake zero
  // irradiance whatever its angle, and the test would pass for the wrong reason.
  add_the_four_lights_that_outrank_everything(map);

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

// --- Area lights -------------------------------------------------------------

// Every mask coverage the top face carries, so the assertions below can be about
// the SHAPE of a shadow edge rather than about one texel somebody picked.
std::vector<float> top_face_coverage(const shared::lightmap_t &lightmap,
                                     const shared::lightmap_visibility_masks_t &masks)
{
  const shared::lightmap_chart_t &chart = *upward_chart(lightmap);
  const int gutter = lightmap.settings.gutter_in_texels;

  std::vector<float> coverage;
  for (int y = 0; y < shared::chart_covered_height(chart, lightmap.settings); ++y)
    for (int x = 0; x < shared::chart_covered_width(chart, lightmap.settings); ++x)
    {
      if (!shared::sample_texel(chart, x, y).on_surface) continue;
      coverage.push_back(masks.coverage[masks.index_of(0, chart.page, chart.atlas_rect.min_x + gutter + x,
                                                       chart.atlas_rect.min_y + gutter + y)]);
    }

  assert(!coverage.empty());
  return coverage;
}

// The whole of what a source radius buys the bake: a shadow stops being a set of
// texels that either see the light or do not.
//
// ONE sample per texel edge, deliberately. The default of 2 already averages four
// samples of a texel's footprint, so a texel straddling a HARD edge is fractional
// too -- and a test that cannot tell 6.3's supersampling from an area light's
// penumbra would pass with the radius doing nothing at all.
void a_source_radius_softens_a_shadow_edge()
{
  shared::lightmap_solve_settings_t solve;
  solve.samples_per_texel_edge = 1;

  const auto coverage_with_radius = [&](float radius) {
    shared::map_t map = map_with_a_floor_and_a_light(
        64.f, 1.f, entities::Light_Mode::Baked, radius);
    map.geometry.push_back({map.next_uid++, shared::make_box_brush({0, 32, 0}, {32, 4, 32})});

    shared::lightmap_visibility_masks_t masks;
    const shared::lightmap_t lightmap = bake_for(map, solve, &masks);
    return top_face_coverage(lightmap, masks);
  };

  int punctual_partial = 0;
  for (float coverage : coverage_with_radius(0.f))
  {
    assert(coverage == 0.f || coverage == 1.f);
    if (coverage > 0.f && coverage < 1.f) ++punctual_partial;
  }
  assert(punctual_partial == 0);

  // A radius comparable to the lid's own half-extent, so the penumbra is wider
  // than the texel grid and cannot be missed between two samples of it.
  int soft_partial = 0;
  int lit = 0;
  int dark = 0;
  for (float coverage : coverage_with_radius(24.f))
  {
    if (coverage >= 1.f) ++lit;
    else if (coverage <= 0.f) ++dark;
    else ++soft_partial;
  }

  assert(soft_partial > 0);

  // And it is a penumbra rather than a general blur: the umbra under the lid is
  // still fully occluded and the floor past the shadow is still fully lit.
  assert(dark > 0);
  assert(lit > 0);
}

// The cost of area lights lands ONLY where an author asked for softness. A
// punctual light takes its one centre ray whatever the ray budget says, which is
// what makes every map authored before this bake exactly what it did before.
void a_punctual_light_spends_no_extra_rays()
{
  shared::map_t map = map_with_a_floor_and_a_light(64.f, 1.f);
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({0, 32, 0}, {32, 4, 32})});

  shared::lightmap_solve_settings_t one;
  one.soft_shadow_samples = 1;

  shared::lightmap_solve_settings_t many;
  many.soft_shadow_samples = 32;

  const shared::lightmap_t cheap = bake_for(map, one);
  const shared::lightmap_t expensive = bake_for(map, many);

  assert(!cheap.irradiance_pages.bytes.empty());
  assert(cheap.irradiance_pages.bytes == expensive.irradiance_pages.bytes);
  assert(cheap.visibility_pages.bytes == expensive.visibility_pages.bytes);
}

// Inside a chain, next-event estimation spends ONE ray toward a random point on
// the emitter's disc, not the texel's spiral (lightmap_gpu_plan.md step 0). The
// two are estimators of the same fraction, so at a point in the penumbra the
// single ray must AVERAGE to what the spiral answers -- and a punctual light must
// take the same centre ray under both, since that is what keeps every map lit by
// point sources bit for bit what it was.
void one_shadow_ray_toward_the_disc_averages_to_the_spiral()
{
  shared::map_t map =
      map_with_a_floor_and_a_light(64.f, 1.f, entities::Light_Mode::Baked, 24.f);
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({0, 32, 0}, {32, 4, 32})});

  const std::vector<shared::baked_light_t> lights = shared::collect_lights(map);
  assert(lights.size() == 1);
  const Bounding_Volume_Hierarchy bvh = shared::build_occluder_bvh(map);

  // A lid edge at (32, 36) seen from a disc spanning x in [-24, 24] at y = 64
  // throws its penumbra over x in about [42, 104] on the floor; 70 is inside it.
  const linalg::vec3 position{70.f, 0.f, 0.f};
  const linalg::vec3 normal{0.f, 1.f, 0.f};
  const shared::light_arrival_t arrival =
      shared::arrival_at(lights[0].light, position, normal, 100000.f);
  assert(arrival.arrives && arrival.shadow_disc_radius == 24.f);

  const float spiral =
      shared::light_visibility(bvh, position, normal, arrival, 0.25f, 256, 0x2545f491u);
  assert(spiral > 0.05f && spiral < 0.95f);

  constexpr int SINGLE_RAY_TRIALS = 4096;
  double reached = 0.0;
  for (int trial = 0; trial < SINGLE_RAY_TRIALS; ++trial)
  {
    const float one = shared::light_visibility_single_ray(
        bvh, position, normal, arrival, 0.25f, shared::hash_mix(0x9e3779b9u, (uint32_t)trial));
    assert(one == 0.f || one == 1.f);
    reached += one;
  }
  const float averaged = (float)(reached / SINGLE_RAY_TRIALS);
  assert(std::abs(averaged - spiral) < 0.05f);

  // The punctual case: the same one ray, the same answer, whichever estimator.
  shared::light_arrival_t punctual = arrival;
  punctual.shadow_disc_radius = 0.f;
  for (uint32_t hash = 0; hash < 16; ++hash)
    assert(shared::light_visibility_single_ray(bvh, position, normal, punctual, 0.25f, hash) ==
           shared::light_visibility(bvh, position, normal, punctual, 0.25f, 32, hash));
}

// The other half of a radius, and the one with no shadow in it: an emitter with
// SIZE has no singularity at its centre, so the falloff divides by the radius
// once the surface is nearer than that. Without the clamp a light a few units off
// a wall bakes an unbounded hotspot, which is a point source's fiction.
//
// A residual light, because the residual is the only thing the atlas still stores
// irradiance for -- and weak enough that the four overhead outrank it, or it would
// be shaded analytically and bake nothing at all.
void a_source_radius_clamps_the_near_field()
{
  const shared::lightmap_solve_settings_t solve;

  const shared::map_t punctual = map_with_a_floor_and_a_residual_light(4.f, 0.25f);
  const shared::map_t sized = map_with_a_floor_and_a_residual_light(4.f, 0.25f, 32.f);

  const linalg::vec3 punctual_texel = texel_under_the_light(bake_for(punctual, solve));
  const linalg::vec3 sized_texel = texel_under_the_light(bake_for(sized, solve));

  assert(punctual_texel.x > 0.f);
  assert(sized_texel.x > 0.f);
  assert(sized_texel.x < punctual_texel.x);
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

// --- The gather: how a frame's light array is laid out -----------------------

shared::frame_lights_t gather_for(const shared::map_t &map,
                                  const shared::lightmap_t &lightmap)
{
  shared::frame_lights_t frame;
  shared::begin_frame_lights(frame, lightmap);
  for (const shared::map_entity_t &entry : map.entities)
    if (entry.entity) shared::add_frame_light(frame, lightmap, entry.uid, *entry.entity);
  return frame;
}

// The counterpart of the drop policy, at the far end. A chart's four slots ARE
// the runtime's light culling (lighting_def.md ss14 step 6), so the head of the
// frame's array has to be indexed BY SLOT -- a shader that resolves a stored
// channel through a differently ordered array lights the face with the wrong
// light entirely, and every one of them is a plausible light.
void the_gather_indexes_baked_lights_by_their_slot()
{
  shared::map_t map = map_with_a_floor();
  const shared::entity_uid_t overhead = add_light(map, {0, 64, 0}, 1.f);
  add_light(map, {200, 64, 0}, 1.f);

  const shared::lightmap_t lightmap = bake_for(map);
  assert(lightmap.light_uids.size() == 2);

  const shared::frame_lights_t frame = gather_for(map, lightmap);

  assert(frame.baked_count == 2);
  for (uint32_t slot = 0; slot < frame.baked_count; ++slot)
  {
    assert(frame.entries[slot].baked_slot == (int16_t)slot);

    // The entry at a slot is the light the resolve table names there, which is
    // the whole content of "indexed by slot".
    const float expected_x = lightmap.light_uids[slot] == overhead ? 0.f : 200.f;
    assert(frame.entries[slot].position.x == expected_x);
  }

  // Both are Baked, so there is no analytic tail: a Baked light lights what the
  // bake measured and nothing else, exactly as it did before it became analytic.
  assert(frame.entries.size() == frame.baked_count);
}

// The one place Baked and Mixed differ now, and it is a placement rather than a
// term. A Mixed light is in the array TWICE -- at its slot, where a lightmapped
// surface reads it with its baked shadow, and in the tail, where a surface with
// no chart reads it with none. One copy each way would cost the shadow or cost
// the player standing in front of the wall.
void a_mixed_light_is_in_the_array_twice_and_a_baked_one_once()
{
  const auto tail_of = [](const shared::frame_lights_t &frame) {
    return frame.entries.size() - frame.baked_count;
  };

  {
    const shared::map_t map = map_with_a_floor_and_a_light(64.f, 1.f,
                                                           entities::Light_Mode::Baked);
    const shared::lightmap_t lightmap = bake_for(map);
    const shared::frame_lights_t frame = gather_for(map, lightmap);

    assert(frame.baked_count == 1);
    assert(tail_of(frame) == 0);
  }

  {
    const shared::map_t map = map_with_a_floor_and_a_light(64.f, 1.f,
                                                           entities::Light_Mode::Mixed);
    const shared::lightmap_t lightmap = bake_for(map);
    const shared::frame_lights_t frame = gather_for(map, lightmap);

    assert(frame.baked_count == 1);
    assert(tail_of(frame) == 1);

    // The tail copy carries the SLOT, which is what a lightmapped surface skips
    // it by -- it already shaded this light through its chart, with the shadow.
    assert(frame.entries[frame.baked_count].baked_slot == 0);
  }

  {
    const shared::map_t map = map_with_a_floor_and_a_light(64.f, 1.f,
                                                           entities::Light_Mode::Dynamic);
    shared::lightmap_t lightmap = pack_for(map);
    shared::bake_lightmap(map, lightmap, {});
    const shared::frame_lights_t frame = gather_for(map, lightmap);

    // The bake never saw it, so it has no slot and the whole array is the tail.
    assert(frame.baked_count == 0);
    assert(tail_of(frame) == 1);
    assert(frame.entries[0].baked_slot == shared::LIGHTMAP_NO_LIGHT_SLOT);
  }
}

// A light deleted since the bake leaves a slot every chart still names. It has to
// resolve to something that contributes NOTHING, and radiance is the field that
// guarantees it -- the default scene_light_t is white, which would put a full
// unshadowed light wherever the deleted one used to be.
void a_slot_no_live_light_claims_carries_no_radiance()
{
  shared::map_t map = map_with_a_floor();
  add_light(map, {0, 64, 0}, 1.f);
  add_light(map, {200, 64, 0}, 1.f);

  const shared::lightmap_t lightmap = bake_for(map);
  assert(lightmap.light_uids.size() == 2);

  map.entities.pop_back();
  const shared::frame_lights_t frame = gather_for(map, lightmap);

  assert(frame.baked_count == 2);
  assert(frame.entries.size() == 2);

  size_t empty_slots = 0;
  for (const shared::scene_light_t &light : frame.entries)
    if (light.radiance.x == 0.f && light.radiance.y == 0.f && light.radiance.z == 0.f)
      ++empty_slots;
  assert(empty_slots == 1);
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
      map_with_a_floor_and_a_residual_light(shared::LIGHT_REFERENCE_DISTANCE, 1.f);
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
  // A RESIDUAL light, because the irradiance pages are what this measures and a
  // light a chart keeps writes none. The four that outrank it stand straight
  // overhead, so the faces that must stay black still do.
  const shared::map_t map = map_with_a_floor_and_a_residual_light(64.f, 1.f);

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

// --- Indirect light: the path tracer (lighting_def.md gate 2) ----------------

// Two parallel plates with the light between them: the floor's top face is
// directly lit AND sees a lit ceiling, which is the smallest scene in which a
// bounce has somewhere to come from.
shared::map_t map_with_a_floor_a_ceiling_and_a_light_between_them()
{
  shared::map_t map;
  map.geometry.push_back(
      {map.next_uid++, shared::make_box_brush({0, -8, 0}, {64, 8, 64})});
  map.geometry.push_back(
      {map.next_uid++, shared::make_box_brush({0, 72, 0}, {64, 8, 64})});

  std::shared_ptr<entities::Point_Light_Entity> light =
      std::make_shared<entities::Point_Light_Entity>();
  light->position = {0, 32, 0};
  light->range = 1024.f;
  light->light.color = {1.f, 1.f, 1.f};
  light->light.intensity = 400.f;
  map.entities.push_back({map.next_uid++, light});

  return map;
}

shared::lightmap_solve_settings_t traced_solve_settings()
{
  shared::lightmap_solve_settings_t solve;
  solve.samples_per_texel_edge = 1;
  solve.trace_indirect_light = true;
  solve.indirect_rays_per_sample = 16;
  return solve;
}

// The LOWEST upward-facing chart, which is the floor -- a ceiling has an upward
// face too, and it is the one nothing lights.
atlas_position_t texel_at_the_middle_of_the_floor(const shared::lightmap_t &lightmap)
{
  const shared::lightmap_chart_t *floor = nullptr;
  for (const shared::lightmap_chart_t &chart : lightmap.charts)
  {
    if (chart.plane.normal.y < 0.9f) continue;
    if (!floor || chart.origin.y < floor->origin.y) floor = &chart;
  }
  assert(floor && "the scene has no upward face");

  return {floor->page,
          floor->atlas_rect.min_x + lightmap.settings.gutter_in_texels +
              shared::chart_covered_width(*floor, lightmap.settings) / 2,
          floor->atlas_rect.min_y + lightmap.settings.gutter_in_texels +
              shared::chart_covered_height(*floor, lightmap.settings) / 2};
}

shared::lightmap_t bake_indirect_for(const shared::map_t &map,
                                     const shared::lightmap_solve_settings_t &solve)
{
  shared::lightmap_t lightmap = pack_for(map);
  shared::bake_lightmap(map, lightmap, solve);
  return lightmap;
}

// The reconstruction the shader will run, and the ONE place this file spells it:
// E(N) = 0.886227 * L0 + 1.023328 * dot(L1, N), clamped at zero. A test that
// re-derived it per assertion would be a second lighting model in the test file.
linalg::vec3 reconstructed_irradiance(const shared::lightmap_t &lightmap, int page, int x,
                                      int y, const linalg::vec3 &normal)
{
  const linalg::vec3 l0 = lightmap.indirect_l0_pages.load(page, x, y);
  const Array<linalg::vec3, shared::SH_L1_LAYERS_PER_PAGE> l1 =
      lightmap.indirect_l1_pages.load_l1(page, x, y, l0);

  linalg::vec3 irradiance = l0 * shared::SH_L1_IRRADIANCE_L0;
  irradiance = irradiance + l1[0] * (shared::SH_L1_IRRADIANCE_L1 * normal.x);
  irradiance = irradiance + l1[1] * (shared::SH_L1_IRRADIANCE_L1 * normal.y);
  irradiance = irradiance + l1[2] * (shared::SH_L1_IRRADIANCE_L1 * normal.z);

  return {std::max(irradiance.x, 0.f), std::max(irradiance.y, 0.f),
          std::max(irradiance.z, 0.f)};
}

// Albedo textures upload SRGB, so their BYTES are encoded and reflectance
// arithmetic is linear. Sampling them raw is the trap gate 2 names: a 0.5 grey
// wall would reflect 0.73 and every bounce would be systematically too bright.
void the_srgb_decode_is_the_one_albedo_reads_through()
{
  assert(shared::srgb_byte_to_linear(0) == 0.f);
  assert(std::abs(shared::srgb_byte_to_linear(255) - 1.f) < 1e-5f);

  // Mid-grey, and the whole point: 128/255 is 0.502 encoded and 0.216 linear.
  const float mid = shared::srgb_byte_to_linear(128);
  assert(mid > 0.20f && mid < 0.23f);
}

// A tracer that invents light is worse than one that finds none, so the first
// thing to pin is the zero: a floor under an open sky has nothing above it for a
// chain to land on, and every chain escapes on its first ray.
void nothing_bounces_where_there_is_nothing_to_bounce_off()
{
  const shared::map_t map = map_with_a_floor_and_a_light(64.f, 400.f);

  const shared::lightmap_t lightmap = bake_indirect_for(map, traced_solve_settings());

  assert(!lightmap.indirect_l0_pages.empty());
  assert(lightmap.indirect_l0_pages.size_in_texels == lightmap.atlas.size_in_texels);

  // Three layers an atlas page, one per world axis, and asserting it here is what
  // stops a page count that does not divide by three from making every load_l1
  // read someone else's texel.
  assert(lightmap.indirect_l1_pages.page_count ==
         lightmap.atlas.page_count * shared::SH_L1_LAYERS_PER_PAGE);

  // The scene IS lit -- the light is slot 0 of this chart, so what says so is the
  // visibility channel and not the irradiance, which after ss14 step 6 holds only
  // what a chart dropped. A zero below is the bounce being absent, not the bake.
  const atlas_position_t at = texel_at_the_middle_of_the_floor(lightmap);
  assert(lightmap.visibility_pages.load_visibility(at.page, at.x, at.y)[0] > 0.f);

  for (int page = 0; page < lightmap.indirect_l0_pages.page_count; ++page)
    for (int y = 0; y < lightmap.indirect_l0_pages.size_in_texels; ++y)
      for (int x = 0; x < lightmap.indirect_l0_pages.size_in_texels; ++x)
        assert(lightmap.indirect_l0_pages.load(page, x, y).x == 0.f);
}

// --- Emissive surfaces: gate 4 ----------------------------------------------

// A 1x1 texture of one colour. The pool is not involved: traced_scene_t holds
// POINTERS per material, and a scene whose map.materials is empty never asks the
// asset state anything.
assets::texture_asset_t one_texel(uint8_t r, uint8_t g, uint8_t b)
{
  assets::texture_asset_t texture;
  texture.width = 1;
  texture.height = 1;
  texture.channels = 4;
  texture.pixels = {r, g, b, 255};
  return texture;
}

// The floor and the ceiling of the bounce scene, with the light taken OUT --
// which is the whole of gate 4: the only thing in this room that can deliver
// light is a surface, and a chain finds it by landing on it.
shared::map_t map_with_a_floor_and_a_ceiling_and_no_light()
{
  shared::map_t map;
  map.geometry.push_back(
      {map.next_uid++, shared::make_box_brush({0, -8, 0}, {64, 8, 64})});
  map.geometry.push_back(
      {map.next_uid++, shared::make_box_brush({0, 72, 0}, {64, 8, 64})});
  return map;
}

// A material EMITS exactly what its emissive.png holds, sRGB-decoded like the
// authored colour it is -- no strength, no tint, no albedo in it. The absence of
// that map is the only thing that says a material does not glow.
void emission_is_the_emissive_map_and_nothing_else()
{
  const shared::map_t map = map_with_a_floor_and_a_ceiling_and_no_light();
  const assets::texture_asset_t albedo = one_texel(255, 255, 255);
  const assets::texture_asset_t emissive = one_texel(255, 128, 0);

  const Bounding_Volume_Hierarchy bvh = shared::build_occluder_bvh(map);
  shared::traced_scene_t scene = shared::build_traced_scene(map, bvh);

  ray_hit_result_t hit = {};
  assert(bvh_intersect_ray(bvh, {0, 40, 0}, {0, -1, 0}, hit) && hit.hit);
  const linalg::vec3 at = {0, 40 - hit.t, 0};

  scene.materials = {{&albedo, &emissive}};
  const shared::traced_surface_t glowing = shared::surface_at(scene, hit, at);

  assert(std::abs(glowing.albedo.x - 1.f) < 1e-5f);
  assert(glowing.emission.x == shared::srgb_byte_to_linear(255));
  assert(glowing.emission.y == shared::srgb_byte_to_linear(128));
  assert(glowing.emission.z == shared::srgb_byte_to_linear(0));

  // No emissive.png, and that is the whole test for it.
  scene.materials = {{&albedo, nullptr}};
  const shared::traced_surface_t dark = shared::surface_at(scene, hit, at);
  assert(dark.emission.x == 0.f && dark.emission.y == 0.f && dark.emission.z == 0.f);
  assert(dark.albedo.x == glowing.albedo.x);
}

// Gate 4, and the assertion is that there is NO LIGHT IN THE SCENE. Every other
// term in the tracer is gated on a baked_light_t; this one is gated on nothing
// but a chain landing somewhere bright, which is why it costs one line.
void an_emissive_surface_lights_a_room_with_no_lights_in_it()
{
  const shared::map_t map = map_with_a_floor_and_a_ceiling_and_no_light();
  const assets::texture_asset_t albedo = one_texel(255, 255, 255);

  // Red at full, green partway, blue off -- three channels that cannot be told
  // apart by a scene of grey walls and a grey light, which is what every other
  // bounce test here is made of.
  const assets::texture_asset_t emissive = one_texel(255, 128, 0);

  const Bounding_Volume_Hierarchy bvh = shared::build_occluder_bvh(map);
  shared::traced_scene_t scene = shared::build_traced_scene(map, bvh);

  shared::indirect_trace_settings_t settings;
  settings.rays_per_sample = 256;

  const auto bounce_on_the_floor = [&](const assets::texture_asset_t *emissive_map) {
    scene.materials = {{&albedo, emissive_map}};
    return shared::trace_indirect_light(scene, {}, {0, 0, 0}, {0, 1, 0}, settings,
                                        0x51ed270bu);
  };

  // The same scene, the same rays and the same hash -- the ONLY thing that
  // differs is whether the ceiling has an emissive map. Nothing else in this
  // file can move this pair.
  assert(bounce_on_the_floor(nullptr).l0.x == 0.f);

  const shared::indirect_sh_l1_t lit = bounce_on_the_floor(&emissive);
  assert(lit.l0.x > 0.f);

  // Up, because the ceiling is the only emitter and it is above.
  assert(lit.l1[1].y > 0.f);

  // Emission never joins the throughput -- a surface does not reflect its own
  // glow -- and the albedo above is white, so the channels stay in exactly the
  // ratio the emissive texel holds. A term that had picked up the albedo, the
  // base colour or a roulette weight per channel could not.
  const float ratio = shared::srgb_byte_to_linear(255) / shared::srgb_byte_to_linear(128);
  assert(std::abs(lit.l0.x / lit.l0.y - ratio) < 1e-3f);
  assert(lit.l0.z == 0.f);
}

// The whole of gate 2 in one assertion: a surface the light already reaches gets
// MORE than the light delivers, because the room around it is lit too.
void a_ceiling_bounces_light_back_onto_the_floor()
{
  const shared::map_t map = map_with_a_floor_a_ceiling_and_a_light_between_them();

  const shared::lightmap_t lightmap = bake_indirect_for(map, traced_solve_settings());

  const atlas_position_t at = texel_at_the_middle_of_the_floor(lightmap);
  const linalg::vec3 bounced = lightmap.indirect_l0_pages.load(at.page, at.x, at.y);

  assert(bounced.x > 0.f);

  // Grey walls and a grey light, so a colour cast would be a bug in the
  // per-channel throughput rather than a property of the scene.
  assert(std::abs(bounced.x - bounced.y) < 1e-4f);
  assert(std::abs(bounced.y - bounced.z) < 1e-4f);

  // The light is the chart's slot 0, so it is shaded analytically at runtime and
  // the irradiance pages hold nothing here -- the bounce is the only thing in the
  // scene that was not already accounted for.
  assert(lightmap.irradiance_pages.load(at.page, at.x, at.y).x == 0.f);
}

// The whole of what SH L1 buys over the flat accumulator: the texel knows WHICH
// WAY the bounce came from. Everything above this floor texel is the lit ceiling,
// so the L1 vector leans +Y -- and the reconstruction has to be BRIGHTER facing
// the ceiling than facing away from it, which a flat store cannot express.
//
// A flat encoding passes every assertion in the test above and fails this one,
// which is why it is a test of its own rather than another line up there.
// The layout that was reported as "a Baked spot light does nothing": a spot
// standing on a floor slab, aimed at a wall well inside its range and its cone,
// with the wall's face towards it. The wall's chart must KEEP the light -- a
// spot is not a point light with a cone drawn on it, its cone gate and its
// forward vector are gates a point light never runs -- and the reach probe must
// report the same face as reached and visible, since it is what the editor
// shows an author who asks why a face is dark.
void a_spot_light_on_the_floor_lights_the_wall_it_is_aimed_at()
{
  shared::map_t map;
  const shared::entity_uid_t floor_uid = map.next_uid++;
  map.geometry.push_back({floor_uid, shared::make_box_brush({0, -8, 0}, {512, 8, 512})});
  const shared::entity_uid_t wall_uid = map.next_uid++;
  map.geometry.push_back({wall_uid, shared::make_box_brush({300, 256, 0}, {16, 256, 512})});

  // Identity orientation is +X forward (linalg::basis_from), which is what the
  // editor's cone gizmo draws along, so this aims straight at the wall's -X face
  // 284 units away.
  std::shared_ptr<entities::Spot_Light_Entity> spot =
      std::make_shared<entities::Spot_Light_Entity>();
  spot->position = {0, 24, 0};
  spot->range = 512.f;
  spot->inner_degrees = 20.f;
  spot->outer_degrees = 35.f;
  spot->light.color = {1.f, 1.f, 1.f};
  spot->light.intensity = 64.f;
  spot->light.mode = entities::Light_Mode::Baked;
  const shared::entity_uid_t spot_uid = map.next_uid++;
  map.entities.push_back({spot_uid, spot});

  const shared::lightmap_t lightmap = bake_for(map);
  const int16_t slot = shared::find_baked_light_slot(lightmap, spot_uid);
  assert(slot != shared::LIGHTMAP_NO_LIGHT_SLOT);

  bool wall_face_keeps_it = false;
  for (const shared::lightmap_chart_t &chart : lightmap.charts)
  {
    if (chart.object_uid != wall_uid || chart.plane.normal.x > -0.9f) continue;
    for (int16_t kept : chart.light_slots) wall_face_keeps_it |= kept == slot;
  }
  assert(wall_face_keeps_it);
  assert(shared::count_charts_keeping_light(lightmap, slot) >= 1);

  const std::vector<shared::baked_light_t> lights = shared::collect_lights(map);
  assert(lights.size() == 1);
  const std::vector<shared::light_reach_on_face_t> reach =
      shared::probe_light_reach(map, lights[0], lightmap.settings, 0.25f, 100000.f, 16);

  bool wall_face_reported = false;
  for (const shared::light_reach_on_face_t &face : reach)
  {
    if (face.object_uid != wall_uid || face.normal.x > -0.9f) continue;
    wall_face_reported = true;
    assert(face.sampled > 0);
    assert(face.arrives > 0);
    assert(face.reaches > 0);
    assert(face.visible > 0);
    assert(face.nearest_distance < 512.f);
    assert(face.best_cone_cos > std::cos(linalg::to_radians(35.f)));
  }
  assert(wall_face_reported);

  // The wall's FAR face is aimed at too, and must not be lit: it faces away, so
  // `reaches` is zero there whatever `arrives` says.
  for (const shared::light_reach_on_face_t &face : reach)
    if (face.object_uid == wall_uid && face.normal.x > 0.9f) assert(face.reaches == 0);
}

void the_bounce_knows_which_way_it_came_from()
{
  const shared::map_t map = map_with_a_floor_a_ceiling_and_a_light_between_them();

  const shared::lightmap_t lightmap = bake_indirect_for(map, traced_solve_settings());
  const atlas_position_t at = texel_at_the_middle_of_the_floor(lightmap);

  const linalg::vec3 l0 = lightmap.indirect_l0_pages.load(at.page, at.x, at.y);
  const Array<linalg::vec3, shared::SH_L1_LAYERS_PER_PAGE> l1 =
      lightmap.indirect_l1_pages.load_l1(at.page, at.x, at.y, l0);

  // l1[1] is the Y component of the L1 vector, for each of r, g and b.
  assert(l1[1].x > 0.f && l1[1].y > 0.f && l1[1].z > 0.f);

  // And it dominates: the ceiling is straight up, so what is left sideways is a
  // symmetric room's sampling noise.
  assert(l1[1].x > std::abs(l1[0].x) * 2.f);
  assert(l1[1].x > std::abs(l1[2].x) * 2.f);

  const linalg::vec3 facing_up =
      reconstructed_irradiance(lightmap, at.page, at.x, at.y, {0.f, 1.f, 0.f});
  const linalg::vec3 facing_down =
      reconstructed_irradiance(lightmap, at.page, at.x, at.y, {0.f, -1.f, 0.f});

  assert(facing_up.x > 0.f);
  assert(facing_up.x > facing_down.x * 2.f);
}

// The bias encoding normalizes by sqrt(3) * L0, and that constant is DERIVED: for
// light from a single direction |L1| / L0 is exactly Y1 / Y0, so a legal bake
// cannot clip. A value pinned at the top or bottom of the byte range is what
// clipping looks like, and it would be invisible in the picture.
void the_l1_encoding_does_not_clip_on_a_legal_bake()
{
  const shared::map_t map = map_with_a_floor_a_ceiling_and_a_light_between_them();
  const shared::lightmap_t lightmap = bake_indirect_for(map, traced_solve_settings());

  const atlas_position_t at = texel_at_the_middle_of_the_floor(lightmap);
  const linalg::vec3 l0 = lightmap.indirect_l0_pages.load(at.page, at.x, at.y);
  const Array<linalg::vec3, shared::SH_L1_LAYERS_PER_PAGE> l1 =
      lightmap.indirect_l1_pages.load_l1(at.page, at.x, at.y, l0);

  for (int axis = 0; axis < shared::SH_L1_LAYERS_PER_PAGE; ++axis)
    assert(std::abs(l1[axis].x) <= shared::SH_L1_NORMALIZATION * l0.x);

  // And the pair round trips through the PAGES rather than inside one call: an
  // encode and a decode that disagree by a factor of two look exactly like a
  // dimmer bounce.
  shared::lightmap_atlas_t atlas;
  atlas.size_in_texels = 4;
  atlas.page_count = 1;

  shared::lightmap_pages_t pages;
  pages.allocate(atlas, shared::lightmap_pixel_format_t::Unorm8x4,
                 shared::SH_L1_LAYERS_PER_PAGE);

  const linalg::vec3 stored_l0{2.f, 4.f, 8.f};
  Array<linalg::vec3, shared::SH_L1_LAYERS_PER_PAGE> stored_l1;
  stored_l1[0] = {1.f, -2.f, 3.f};
  stored_l1[1] = {-0.5f, 0.25f, -4.f};
  stored_l1[2] = {0.f, 6.f, 1.f};

  pages.store_l1(0, 1, 2, stored_l0, stored_l1);
  const Array<linalg::vec3, shared::SH_L1_LAYERS_PER_PAGE> read =
      pages.load_l1(0, 1, 2, stored_l0);

  // Eight bits over a range of 2 * sqrt(3) * L0, so the tolerance is that step
  // and it is per channel rather than absolute.
  for (int axis = 0; axis < shared::SH_L1_LAYERS_PER_PAGE; ++axis)
  {
    assert(std::abs(read[axis].x - stored_l1[axis].x) <
           shared::SH_L1_NORMALIZATION * stored_l0.x / 100.f);
    assert(std::abs(read[axis].y - stored_l1[axis].y) <
           shared::SH_L1_NORMALIZATION * stored_l0.y / 100.f);
    assert(std::abs(read[axis].z - stored_l1[axis].z) <
           shared::SH_L1_NORMALIZATION * stored_l0.z / 100.f);
  }

  // A channel with no light has no direction to have, and dividing by its L0 is
  // what the encoder must not do.
  pages.store_l1(0, 0, 0, {0.f, 0.f, 0.f}, stored_l1);
  const Array<linalg::vec3, shared::SH_L1_LAYERS_PER_PAGE> dark =
      pages.load_l1(0, 0, 0, {0.f, 0.f, 0.f});
  for (int axis = 0; axis < shared::SH_L1_LAYERS_PER_PAGE; ++axis)
    assert(dark[axis].x == 0.f && dark[axis].y == 0.f && dark[axis].z == 0.f);
}

// The sidecar carries FOUR page sets now, and the L1 one is the only one whose
// layer count is not the atlas's. A file that wrote it and derived it back would
// read a third of the bounce and find the next field inside the pixels.
void a_sidecar_round_trips_the_bounce()
{
  const shared::map_t map = map_with_a_floor_a_ceiling_and_a_light_between_them();
  const shared::lightmap_t baked = bake_indirect_for(map, traced_solve_settings());

  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() / "tilde_lightmap_test";
  std::filesystem::create_directories(directory);
  const std::string map_path = (directory / "bounce_round_trip.source").generic_string();

  const uint32_t hash = shared::compute_map_content_hash(map);
  shared::save_lightmap_sidecar(map_path, baked, hash);
  const shared::lightmap_t loaded = shared::load_lightmap_sidecar(map_path, hash);

  assert(!loaded.charts.empty());
  assert(loaded.indirect_l0_pages.bytes == baked.indirect_l0_pages.bytes);
  assert(loaded.indirect_l1_pages.bytes == baked.indirect_l1_pages.bytes);
  assert(loaded.indirect_l1_pages.page_count ==
         loaded.atlas.page_count * shared::SH_L1_LAYERS_PER_PAGE);
  assert(loaded.indirect_l0_pages.format == shared::lightmap_pixel_format_t::Rgb9e5);
  assert(loaded.indirect_l1_pages.format == shared::lightmap_pixel_format_t::Unorm8x4);

  // Through the accessor and not the bytes, because that is what proves the layer
  // count survived: the same texel decodes to the same direction.
  const atlas_position_t at = texel_at_the_middle_of_the_floor(loaded);
  const linalg::vec3 l0 = loaded.indirect_l0_pages.load(at.page, at.x, at.y);
  const Array<linalg::vec3, shared::SH_L1_LAYERS_PER_PAGE> l1 =
      loaded.indirect_l1_pages.load_l1(at.page, at.x, at.y, l0);
  assert(l1[1].x > 0.f);
}

// The mean over the whole floor rather than one texel: a chain is a random walk,
// so one texel is a sample of a distribution and the average is what the estimator
// promises to get right.
float mean_indirect_on_the_floor(const shared::lightmap_t &lightmap)
{
  const shared::lightmap_chart_t *floor = nullptr;
  for (const shared::lightmap_chart_t &chart : lightmap.charts)
  {
    if (chart.plane.normal.y < 0.9f) continue;
    if (!floor || chart.origin.y < floor->origin.y) floor = &chart;
  }
  assert(floor);

  // The RECONSTRUCTION rather than L0 alone, because that is the number a shader
  // reads: an estimator that scaled with its sample count in the L1 term and not
  // in the L0 one would pass a test that only looked at L0.
  double total = 0.0;
  size_t count = 0;
  for (int y = 0; y < shared::chart_covered_height(*floor, lightmap.settings); ++y)
    for (int x = 0; x < shared::chart_covered_width(*floor, lightmap.settings); ++x)
    {
      total += reconstructed_irradiance(
                   lightmap, floor->page,
                   floor->atlas_rect.min_x + lightmap.settings.gutter_in_texels + x,
                   floor->atlas_rect.min_y + lightmap.settings.gutter_in_texels + y,
                   floor->plane.normal)
                   .x;
      ++count;
    }

  assert(count > 0);
  return (float)(total / (double)count);
}

// An estimator that forgot to divide by its sample count is BRIGHTER the more
// samples it takes, and looks perfectly plausible at any single setting. This is
// the assertion that catches it, and it is the reason the chain count is a
// quality knob rather than an exposure one.
void the_bounce_does_not_scale_with_the_chain_count()
{
  const shared::map_t map = map_with_a_floor_a_ceiling_and_a_light_between_them();

  shared::lightmap_solve_settings_t few = traced_solve_settings();
  few.indirect_rays_per_sample = 8;
  shared::lightmap_solve_settings_t many = traced_solve_settings();
  many.indirect_rays_per_sample = 32;

  const float sparse = mean_indirect_on_the_floor(bake_indirect_for(map, few));
  const float dense = mean_indirect_on_the_floor(bake_indirect_for(map, many));

  assert(sparse > 0.f && dense > 0.f);
  assert(std::abs(sparse - dense) < 0.25f * std::max(sparse, dense));
}

// A chain draws its direction from a hash of the atlas position, never from
// shared/rng.hpp -- and the chart loop runs on as many threads as the machine
// has. Neither can be seen to be wrong except by baking twice.
void an_indirect_rebake_reproduces_itself_byte_for_byte()
{
  const shared::map_t map = map_with_a_floor_a_ceiling_and_a_light_between_them();
  const shared::lightmap_solve_settings_t solve = traced_solve_settings();

  const shared::lightmap_t first = bake_indirect_for(map, solve);
  const shared::lightmap_t second = bake_indirect_for(map, solve);

  assert(!first.indirect_l0_pages.bytes.empty());
  assert(first.indirect_l0_pages.bytes == second.indirect_l0_pages.bytes);
  assert(!first.indirect_l1_pages.bytes.empty());
  assert(first.indirect_l1_pages.bytes == second.indirect_l1_pages.bytes);
}

// Indirect is a MODE of the same solve, so the thing to pin is that turning it on
// changes nothing about the direct half -- the same guarantee the per-light masks
// make. And the off case must leave NO pages behind, or a sidecar would carry a
// bounce the settings say was never traced.
void tracing_indirect_light_moves_no_direct_pixel()
{
  const shared::map_t map = map_with_a_floor_a_ceiling_and_a_light_between_them();

  shared::lightmap_solve_settings_t untraced = traced_solve_settings();
  untraced.trace_indirect_light = false;

  const shared::lightmap_t without = bake_for(map, untraced);
  const shared::lightmap_t with = bake_indirect_for(map, traced_solve_settings());

  assert(without.indirect_l0_pages.empty());
  assert(without.indirect_l1_pages.empty());
  assert(!with.indirect_l0_pages.empty());

  assert(!without.irradiance_pages.bytes.empty());
  assert(without.irradiance_pages.bytes == with.irradiance_pages.bytes);
  assert(without.visibility_pages.bytes == with.visibility_pages.bytes);
}


// --- Gate 5: probe placement -------------------------------------------------

void a_probe_grid_pads_the_geometry_by_one_spacing()
{
  shared::map_t map;
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({0, 0, 0}, {64, 64, 64})});

  const std::optional<shared::probe_grid_t> grid = shared::try_build_probe_grid(map, 64.f);
  assert(grid.has_value());

  // The brush spans [-64, 64]; one spacing of padding puts probes at -128 and
  // 128, so five per axis.
  assert(grid->count.x == 5 && grid->count.y == 5 && grid->count.z == 5);
  assert(grid->probe_count() == 125);
  assert(std::abs(grid->origin.x + 128.f) < 1e-4f);
  assert(std::abs(grid->origin.y + 128.f) < 1e-4f);
  assert(std::abs(grid->origin.z + 128.f) < 1e-4f);

  const shared::aabb_bounds_t bounds = grid->bounds();
  assert(std::abs(bounds.max.x - 128.f) < 1e-4f);
  assert(std::abs(bounds.max.z - 128.f) < 1e-4f);
}

// A brush off the grid must not drag every probe off it with it: the origin
// stays a multiple of the spacing, and the grid grows to cover the brush.
void a_probe_grid_snaps_to_the_spacing_and_not_to_the_brush()
{
  shared::map_t map;
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({10, 0, 0}, {64, 64, 64})});

  const std::optional<shared::probe_grid_t> grid = shared::try_build_probe_grid(map, 64.f);
  assert(grid.has_value());

  // x spans [-54, 74]: snapped down to -64 minus one spacing is -128, snapped
  // up to 128 plus one spacing is 192 -- six probes.
  assert(std::abs(grid->origin.x + 128.f) < 1e-4f);
  assert(grid->count.x == 6);
  assert(grid->count.y == 5);
  assert(std::abs(grid->bounds().max.x - 192.f) < 1e-4f);
}

void a_probe_inside_a_brush_is_inside_and_one_on_its_face_is_too()
{
  shared::map_t map;
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({0, 0, 0}, {64, 64, 64})});

  const std::optional<shared::probe_grid_t> grid = shared::try_build_probe_grid(map, 64.f);
  assert(grid.has_value());

  const Bounding_Volume_Hierarchy occluders = shared::build_occluder_bvh(map);
  const std::vector<uint8_t> inside = shared::classify_probes_inside_solid(*grid, occluders);
  assert(inside.size() == grid->probe_count());

  assert(inside[grid->index_of(2, 2, 2)] == 1); // the centre
  assert(inside[grid->index_of(1, 2, 2)] == 1); // on the -x face
  assert(inside[grid->index_of(0, 2, 2)] == 0); // one spacing outside it
  assert(inside[grid->index_of(0, 0, 0)] == 0); // the padded corner

  // The solid spans three probes on each axis, faces included.
  size_t inside_count = 0;
  for (const uint8_t flag : inside) inside_count += flag;
  assert(inside_count == 27);
}

void an_empty_map_has_no_probe_grid()
{
  shared::map_t map;
  assert(!shared::try_build_probe_grid(map, 64.f).has_value());
}

void a_grid_too_long_for_a_3d_texture_is_refused()
{
  shared::map_t map;
  map.geometry.push_back(
      {map.next_uid++, shared::make_box_brush({0, 0, 0}, {64.f * 300.f, 64, 64})});

  assert(!shared::try_build_probe_grid(map, 64.f).has_value());
  assert(shared::try_build_probe_grid(map, 256.f).has_value());
}


// --- Gate 6: reflection capture placement ------------------------------------

// A closed room, interior [-256, 256] x [0, 256] x [-256, 256], walls 32 thick.
// The 64-unit probe grid snaps its origin to -384 / -128 / -384, so a 128-unit
// capture lattice lands at x, z in {-128, 0, 128} and y = 128: nine captures.
shared::map_t map_with_a_closed_room()
{
  shared::map_t map;
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({0, -16, 0}, {288, 16, 288})});
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({0, 272, 0}, {288, 16, 288})});
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({-272, 128, 0}, {16, 128, 288})});
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({272, 128, 0}, {16, 128, 288})});
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({0, 128, -272}, {288, 128, 16})});
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({0, 128, 272}, {288, 128, 16})});
  return map;
}

shared::reflection_capture_set_t build_captures_for(const shared::map_t &map,
                                                    float reflection_spacing,
                                                    float open_face_extent = 4096.f)
{
  const std::optional<shared::probe_grid_t> grid = shared::try_build_probe_grid(map, 64.f);
  assert(grid.has_value());
  const Bounding_Volume_Hierarchy occluders = shared::build_occluder_bvh(map);
  const std::vector<uint8_t> inside = shared::classify_probes_inside_solid(*grid, occluders);

  shared::reflection_capture_settings_t settings;
  settings.spacing_in_world_units = reflection_spacing;
  settings.open_face_extent = open_face_extent;
  return shared::build_reflection_captures(map, *grid, inside, occluders, settings);
}

const shared::reflection_capture_t *find_capture_at(const shared::reflection_capture_set_t &set,
                                                    const linalg::vec3 &position)
{
  for (const shared::reflection_capture_t &capture : set.captures)
    if (linalg::length(capture.position - position) < 1e-3f) return &capture;
  return nullptr;
}

bool near(float a, float b) { return std::abs(a - b) < 1e-3f; }

void a_capture_in_a_rectangular_room_measures_the_room()
{
  const shared::map_t map = map_with_a_closed_room();
  const shared::reflection_capture_set_t set = build_captures_for(map, 128.f);

  assert(near(set.spacing, 128.f));
  assert(set.captures.size() == 9);

  const shared::reflection_capture_t *capture = find_capture_at(set, {0, 128, 0});
  assert(capture != nullptr);
  assert(capture->open_faces == 0);
  assert(!capture->box_overridden);
  assert(near(capture->box.min.x, -256.f) && near(capture->box.max.x, 256.f));
  assert(near(capture->box.min.y, 0.f) && near(capture->box.max.y, 256.f));
  assert(near(capture->box.min.z, -256.f) && near(capture->box.max.z, 256.f));

  for (const shared::reflection_capture_t &other : set.captures)
  {
    assert(other.open_faces == 0);
    assert(!other.box_overridden);
    assert(near(other.box.min.x, -256.f) && near(other.box.max.x, 256.f));
    assert(near(other.box.min.y, 0.f) && near(other.box.max.y, 256.f));
    assert(near(other.box.min.z, -256.f) && near(other.box.max.z, 256.f));
    assert(near(other.position.y, 128.f));
    assert(std::abs(other.position.x) <= 128.f && std::abs(other.position.z) <= 128.f);
  }
}

void a_capture_lattice_snaps_to_the_probe_spacing()
{
  const shared::map_t map = map_with_a_closed_room();
  const shared::reflection_capture_set_t set = build_captures_for(map, 100.f);
  assert(near(set.spacing, 128.f));
  assert(set.captures.size() == 9);

  const shared::reflection_capture_set_t fine = build_captures_for(map, 10.f);
  assert(near(fine.spacing, 64.f));
  assert(fine.captures.size() == 7 * 3 * 7);
  assert(find_capture_at(fine, {-192, 64, 192}) != nullptr);
  assert(find_capture_at(fine, {-256, 64, 192}) == nullptr);
}

void a_capture_inside_a_solid_is_absent_and_a_pillar_bounds_its_neighbour()
{
  shared::map_t map = map_with_a_closed_room();
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({0, 128, 0}, {16, 16, 16})});

  const shared::reflection_capture_set_t set = build_captures_for(map, 128.f);
  assert(set.captures.size() == 8);
  assert(find_capture_at(set, {0, 128, 0}) == nullptr);

  const shared::reflection_capture_t *beside = find_capture_at(set, {128, 128, 0});
  assert(beside != nullptr);
  assert(near(beside->box.min.x, 16.f));
  assert(near(beside->box.max.x, 256.f));
  assert(near(beside->box.min.y, 0.f));
  assert(near(beside->box.min.z, -256.f));

  const shared::reflection_capture_t *behind = find_capture_at(set, {0, 128, 128});
  assert(behind != nullptr);
  assert(near(behind->box.min.z, 16.f));
  assert(near(behind->box.min.x, -256.f));

  const shared::reflection_capture_t *diagonal = find_capture_at(set, {128, 128, 128});
  assert(diagonal != nullptr);
  assert(near(diagonal->box.min.x, -256.f) && near(diagonal->box.min.z, -256.f));
}

void a_capture_facing_nothing_is_open_on_that_face()
{
  shared::map_t map;
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({0, -16, 0}, {288, 16, 288})});
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({0, 128, 64}, {8, 136, 8})});

  const shared::reflection_capture_set_t set = build_captures_for(map, 128.f, 4096.f);
  const shared::reflection_capture_t *capture = find_capture_at(set, {0, 128, 0});
  assert(capture != nullptr);

  assert(!capture->face_is_open(shared::reflection_box_face_t::Negative_Y));
  assert(!capture->face_is_open(shared::reflection_box_face_t::Positive_Z));
  assert(capture->face_is_open(shared::reflection_box_face_t::Positive_Y));
  assert(capture->face_is_open(shared::reflection_box_face_t::Negative_X));
  assert(capture->face_is_open(shared::reflection_box_face_t::Positive_X));
  assert(capture->face_is_open(shared::reflection_box_face_t::Negative_Z));

  assert(near(capture->box.min.y, 0.f));
  assert(near(capture->box.max.z, 56.f));
  assert(near(capture->box.max.y, 128.f + 4096.f));
  assert(near(capture->box.min.x, -4096.f));
  assert(near(capture->box.max.x, 4096.f));
  assert(near(capture->box.min.z, -4096.f));

  const shared::reflection_capture_t *other_side = find_capture_at(set, {0, 128, 128});
  assert(other_side != nullptr);
  assert(!other_side->face_is_open(shared::reflection_box_face_t::Negative_Z));
  assert(other_side->face_is_open(shared::reflection_box_face_t::Positive_Z));
  assert(near(other_side->box.min.z, 72.f));

  const shared::reflection_capture_t *clear = find_capture_at(set, {128, 128, 0});
  assert(clear != nullptr);
  assert(clear->face_is_open(shared::reflection_box_face_t::Positive_Z));
  assert(!clear->face_is_open(shared::reflection_box_face_t::Negative_Y));
}

void a_reflection_volume_overrides_the_measured_box()
{
  shared::map_t map = map_with_a_closed_room();
  std::shared_ptr<entities::Reflection_Volume_Entity> volume =
      std::make_shared<entities::Reflection_Volume_Entity>();
  volume->position = {0, 128, 0};
  volume->volume.half_extents = {100, 100, 100};
  map.entities.push_back({map.next_uid++, volume});

  const shared::reflection_capture_set_t set = build_captures_for(map, 128.f);
  assert(set.captures.size() == 9);

  const shared::reflection_capture_t *covered = find_capture_at(set, {0, 128, 0});
  assert(covered != nullptr);
  assert(covered->box_overridden);
  assert(covered->open_faces == 0);
  assert(near(covered->box.min.x, -100.f) && near(covered->box.max.x, 100.f));
  assert(near(covered->box.min.y, 28.f) && near(covered->box.max.y, 228.f));
  assert(near(covered->box.min.z, -100.f) && near(covered->box.max.z, 100.f));

  size_t overridden = 0;
  for (const shared::reflection_capture_t &capture : set.captures) overridden += capture.box_overridden;
  assert(overridden == 1);

  const shared::reflection_capture_t *outside = find_capture_at(set, {128, 128, 0});
  assert(outside != nullptr);
  assert(!outside->box_overridden);
  assert(near(outside->box.min.x, -256.f));
}

void the_capture_pick_is_the_nearest_four_weighted_by_distance()
{
  const shared::map_t map = map_with_a_closed_room();
  const shared::reflection_capture_set_t set = build_captures_for(map, 128.f);

  const shared::reflection_capture_pick_t on_top = shared::find_captures_for(set, {0, 128, 0});
  assert(on_top.count == shared::REFLECTION_BLEND_COUNT);
  assert(near(set.captures[on_top.indices[0]].position.x, 0.f));
  assert(near(set.captures[on_top.indices[0]].position.y, 128.f));
  assert(near(set.captures[on_top.indices[0]].position.z, 0.f));
  float total = 0.f;
  for (uint32_t slot = 0; slot < on_top.count; ++slot) total += on_top.weights[slot];
  assert(near(total, 1.f));
  assert(on_top.weights[0] > 0.9f);
  for (uint32_t slot = 1; slot < on_top.count; ++slot)
  {
    assert(near(linalg::length(set.captures[on_top.indices[slot]].position -
                               linalg::vec3{0, 128, 0}),
                128.f));
    assert(on_top.weights[slot] < 0.05f);
  }

  const shared::reflection_capture_pick_t between = shared::find_captures_for(set, {64, 128, 0});
  assert(between.count == shared::REFLECTION_BLEND_COUNT);
  assert(near(between.weights[0], between.weights[1]));
  {
    const float x0 = set.captures[between.indices[0]].position.x;
    const float x1 = set.captures[between.indices[1]].position.x;
    assert((near(x0, 0.f) && near(x1, 128.f)) || (near(x0, 128.f) && near(x1, 0.f)));
  }

  shared::reflection_capture_set_t two;
  two.captures.push_back({.position = {0, 0, 0}});
  two.captures.push_back({.position = {300, 0, 0}});
  const shared::reflection_capture_pick_t pair = shared::find_captures_for(two, {100, 0, 0});
  assert(pair.count == 2);
  assert(pair.indices[0] == 0 && pair.indices[1] == 1);
  assert(pair.weights[0] > pair.weights[1]);
  assert(near(pair.weights[0] + pair.weights[1], 1.f));

  const shared::reflection_capture_set_t none;
  assert(shared::find_captures_for(none, {0, 0, 0}).count == 0);
}


// --- Gate 6: what a capture stores -------------------------------------------

void a_cube_texel_direction_points_into_its_face()
{
  const int size = 8;
  const linalg::vec3 axes[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
                                {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
  for (int face = 0; face < shared::REFLECTION_CUBE_FACE_COUNT; ++face)
  {
    for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x)
    {
      const linalg::vec3 direction = shared::reflection_cube_direction(face, x, y, size);
      assert(near(linalg::length(direction), 1.f));
      const float along = linalg::dot(direction, axes[face]);
      assert(along > 0.57f);
      assert(along >= std::abs(direction.x) - 1e-5f && along >= std::abs(direction.y) - 1e-5f &&
             along >= std::abs(direction.z) - 1e-5f);
    }
  }

  const linalg::vec3 corner = shared::reflection_cube_direction(0, 0, 0, 1);
  assert(near(corner.x, 1.f) && near(corner.y, 0.f) && near(corner.z, 0.f));
}

// The ceiling's underside glows; nothing else in the room emits or is lit. The
// texel looking up reads the glow itself, since the first hit's emission enters
// a chain with a PI the projection divides back out; the texel looking down
// reads only what the grey floor bounces back of it.
void a_capture_sees_an_emissive_ceiling_directly_and_the_floor_reflects_it()
{
  shared::map_t map = map_with_a_floor_and_a_ceiling_and_no_light();
  shared::brush_geometry_t& ceiling =
      std::get<shared::brush_geometry_t>(map.geometry[1].value);
  shared::face_surface_for(ceiling, Plane{{0, 64, 0}, {0, -1, 0}}).material = 1;

  const assets::texture_asset_t albedo = one_texel(255, 255, 255);
  const assets::texture_asset_t emissive = one_texel(255, 128, 0);
  const Bounding_Volume_Hierarchy bvh = shared::build_occluder_bvh(map);
  shared::traced_scene_t scene = shared::build_traced_scene(map, bvh);
  scene.materials = {{nullptr, nullptr}, {&albedo, &emissive}};

  shared::reflection_capture_set_t set;
  set.captures.push_back({.position = {0, 32, 0}});

  shared::indirect_trace_settings_t settings;
  settings.rays_per_sample = 64;
  const int size = 8;
  shared::bake_reflection_captures(set, scene, {}, settings, size);

  const shared::reflection_cube_t& cube = set.captures.front().cube;
  assert(!cube.empty());
  assert(cube.size_in_texels == size);

  const linalg::vec3 glow{shared::srgb_byte_to_linear(255), shared::srgb_byte_to_linear(128),
                          0.f};
  const linalg::vec3 up = cube.load(2, size / 2, size / 2);
  const linalg::vec3 down = cube.load(3, size / 2, size / 2);

  assert(up.x >= glow.x * 0.99f);
  assert(up.x < glow.x * 2.f);
  assert(std::abs(up.x / up.y - glow.x / glow.y) < 2e-2f);
  assert(up.z == 0.f);

  assert(down.x > 0.f);
  assert(down.x < up.x);
  assert(std::abs(down.x / down.y - glow.x / glow.y) < 2e-2f);
  assert(down.z == 0.f);

  const linalg::vec3 sideways = cube.load(0, size / 2, size / 2);
  assert(sideways.x == 0.f && sideways.y == 0.f && sideways.z == 0.f);
}

void a_batched_capture_bake_is_the_reference_bake_bit_for_bit()
{
  const shared::map_t map = map_with_a_floor_a_ceiling_and_a_light_between_them();

  const auto bake = [&](const shared::lightmap_solve_settings_t& solve,
                        shared::lightmap_batch_solver_t* solver) {
    shared::lightmap_t lightmap = pack_for(map);
    lightmap.settings.probe_spacing_in_world_units = 16.f;
    lightmap.settings.reflection_spacing_in_world_units = 32.f;
    lightmap.settings.reflection_size_in_texels = 4;
    shared::bake_lightmap(map, lightmap, solve, solver);
    return lightmap;
  };

  shared::lightmap_solve_settings_t traced = traced_solve_settings();
  traced.bake_reflection_captures = true;

  const shared::lightmap_t reference = bake(traced, nullptr);
  assert(reference.reflections.captures.size() == 16);
  assert(reference.reflections.baked());
  assert(near(reference.reflections.spacing, 32.f));

  shared::cpu_batch_solver_t solver;
  const shared::lightmap_t batched = bake(traced, &solver);
  assert(batched.reflections.captures.size() == reference.reflections.captures.size());
  for (size_t i = 0; i < reference.reflections.captures.size(); ++i)
  {
    const shared::reflection_capture_t& a = reference.reflections.captures[i];
    const shared::reflection_capture_t& b = batched.reflections.captures[i];
    assert(near(a.position.x, b.position.x) && near(a.position.y, b.position.y) &&
           near(a.position.z, b.position.z));
    assert(a.cube.size_in_texels == 4 && b.cube.size_in_texels == 4);
    assert(a.cube.bytes == b.cube.bytes);
  }
  assert(solver.statistics().capture_dispatches == 1);
  assert(solver.statistics().shade.chains > 0);

  bool any_lit = false;
  for (const shared::reflection_capture_t& capture : reference.reflections.captures)
    for (size_t texel = 0; texel < capture.cube.texel_count(); ++texel)
      any_lit |= capture.cube.load(texel).x > 0.f;
  assert(any_lit);

  shared::lightmap_solve_settings_t untraced = traced;
  untraced.trace_indirect_light = false;
  assert(bake(untraced, nullptr).reflections.empty());

  shared::lightmap_solve_settings_t off = traced;
  off.bake_reflection_captures = false;
  assert(bake(off, nullptr).reflections.empty());
}

// Step 4: the capture set and every cube's mip chain ride the sidecar, with the
// two settings that produced them; a capture whose bytes do not fit its
// declared chain drops the whole set rather than being indexed.
void a_sidecar_round_trips_the_reflection_captures()
{
  const shared::map_t map = map_with_a_floor_a_ceiling_and_a_light_between_them();
  shared::lightmap_t baked = pack_for(map);
  baked.settings.probe_spacing_in_world_units = 16.f;
  baked.settings.reflection_spacing_in_world_units = 32.f;
  baked.settings.reflection_size_in_texels = 4;
  shared::lightmap_solve_settings_t traced = traced_solve_settings();
  traced.bake_reflection_captures = true;
  shared::bake_lightmap(map, baked, traced, nullptr);
  assert(baked.reflections.baked());
  assert(baked.reflections.captures.size() == 16);

  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() / "tilde_lightmap_test";
  std::filesystem::create_directories(directory);
  const std::string map_path = (directory / "round_trip_reflections.source").generic_string();

  const uint32_t hash = shared::compute_map_content_hash(map);
  shared::save_lightmap_sidecar(map_path, baked, hash);
  const shared::lightmap_t loaded = shared::load_lightmap_sidecar(map_path, hash);

  assert(loaded.settings.reflection_spacing_in_world_units == 32.f);
  assert(loaded.settings.reflection_size_in_texels == 4);
  assert(loaded.reflections.spacing == baked.reflections.spacing);
  assert(loaded.reflections.captures.size() == baked.reflections.captures.size());
  for (size_t i = 0; i < baked.reflections.captures.size(); ++i)
  {
    const shared::reflection_capture_t& before = baked.reflections.captures[i];
    const shared::reflection_capture_t& after = loaded.reflections.captures[i];
    assert(before.position.x == after.position.x && before.position.y == after.position.y &&
           before.position.z == after.position.z);
    assert(before.box.min.x == after.box.min.x && before.box.min.y == after.box.min.y &&
           before.box.min.z == after.box.min.z);
    assert(before.box.max.x == after.box.max.x && before.box.max.y == after.box.max.y &&
           before.box.max.z == after.box.max.z);
    assert(before.probe_index == after.probe_index);
    assert(before.open_faces == after.open_faces);
    assert(before.box_overridden == after.box_overridden);
    assert(after.cube.size_in_texels == 4);
    assert(after.cube.mip_count == shared::reflection_cube_t::mip_count_for(4));
    assert(after.cube.bytes == before.cube.bytes);
  }

  // A chain that lies about its size is the file disagreeing with itself.
  shared::lightmap_t corrupt = baked;
  corrupt.reflections.captures[3].cube.size_in_texels = 8;
  shared::save_lightmap_sidecar(map_path, corrupt, hash);
  const shared::lightmap_t refused = shared::load_lightmap_sidecar(map_path, hash);
  assert(!refused.charts.empty());
  assert(refused.reflections.empty());

  // A bake with no captures round-trips as none, not as a set of zero cubes.
  shared::lightmap_t bare = baked;
  bare.reflections = {};
  shared::save_lightmap_sidecar(map_path, bare, hash);
  assert(shared::load_lightmap_sidecar(map_path, hash).reflections.empty());
}

void identical_capture_answers_agree()
{
  std::vector<shared::gpu_sample_t> samples(12);
  std::vector<linalg::vec3> answers(12);
  for (size_t i = 0; i < samples.size(); ++i)
  {
    samples[i].chart_index = (uint32_t)(i / 6);
    answers[i] = {(float)i * 0.1f, 0.5f, (float)(i % 3)};
  }
  const size_t groups[2] = {0, 1};
  const shared::record_comparison_report_t same =
      shared::compare_capture_results(samples, Span<const size_t>(groups), answers, answers);
  assert(same.agrees());
  assert(same.differing_records == 0);
  assert(same.charts.size() == 2);

  std::vector<linalg::vec3> biased = answers;
  for (linalg::vec3& value : biased) value.x += 10.f;
  const shared::record_comparison_report_t off =
      shared::compare_capture_results(samples, Span<const size_t>(groups), answers, biased);
  assert(off.differing_records == 12);
}


// --- Gate 6 step 3: the prefilter and the BRDF table -------------------------

void a_cube_texel_direction_round_trips_to_its_texel()
{
  for (const int size : {1, 2, 8, 64})
    for (int face = 0; face < shared::REFLECTION_CUBE_FACE_COUNT; ++face)
      for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
          const linalg::vec3 direction = shared::reflection_cube_direction(face, x, y, size);
          const shared::reflection_cube_texel_t texel =
              shared::reflection_cube_texel_of(direction, size);
          assert(texel.face == face && texel.x == x && texel.y == y);
        }

  const shared::reflection_cube_texel_t coarse =
      shared::reflection_cube_texel_of(shared::reflection_cube_direction(2, 5, 6, 8), 4);
  assert(coarse.face == 2 && coarse.x == 2 && coarse.y == 3);
}

void a_prefiltered_cube_keeps_mip_zero_and_averages_a_uniform_cube()
{
  shared::reflection_cube_t cube;
  cube.allocate(8);
  assert(cube.mip_count == 4);
  assert(cube.texels_in_mip(0) == 6 * 64 && cube.texels_in_mip(3) == 6);
  assert(cube.texel_count() == 6 * (64 + 16 + 4 + 1));
  assert(cube.texel_index_of(1, 0, 0, 0) == 6 * 64);

  const linalg::vec3 constant{0.5f, 0.25f, 0.125f};
  for (size_t texel = 0; texel < cube.texels_in_mip(0); ++texel) cube.store(texel, constant);
  const std::vector<uint8_t> before(cube.bytes.begin(),
                                    cube.bytes.begin() + (ptrdiff_t)(cube.texels_in_mip(0) * 4));

  shared::prefilter_reflection_cube(cube);

  const std::vector<uint8_t> after(cube.bytes.begin(),
                                   cube.bytes.begin() + (ptrdiff_t)(cube.texels_in_mip(0) * 4));
  assert(before == after);

  for (int mip = 1; mip < cube.mip_count; ++mip)
    for (size_t texel = cube.texel_offset_of_mip(mip); texel < cube.texel_offset_of_mip(mip + 1);
         ++texel)
    {
      const linalg::vec3 value = cube.load(texel);
      assert(std::abs(value.x - constant.x) < 0.01f);
      assert(std::abs(value.y - constant.y) < 0.005f);
      assert(std::abs(value.z - constant.z) < 0.0025f);
    }
}

void a_prefiltered_cube_spreads_a_bright_texel_over_its_own_hemisphere_only()
{
  shared::reflection_cube_t cube;
  cube.allocate(8);
  for (int y = 0; y < 8; ++y)
    for (int x = 0; x < 8; ++x) cube.store(0, 2, x, y, {8.f, 8.f, 8.f});

  shared::prefilter_reflection_cube(cube);

  const linalg::vec3 under = cube.load(1, 2, 2, 2);
  assert(under.x > 4.f && under.x <= 8.01f);

  for (int x = 1; x <= 2; ++x)
    for (int y = 1; y <= 2; ++y) assert(cube.load(1, 3, x, y).x == 0.f);

  const int last = cube.mip_count - 1;
  assert(cube.load(last, 2, 0, 0).x > 0.f);
  assert(cube.load(last, 2, 0, 0).x < 8.f);
  assert(cube.load(last, 3, 0, 0).x == 0.f);
  assert(cube.load(last, 0, 0, 0).x > 0.f);
}

void the_environment_brdf_reads_one_and_zero_head_on_and_rises_at_grazing()
{
  const shared::environment_brdf_lut_t lut = shared::build_environment_brdf_lut(32, 256);
  assert(lut.size == 32);
  assert(lut.scale_bias.size() == 32 * 32 * 2);

  const linalg::vec2 head_on_smooth = lut.load(31, 0);
  assert(head_on_smooth.x > 0.95f && head_on_smooth.x <= 1.f);
  assert(head_on_smooth.y < 0.02f);

  for (int roughness = 0; roughness < 32; ++roughness)
    for (int view = 0; view < 32; ++view)
    {
      const linalg::vec2 value = lut.load(view, roughness);
      assert(value.x >= 0.f && value.y >= 0.f);
      assert(value.x + value.y <= 1.02f);
    }

  const linalg::vec2 grazing = lut.load(1, 8);
  const linalg::vec2 facing = lut.load(30, 8);
  assert(grazing.y > facing.y);
  assert(grazing.x < facing.x);
}


// --- Gate 5: what a probe stores ---------------------------------------------

shared::probe_trace_t trace_probe_at(const shared::map_t &map, const linalg::vec3 &position,
                                     int rays_per_sample = 64)
{
  const Bounding_Volume_Hierarchy bvh = shared::build_occluder_bvh(map);
  const shared::traced_scene_t scene = shared::build_traced_scene(map, bvh);
  const std::vector<shared::baked_light_t> lights = shared::collect_lights(map);
  const shared::probe_visibility_slots_t slots = shared::assign_probe_visibility_channels(lights);

  shared::indirect_trace_settings_t settings;
  settings.rays_per_sample = rays_per_sample;
  return shared::trace_probe_light(scene, lights, slots, position, settings, 0x51a7u);
}

shared::indirect_sh_l1_t trace_probe_in(const shared::map_t &map, const linalg::vec3 &position,
                                        int rays_per_sample = 64)
{
  return trace_probe_at(map, position, rays_per_sample).light;
}

void make_every_light_mixed(shared::map_t &map)
{
  for (shared::map_entity_t &entry : map.entities)
    if (entities::Point_Light_Entity *light =
            entities::entity_as<entities::Point_Light_Entity>(entry.entity.get()))
      light->light.mode = entities::Light_Mode::Mixed;
}

// A probe below the light between the plates reads it DIRECTLY -- L0 above zero
// and the L1 vector pointing up at it -- which is the half a texel leaves to its
// slots and a probe cannot.
void a_probe_reads_a_baked_light_directly_and_knows_where_it_is()
{
  const shared::map_t map = map_with_a_floor_a_ceiling_and_a_light_between_them();

  const shared::indirect_sh_l1_t probe = trace_probe_in(map, {0, 8, 0});
  assert(probe.l0.x > 0.f);
  assert(probe.l1[1].x > 0.f);
  assert(probe.l1[1].x > std::abs(probe.l1[0].x) * 2.f);
  assert(probe.l1[1].x > std::abs(probe.l1[2].x) * 2.f);

  // The direct term alone is a known number: E * Y at the light's direction.
  // With no chains it is exactly that, and with them it is more.
  const shared::indirect_sh_l1_t direct_only = trace_probe_in(map, {0, 8, 0}, 0);
  const std::vector<shared::baked_light_t> lights = shared::collect_lights(map);
  const shared::light_arrival_t arrival =
      shared::arrival_at(lights[0].light, {0, 8, 0}, {0, 1, 0}, 100000.f);
  const float expected_l0 = lights[0].light.radiance.x * arrival.attenuation * shared::SH_L1_Y0;
  assert(std::abs(direct_only.l0.x - expected_l0) < 1e-4f);
  assert(probe.l0.x > direct_only.l0.x);
}

// A Mixed light reaches a dynamic object through the runtime tail, so the probe
// must not carry it directly -- but the ceiling it lights still bounces.
void a_probe_reads_a_mixed_light_through_its_bounce_only()
{
  shared::map_t map = map_with_a_floor_a_ceiling_and_a_light_between_them();
  const shared::indirect_sh_l1_t with_baked = trace_probe_in(map, {0, 8, 0});

  make_every_light_mixed(map);

  const shared::indirect_sh_l1_t with_mixed = trace_probe_in(map, {0, 8, 0});
  const shared::indirect_sh_l1_t mixed_direct_only = trace_probe_in(map, {0, 8, 0}, 0);

  assert(mixed_direct_only.l0.x == 0.f);
  assert(with_mixed.l0.x > 0.f);
  assert(with_mixed.l0.x < with_baked.l0.x);
}

// --- Gate 9 step 4: what a probe stores for a Mixed light ----------------------

// A Baked light's occlusion is folded into the probe's radiance and claims no
// channel; a Mixed light claims one, in slot order, and a fifth is left out.
void probe_visibility_channels_go_to_the_first_four_mixed_lights()
{
  const shared::map_t map = map_with_a_floor_a_ceiling_and_a_light_between_them();
  const std::vector<shared::baked_light_t> baked_only = shared::collect_lights(map);
  const shared::probe_visibility_slots_t none =
      shared::assign_probe_visibility_channels(baked_only);
  for (const int16_t slot : none) assert(slot == shared::LIGHTMAP_NO_LIGHT_SLOT);

  std::vector<shared::baked_light_t> six(6);
  for (size_t slot = 0; slot < six.size(); ++slot)
  {
    six[slot].uid = 100 + (shared::entity_uid_t)slot;
    six[slot].light.mode = slot == 0 ? entities::Light_Mode::Baked : entities::Light_Mode::Mixed;
  }
  const std::vector<shared::baked_light_t> &lights = six;
  const shared::probe_visibility_slots_t slots = shared::assign_probe_visibility_channels(lights);
  assert(slots[0] == 1 && slots[1] == 2 && slots[2] == 3 && slots[3] == 4);
}

// Under the light a probe sees all of a Mixed light and stores no radiance for
// it; above the ceiling plate it sees none of it. The visibility is the same
// light_visibility a texel of the atlas stores, at a point in space.
void a_probe_stores_a_mixed_lights_visibility_and_not_its_light()
{
  shared::map_t map = map_with_a_floor_a_ceiling_and_a_light_between_them();
  make_every_light_mixed(map);

  const shared::probe_trace_t below = trace_probe_at(map, {0, 8, 0}, 0);
  assert(below.light.l0.x == 0.f);
  assert(below.visibility[0] == 1.f);
  for (uint32_t channel = 1; channel < shared::PROBE_VISIBILITY_CHANNELS; ++channel)
    assert(below.visibility[channel] == 1.f);

  const shared::probe_trace_t above_the_ceiling = trace_probe_at(map, {0, 96, 0}, 0);
  assert(above_the_ceiling.visibility[0] == 0.f);
  assert(above_the_ceiling.visibility[1] == 1.f);
}

// Behind the ceiling plate the light is occluded: the direct term is shadowed
// out and nothing above the plate bounces, so the probe reads black.
void a_probe_a_wall_hides_from_the_light_reads_nothing_direct()
{
  const shared::map_t map = map_with_a_floor_a_ceiling_and_a_light_between_them();
  const shared::indirect_sh_l1_t above_the_ceiling = trace_probe_in(map, {0, 96, 0}, 0);
  assert(above_the_ceiling.l0.x == 0.f);
}

// The sphere sampler covers BOTH halves: a probe under the light gets a positive
// Y lean from the direct term, and the floor beneath it bounces back UP, so a
// probe between the two sees light from below as well -- the L0 with chains is
// well above the direct term alone, and the lean is less than pure single-direction.
void a_probe_sees_the_floor_bounce_from_below()
{
  const shared::map_t map = map_with_a_floor_a_ceiling_and_a_light_between_them();
  const shared::indirect_sh_l1_t probe = trace_probe_in(map, {0, 8, 0}, 256);
  const shared::indirect_sh_l1_t direct_only = trace_probe_in(map, {0, 8, 0}, 0);

  const float direct_ratio = direct_only.l1[1].x / direct_only.l0.x;
  const float ratio = probe.l1[1].x / probe.l0.x;
  assert(std::abs(direct_ratio - shared::SH_L1_NORMALIZATION) < 1e-3f);
  assert(ratio < direct_ratio);
  assert(ratio > 0.f);
}


// --- Gate 5: the baked volume --------------------------------------------------

shared::lightmap_solve_settings_t probe_solve_settings()
{
  shared::lightmap_solve_settings_t solve = traced_solve_settings();
  solve.bake_probes = true;
  return solve;
}

shared::lightmap_t bake_probes_for(const shared::map_t &map, float spacing,
                                   const shared::lightmap_solve_settings_t &solve)
{
  shared::lightmap_t lightmap = pack_for(map);
  lightmap.settings.probe_spacing_in_world_units = spacing;
  shared::bake_lightmap(map, lightmap, solve);
  return lightmap;
}

// Between the plates a probe is traced; on the floor's face and inside its slab
// it is not, and holds what the NEAREST open air holds instead of black. The
// floor is thick here so that a probe one spacing into it is nearer the lit
// room than the dark space underneath -- a thin slab's underside probe is
// legitimately filled from below, which is black.
void a_probe_bake_fills_the_volume_and_dilates_into_solids()
{
  shared::map_t map = map_with_a_floor_a_ceiling_and_a_light_between_them();
  map.geometry.front().value = shared::make_box_brush({0, -40, 0}, {64, 40, 64});
  const shared::lightmap_t lightmap = bake_probes_for(map, 16.f, probe_solve_settings());
  assert(!lightmap.probes.empty());

  const shared::probe_grid_t &grid = lightmap.probes.grid;
  assert(std::abs(grid.spacing - 16.f) < 1e-4f);

  const auto index_at = [&](const linalg::vec3 &position) {
    const linalg::vec3 offset = (position - grid.origin) * (1.f / grid.spacing);
    return grid.index_of((int)std::lround(offset.x), (int)std::lround(offset.y),
                         (int)std::lround(offset.z));
  };

  const shared::indirect_sh_l1_t open_air = lightmap.probes.load(index_at({0, 16, 0}));
  assert(open_air.l0.x > 0.f);
  assert(open_air.l1[1].x > 0.f);

  const shared::indirect_sh_l1_t on_the_floor = lightmap.probes.load(index_at({0, 0, 0}));
  const shared::indirect_sh_l1_t in_the_slab = lightmap.probes.load(index_at({0, -16, 0}));
  assert(on_the_floor.l0.x > 0.f);
  assert(in_the_slab.l0.x > 0.f);
}

// The whole volume: the slot table names the light, the open-air probe under
// it reads visible, the one over the ceiling reads occluded, and the dilated
// probe inside the plate takes its neighbours' answer rather than white.
void a_probe_bake_carries_a_mixed_lights_visibility_through_the_volume()
{
  shared::map_t map = map_with_a_floor_a_ceiling_and_a_light_between_them();
  make_every_light_mixed(map);
  const shared::lightmap_t lightmap = bake_probes_for(map, 16.f, probe_solve_settings());
  assert(!lightmap.probes.empty());
  assert(lightmap.probes.visibility_slots[0] == 0);
  assert(lightmap.probes.visibility_slots[1] == shared::LIGHTMAP_NO_LIGHT_SLOT);
  assert(lightmap.probes.visibility_bytes.size() == lightmap.probes.grid.probe_count() * 4);

  const shared::probe_grid_t &grid = lightmap.probes.grid;
  const auto index_at = [&](const linalg::vec3 &position) {
    const linalg::vec3 offset = (position - grid.origin) * (1.f / grid.spacing);
    return grid.index_of((int)std::lround(offset.x), (int)std::lround(offset.y),
                         (int)std::lround(offset.z));
  };

  assert(lightmap.probes.load_visibility(index_at({0, 16, 0}))[0] == 1.f);
  assert(lightmap.probes.load_visibility(index_at({0, 96, 0}))[0] == 0.f);
  assert(lightmap.probes.load_visibility(index_at({0, 96, 0}))[1] == 1.f);

  // The plate's two faces are inside it and are filled from their open
  // neighbours: the underside from the lit room below it, the top from the
  // occluded space above. Each takes its own side's answer, not a mix.
  assert(lightmap.probes.load_visibility(index_at({0, 64, 0}))[0] == 1.f);
  assert(lightmap.probes.load_visibility(index_at({0, 80, 0}))[0] == 0.f);
}

void a_bake_without_probes_clears_the_volume()
{
  const shared::map_t map = map_with_a_floor_a_ceiling_and_a_light_between_them();
  shared::lightmap_t lightmap = bake_probes_for(map, 16.f, probe_solve_settings());
  assert(!lightmap.probes.empty());

  shared::bake_lightmap(map, lightmap, traced_solve_settings());
  assert(lightmap.probes.empty());
}

void a_sidecar_round_trips_the_probes()
{
  shared::map_t map = map_with_a_floor_a_ceiling_and_a_light_between_them();
  make_every_light_mixed(map);
  const shared::lightmap_t baked = bake_probes_for(map, 16.f, probe_solve_settings());
  assert(baked.probes.visibility_slots[0] == 0);

  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() / "tilde_lightmap_test";
  std::filesystem::create_directories(directory);
  const std::string map_path = (directory / "probes_round_trip.source").generic_string();

  const uint32_t hash = shared::compute_map_content_hash(map);
  shared::save_lightmap_sidecar(map_path, baked, hash);
  const shared::lightmap_t loaded = shared::load_lightmap_sidecar(map_path, hash);

  assert(!loaded.charts.empty());
  assert(std::abs(loaded.settings.probe_spacing_in_world_units - 16.f) < 1e-4f);
  assert(loaded.probes.grid.count.x == baked.probes.grid.count.x);
  assert(loaded.probes.grid.count.y == baked.probes.grid.count.y);
  assert(loaded.probes.grid.count.z == baked.probes.grid.count.z);
  assert(std::abs(loaded.probes.grid.origin.x - baked.probes.grid.origin.x) < 1e-6f);
  assert(loaded.probes.l0_bytes == baked.probes.l0_bytes);
  assert(loaded.probes.l1_bytes == baked.probes.l1_bytes);
  for (uint32_t channel = 0; channel < shared::PROBE_VISIBILITY_CHANNELS; ++channel)
    assert(loaded.probes.visibility_slots[channel] == baked.probes.visibility_slots[channel]);
  assert(loaded.probes.visibility_bytes == baked.probes.visibility_bytes);
}

// The volume's codec is the atlas's: what goes in comes back within a byte of
// quantization, direction included.
void a_probe_volume_round_trips_its_own_codec()
{
  shared::probe_grid_t grid;
  grid.spacing = 8.f;
  grid.count = {2, 2, 2};

  shared::probe_volume_t volume;
  volume.allocate(grid);

  shared::indirect_sh_l1_t value;
  value.l0 = {2.f, 4.f, 8.f};
  value.l1[0] = {1.f, -2.f, 3.f};
  value.l1[1] = {-0.5f, 0.25f, -4.f};
  value.l1[2] = {0.f, 6.f, 1.f};
  volume.store(5, value);

  const shared::indirect_sh_l1_t back = volume.load(5);
  assert(std::abs(back.l0.x - 2.f) < 0.01f && std::abs(back.l0.z - 8.f) < 0.04f);
  for (int axis = 0; axis < shared::SH_L1_LAYERS_PER_PAGE; ++axis)
  {
    assert(std::abs(back.l1[axis].x - value.l1[axis].x) < 0.05f);
    assert(std::abs(back.l1[axis].y - value.l1[axis].y) < 0.1f);
    assert(std::abs(back.l1[axis].z - value.l1[axis].z) < 0.2f);
  }

  const shared::indirect_sh_l1_t untouched = volume.load(0);
  assert(untouched.l0.x == 0.f && untouched.l1[0].x == 0.f);

  // The visibility channels: a fresh volume reads fully visible, a stored
  // coverage comes back within a byte.
  for (const float channel : volume.load_visibility(3)) assert(channel == 1.f);
  volume.store_visibility(3, {{0.f, 0.25f, 0.5f, 1.f}});
  const Array<float, shared::PROBE_VISIBILITY_CHANNELS> coverage = volume.load_visibility(3);
  assert(coverage[0] == 0.f && coverage[3] == 1.f);
  assert(std::abs(coverage[1] - 0.25f) < 0.005f && std::abs(coverage[2] - 0.5f) < 0.005f);
}

} // namespace

// --- Static meshes -----------------------------------------------------------
//
// resources/models/Box.mesh is the fixture: 24 vertices, 12 triangles, UNIT
// sized, so the object's scale is what gives it a size -- 128 units here, the
// same box a_box_gets_one_chart_per_face builds as a brush. Loaded for real, so
// these run under ctest's pinned working directory or from the project root.

constexpr float BOX_HALF_EXTENT = 64.f;

shared::map_t map_with_a_mesh(const char* mesh_path, float scale)
{
  shared::map_t map;
  shared::static_mesh_geometry_t object;
  object.surface.mesh_path = mesh_path;
  object.scale = {scale, scale, scale};
  map.geometry.push_back({map.next_uid++, object});
  return map;
}

shared::map_t map_with_a_box_mesh()
{
  return map_with_a_mesh("resources/models/Box.mesh", 2.f * BOX_HALF_EXTENT);
}

// Which face of the box a world point is on -- the signed axis of its largest
// coordinate -- asserting the point IS on the surface.
linalg::vec3 box_face_normal_at(const linalg::vec3& position)
{
  const float absolute_x = std::abs(position.x);
  const float absolute_y = std::abs(position.y);
  const float absolute_z = std::abs(position.z);
  const float largest = std::max(absolute_x, std::max(absolute_y, absolute_z));
  assert(std::abs(largest - BOX_HALF_EXTENT) < 1e-2f);
  if (largest == absolute_x) return {position.x > 0.f ? 1.f : -1.f, 0.f, 0.f};
  if (largest == absolute_y) return {0.f, position.y > 0.f ? 1.f : -1.f, 0.f};
  return {0.f, 0.f, position.z > 0.f ? 1.f : -1.f};
}

// What every unwrapped object's charts must satisfy: each chart carries an
// unwrap whose coverage and twin agree with it, every uv lies inside its
// chart's covered rect, and every real face of the object is in exactly one
// chart. A degenerate face has no area to unwrap and may be in none.
void assert_charts_cover_every_face_once(const std::vector<shared::lightmap_chart_t>& charts,
                                         const std::vector<shared::world_triangle_t>& faces,
                                         const shared::lightmap_bake_settings_t& settings)
{
  std::vector<int> times_covered(faces.size(), 0);
  for (const shared::lightmap_chart_t& chart : charts)
  {
    assert(chart.polygon.empty());
    assert(!chart.unwrap.empty());
    assert(chart.unwrap.indices.size() == chart.unwrap.faces.size() * 3);
    assert(chart.triangles.size() == chart.unwrap.indices.size());
    assert(chart.twins.size() == chart.unwrap.faces.size());

    const float covered_u =
        (float)shared::chart_covered_width(chart, settings) * chart.world_units_per_texel;
    const float covered_v =
        (float)shared::chart_covered_height(chart, settings) * chart.world_units_per_texel;
    for (const shared::unwrapped_vertex_t& vertex : chart.unwrap.vertices)
    {
      assert(vertex.uv.x >= -1e-3f && vertex.uv.x <= covered_u + 1e-3f);
      assert(vertex.uv.y >= -1e-3f && vertex.uv.y <= covered_v + 1e-3f);
    }

    for (uint32_t face : chart.unwrap.faces)
    {
      assert(face < faces.size());
      ++times_covered[face];
    }
  }

  for (size_t face = 0; face < faces.size(); ++face)
  {
    if (faces[face].is_degenerate()) assert(times_covered[face] <= 1);
    else assert(times_covered[face] == 1);
  }
}

// lightmap_unwrap_plan.md step 2: a static mesh's charts come from xatlas, and
// its faces are the unwrap's rather than one chart per plane.
void a_static_mesh_is_unwrapped_into_charts()
{
  const shared::map_t map = map_with_a_box_mesh();
  const shared::lightmap_bake_settings_t settings;
  const std::vector<shared::lightmap_chart_t> charts =
      shared::build_lightmap_charts(map, settings);

  // Six planar faces at right angles: xatlas cannot merge them into fewer than
  // one chart and has no reason to cut one.
  assert(!charts.empty() && charts.size() <= 6);

  const shared::static_mesh_geometry_t& box =
      std::get<shared::static_mesh_geometry_t>(map.geometry[0].value);
  const std::vector<shared::world_triangle_t> faces = shared::static_mesh_world_triangles(box);
  assert(faces.size() == 12);
  assert_charts_cover_every_face_once(charts, faces, settings);

  // The twin is the mesh in the world: every corner on the box surface, every
  // normal a unit axis naming the face that corner lies on. (A corner is on
  // three faces at once, so it is the normal that says which.)
  for (const shared::lightmap_chart_t& chart : charts)
    for (const shared::chart_triangle_twin_t& twin : chart.twins)
      for (uint32_t corner = 0; corner < 3; ++corner)
      {
        const linalg::vec3& normal = twin.normals[corner];
        assert(std::abs(linalg::length(normal) - 1.f) < 1e-3f);
        assert(std::abs(linalg::dot(twin.corners[corner], normal) - BOX_HALF_EXTENT) < 1e-2f);
        (void)box_face_normal_at(twin.corners[corner]);
      }
}

// Step 1 through the unwrap: a texel of a mesh chart samples a point ON the
// mesh with the mesh's normal there, and six 128-unit faces at four units a
// texel are 6144 texels of surface however xatlas cut them up.
void a_texel_of_a_mesh_chart_samples_the_mesh_surface()
{
  const shared::map_t map = map_with_a_box_mesh();
  const shared::lightmap_bake_settings_t settings;
  const std::vector<shared::lightmap_chart_t> charts =
      shared::build_lightmap_charts(map, settings);

  int on_surface = 0;
  for (const shared::lightmap_chart_t& chart : charts)
    for (int texel_y = 0; texel_y < shared::chart_covered_height(chart, settings); ++texel_y)
      for (int texel_x = 0; texel_x < shared::chart_covered_width(chart, settings); ++texel_x)
      {
        const shared::texel_sample_t sample = shared::sample_texel(chart, texel_x, texel_y);
        if (!sample.on_surface) continue;
        ++on_surface;
        assert(linalg::dot(sample.normal, box_face_normal_at(sample.position)) > 0.999f);
      }

  assert(on_surface > 5500 && on_surface < 6800);
}

void a_sphere_unwraps_into_few_charts()
{
  const shared::map_t map = map_with_a_mesh("resources/models/Sphere.mesh", 128.f);
  const shared::lightmap_bake_settings_t settings;
  const std::vector<shared::lightmap_chart_t> charts =
      shared::build_lightmap_charts(map, settings);

  const shared::static_mesh_geometry_t& sphere =
      std::get<shared::static_mesh_geometry_t>(map.geometry[0].value);
  const std::vector<shared::world_triangle_t> faces =
      shared::static_mesh_world_triangles(sphere);
  size_t real_faces = 0;
  for (const shared::world_triangle_t& face : faces)
    if (!face.is_degenerate()) ++real_faces;
  assert(real_faces > 100);

  // Far fewer charts than facets -- the whole point of unwrapping. A chart per
  // plane would be one per facet here.
  assert(!charts.empty());
  assert(charts.size() * 4 <= real_faces);
  assert_charts_cover_every_face_once(charts, faces, settings);

  // Every twin corner is on the sphere.
  for (const shared::lightmap_chart_t& chart : charts)
    for (const shared::chart_triangle_twin_t& twin : chart.twins)
      for (uint32_t corner = 0; corner < 3; ++corner)
        assert(std::abs(linalg::length(twin.corners[corner]) - 64.f) < 0.5f);
}

// The pin for step 1: a box built as a brush and the same box built as a mesh
// both bake, at every texel, what the residual light delivers to the point the
// texel samples -- one through a polygon and a plane, the other through an
// unwrap and its twin. The light is under the box's top face so that face
// carries a gradient rather than a flat value; every other face bakes black.
void a_box_mesh_bakes_what_the_light_delivers()
{
  const auto light_it = [](shared::map_t& map) {
    std::shared_ptr<entities::Point_Light_Entity> light =
        std::make_shared<entities::Point_Light_Entity>();
    light->position = {0, 128, 0};
    light->range = 1024.f;
    light->light.color = {1.f, 1.f, 1.f};
    light->light.intensity = 1.f;
    map.entities.push_back({map.next_uid++, light});
    add_the_four_lights_that_outrank_everything(map);
  };

  shared::map_t brush_map;
  brush_map.geometry.push_back(
      {brush_map.next_uid++, shared::make_box_brush({0, 0, 0}, {64, 64, 64})});
  light_it(brush_map);

  shared::map_t mesh_map = map_with_a_box_mesh();
  light_it(mesh_map);

  // The centre sample alone, so a texel's value is the value at one known point.
  shared::lightmap_solve_settings_t solve;
  solve.samples_per_texel_edge = 1;

  const auto check = [&](const shared::map_t& map, int expected_lit, int lit_tolerance) {
    const shared::lightmap_t bake = bake_for(map, solve);
    assert(!bake.charts.empty());

    const std::vector<shared::baked_light_t> lights = shared::collect_lights(map);
    const shared::baked_light_t* residual = nullptr;
    for (const shared::baked_light_t& light : lights)
      if (light.light.position.y == 128.f) residual = &light;
    assert(residual);

    const int gutter = bake.settings.gutter_in_texels;
    int lit_texels = 0;
    for (const shared::lightmap_chart_t& chart : bake.charts)
      for (int texel_y = 0; texel_y < shared::chart_covered_height(chart, bake.settings); ++texel_y)
        for (int texel_x = 0; texel_x < shared::chart_covered_width(chart, bake.settings); ++texel_x)
        {
          const shared::texel_sample_t sample = shared::sample_texel(chart, texel_x, texel_y);
          if (!sample.on_surface) continue;

          const shared::light_arrival_t arrival =
              shared::arrival_at(residual->light, sample.position, sample.normal,
                                 solve.directional_shadow_distance);
          const linalg::vec3 expected =
              arrival.reaches
                  ? residual->light.radiance * (arrival.attenuation * arrival.normal_dot_light)
                  : linalg::vec3{0.f, 0.f, 0.f};
          const linalg::vec3 actual = bake.irradiance_pages.load(
              chart.page, chart.atlas_rect.min_x + gutter + texel_x,
              chart.atlas_rect.min_y + gutter + texel_y);

          const float tolerance = 1e-4f + 0.01f * std::max(expected.x, actual.x);
          assert(std::abs(expected.x - actual.x) < tolerance);
          assert(std::abs(expected.y - actual.y) < tolerance);
          assert(std::abs(expected.z - actual.z) < tolerance);
          if (actual.x > 0.f) ++lit_texels;
        }

    assert(std::abs(lit_texels - expected_lit) <= lit_tolerance);
  };

  // The brush's top face is exactly 32x32 texels; the mesh's is the same area
  // on whatever grid xatlas gave it.
  check(brush_map, 32 * 32, 0);
  check(mesh_map, 32 * 32, 100);
}

// Step 4: the draw copy is built from the stored unwrap -- the asset's vertices
// by xref, the atlas position from the chart's placed rect -- and keeps the
// source's faces in the source's order.
void a_lightmapped_static_mesh_draws_through_its_unwrap()
{
  const shared::map_t map = map_with_a_box_mesh();

  shared::lightmap_t lightmap;
  lightmap.charts = shared::build_lightmap_charts(map, lightmap.settings);
  lightmap.atlas = shared::pack_lightmap_charts(lightmap.charts, lightmap.settings);
  shared::set_lightmap_geometry_id(lightmap);
  assert(lightmap.atlas.page_count == 1);

  const shared::static_mesh_geometry_t& box =
      std::get<shared::static_mesh_geometry_t>(map.geometry[0].value);
  const assets::mesh_asset_t* source = assets::get(shared::resolve_surface_mesh(box.surface));
  assert(source);
  const assets::mesh_asset_t mesh =
      shared::generate_lightmapped_static_mesh(box, {&lightmap, map.geometry[0].uid});

  // Same faces, in the same order, so every submesh range still holds; fewer
  // vertices than corners, because a chart's vertices are shared inside it.
  assert(mesh.indices.size() == 36);
  assert(mesh.vertices.size() >= 8 && mesh.vertices.size() <= 36);
  assert(mesh.lightmap.size() == mesh.vertices.size());
  assert(mesh.submeshes.size() == source->submeshes.size());

  const std::vector<shared::world_triangle_t> faces = shared::static_mesh_world_triangles(box);
  for (size_t face = 0; face < faces.size(); ++face)
    for (uint32_t corner = 0; corner < 3; ++corner)
    {
      const linalg::vec3& position = mesh.vertices[mesh.indices[face * 3 + corner]].position;
      assert(linalg::length(position - faces[face].corners[corner]) < 1e-3f);
    }

  // Every vertex's atlas position is inside the covered rect of one of this
  // object's placed charts.
  const float size = (float)lightmap.atlas.size_in_texels;
  const int gutter = lightmap.settings.gutter_in_texels;
  for (const shared::vertex_lightmap_t& vertex : mesh.lightmap)
  {
    assert(vertex.uv.z == 0.f);
    bool inside_a_chart = false;
    for (const shared::lightmap_chart_t& chart : lightmap.charts)
    {
      const float min_u = (float)(chart.atlas_rect.min_x + gutter) / size;
      const float min_v = (float)(chart.atlas_rect.min_y + gutter) / size;
      const float max_u = min_u + (float)shared::chart_covered_width(chart, lightmap.settings) / size;
      const float max_v = min_v + (float)shared::chart_covered_height(chart, lightmap.settings) / size;
      if (vertex.uv.x >= min_u - 1e-4f && vertex.uv.x <= max_u + 1e-4f &&
          vertex.uv.y >= min_v - 1e-4f && vertex.uv.y <= max_v + 1e-4f)
        inside_a_chart = true;
    }
    assert(inside_a_chart);
  }

  // Without a bake the copy is still the world-space mesh, with no lightmap
  // array: it uploads byte for byte what it always did.
  const assets::mesh_asset_t unlit = shared::generate_lightmapped_static_mesh(box, {});
  assert(unlit.vertices.size() == 36);
  assert(unlit.lightmap.empty());
}

// Step 3: the unwrap is the one thing about a chart that cannot be re-derived,
// so it is what the sidecar bump carries.
void a_sidecar_round_trips_an_unwrap()
{
  const shared::map_t map = map_with_a_box_mesh();
  shared::lightmap_t baked = pack_for(map);
  shared::set_lightmap_geometry_id(baked);
  assert(!baked.charts.empty());

  const std::filesystem::path directory =
      std::filesystem::temp_directory_path() / "tilde_lightmap_test";
  std::filesystem::create_directories(directory);
  const std::string map_path = (directory / "round_trip_unwrap.source").generic_string();

  const uint32_t hash = shared::compute_map_content_hash(map);
  shared::save_lightmap_sidecar(map_path, baked, hash);
  const shared::lightmap_t loaded = shared::load_lightmap_sidecar(map_path, hash);

  assert(loaded.charts.size() == baked.charts.size());
  for (size_t i = 0; i < loaded.charts.size(); ++i)
  {
    const shared::chart_unwrap_t& before = baked.charts[i].unwrap;
    const shared::chart_unwrap_t& after = loaded.charts[i].unwrap;
    assert(!after.empty());
    assert(after.vertices.size() == before.vertices.size());
    for (size_t v = 0; v < after.vertices.size(); ++v)
    {
      assert(after.vertices[v].xref == before.vertices[v].xref);
      assert(after.vertices[v].uv.x == before.vertices[v].uv.x);
      assert(after.vertices[v].uv.y == before.vertices[v].uv.y);
    }
    assert(after.indices == before.indices);
    assert(after.faces == before.faces);
  }
  assert(loaded.geometry_id == baked.geometry_id);
}

void a_static_mesh_casts_a_shadow_in_the_bake()
{
  const shared::map_t map = map_with_a_box_mesh();
  const Bounding_Volume_Hierarchy occluders = shared::build_occluder_bvh(map);

  // Straight up through the box from underneath: blocked. Beside it: not.
  assert(!shared::shadow_ray_reaches(occluders, {0.f, -100.f, 0.f}, {0.f, 1.f, 0.f},
                                     {0.f, 1.f, 0.f}, 200.f, 0.5f));
  assert(shared::shadow_ray_reaches(occluders, {100.f, -100.f, 0.f}, {0.f, 1.f, 0.f},
                                    {0.f, 1.f, 0.f}, 200.f, 0.5f));

  // A texel on the box's own top face, looking up, sees the sky: the mesh is
  // its triangles and not its bound, or this ray would start inside a solid.
  assert(shared::shadow_ray_reaches(occluders, {0.f, 64.f, 0.f}, {0.f, 1.f, 0.f},
                                    {0.f, 1.f, 0.f}, 200.f, 0.5f));
}

// --- The GPU scene: lightmap_gpu_plan.md step 2 ------------------------------

// The pin between the two scenes: for every triangle the GPU scene holds, a ray
// dropped onto its centroid through the occluder BVH must land on it, and what
// surface_at resolves there through the brush-and-face walk must be what the
// triangle's record says it is made of. Textured and untextured brush faces, a
// face naming an index the table does not have, and a static mesh -- every
// answer surface_at can give.
void the_gpu_scene_is_made_of_what_the_tracer_sees()
{
  shared::map_t map;
  map.materials = {"", ""};

  const shared::entity_uid_t floor_uid = map.next_uid++;
  map.geometry.push_back({floor_uid, shared::make_box_brush({0, -8, 0}, {64, 8, 64})});
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({0, 72, 0}, {64, 8, 64})});

  shared::static_mesh_geometry_t prop;
  prop.surface.mesh_path = "resources/models/Box.mesh";
  prop.position = {200, 40, 0};
  prop.scale = {32, 32, 32};
  const shared::entity_uid_t prop_uid = map.next_uid++;
  map.geometry.push_back({prop_uid, prop});

  // The floor's top face names material 1, its bottom an index the table does
  // not have; everything else stays at the map default.
  shared::brush_geometry_t& floor =
      std::get<shared::brush_geometry_t>(map.geometry[0].value);
  shared::face_surface_for(floor, Plane{{0, 0, 0}, {0, 1, 0}}).material = 1;
  shared::face_surface_for(floor, Plane{{0, -16, 0}, {0, -1, 0}}).material = 7;

  const assets::texture_asset_t albedo = one_texel(255, 255, 255);
  const assets::texture_asset_t emissive = one_texel(255, 128, 0);

  const Bounding_Volume_Hierarchy bvh = shared::build_occluder_bvh(map);
  shared::traced_scene_t traced = shared::build_traced_scene(map, bvh);
  traced.materials = {{nullptr, nullptr}, {&albedo, &emissive}};

  const shared::gpu_bake_scene_t scene = shared::build_gpu_bake_scene(map, traced);

  // Two boxes of twelve triangles and a box mesh of twelve, one vertex a corner.
  assert(scene.triangles.size() == 36);
  assert(scene.vertices.size() == 108);
  assert(scene.indices.size() == 108);
  assert(scene.triangle_object_uids.size() == 36);

  // Two map materials and the trailing untextured one; two textures, because
  // only material 1 names any.
  assert(scene.materials.size() == 3);
  assert(scene.untextured_material == 2);
  assert(scene.materials[0].albedo_texture == shared::GPU_NO_TEXTURE);
  assert(scene.materials[0].emissive_texture == shared::GPU_NO_TEXTURE);
  assert(scene.materials[1].albedo_texture == 0);
  assert(scene.materials[1].emissive_texture == 1);
  assert(scene.textures.size() == 2);
  assert(scene.textures[0] == &albedo && scene.textures[1] == &emissive);

  int by_material[3] = {0, 0, 0};
  int prop_triangles = 0;

  for (uint32_t index = 0; index < (uint32_t)scene.triangles.size(); ++index)
  {
    const shared::gpu_triangle_t& triangle = scene.triangles[index];
    assert(triangle.material < scene.materials.size());
    ++by_material[triangle.material];
    if (scene.triangle_object_uids[index] == prop_uid)
    {
      ++prop_triangles;
      assert(triangle.material == scene.untextured_material);
    }

    const auto corner = [&](uint32_t offset) {
      const linalg::vec4& vertex = scene.vertices[scene.indices[index * 3 + offset]];
      return linalg::vec3{vertex.x, vertex.y, vertex.z};
    };
    const linalg::vec3 a = corner(0);
    const linalg::vec3 b = corner(1);
    const linalg::vec3 c = corner(2);
    const linalg::vec3 centroid = (a + b + c) * (1.f / 3.f);
    const linalg::vec3 normal = linalg::normalize(linalg::cross(b - a, c - a));

    // Dropped onto the triangle from just outside it.
    constexpr float HEIGHT = 2.f;
    const linalg::vec3 origin = centroid + normal * HEIGHT;
    ray_hit_result_t hit = {};
    assert(bvh_intersect_ray(bvh, origin, normal * -1.f, hit) && hit.hit);
    assert(std::abs(hit.t - HEIGHT) < 1e-2f);
    assert(hit.id.index == scene.triangle_object_uids[index]);
    assert(linalg::dot(hit.normal, normal) > 0.999f);

    const linalg::vec3 hit_position = origin + normal * (-hit.t);
    const shared::traced_surface_t cpu = shared::surface_at(traced, hit, hit_position);
    const shared::traced_surface_t gpu =
        shared::gpu_surface_at(scene, index, shared::gpu_triangle_uv_at(scene, index, centroid));

    // Exact, not close: both answers come out of the one sample_texture.
    for (int channel = 0; channel < 3; ++channel)
    {
      assert(cpu.albedo[channel] == gpu.albedo[channel]);
      assert(cpu.emission[channel] == gpu.emission[channel]);
    }
  }

  // The floor's top face is the only textured one; its bottom face fell through
  // to the untextured entry beside the prop's twelve.
  assert(by_material[1] == 2);
  assert(by_material[2] == 2 + 12);
  assert(by_material[0] == 36 - 2 - 14);
  assert(prop_triangles == 12);

  // A textured triangle's uv reaches the record: the top face is a 128-unit
  // square at the 128-unit default repeat, so its corners span one repeat.
  bool saw_textured = false;
  for (uint32_t index = 0; index < (uint32_t)scene.triangles.size(); ++index)
  {
    const shared::gpu_triangle_t& triangle = scene.triangles[index];
    if (triangle.material != 1) continue;
    saw_textured = true;
    const float span_u = std::max({triangle.uv0.x, triangle.uv1.x, triangle.uv2.x}) -
                         std::min({triangle.uv0.x, triangle.uv1.x, triangle.uv2.x});
    const float span_v = std::max({triangle.uv0.y, triangle.uv1.y, triangle.uv2.y}) -
                         std::min({triangle.uv0.y, triangle.uv1.y, triangle.uv2.y});
    assert(std::abs(span_u - 1.f) < 1e-4f && std::abs(span_v - 1.f) < 1e-4f);
  }
  assert(saw_textured);
}

// A texture reaches the GPU scene at its OWN size, and is read through the
// tracer's own fetch: a 512-wide stripe of alternating black and white texels
// answers black at one texel and white at the next, where the one-size resample
// this replaced averaged the pair to a grey. Wrapping folds both ways as
// sample_texture does, and one asset named twice is one entry.
void a_texture_reaches_the_gpu_scene_at_its_own_size()
{
  constexpr int WIDTH = 512;
  assets::texture_asset_t texture;
  texture.width = WIDTH;
  texture.height = 1;
  texture.channels = 4;
  texture.pixels.assign((size_t)WIDTH * 4, 255);
  for (int x = 0; x < WIDTH; x += 2)
    texture.pixels[(size_t)x * 4] = texture.pixels[(size_t)x * 4 + 1] =
        texture.pixels[(size_t)x * 4 + 2] = 0;

  shared::map_t map;
  const Bounding_Volume_Hierarchy bvh = shared::build_occluder_bvh(map);
  shared::traced_scene_t traced = shared::build_traced_scene(map, bvh);
  traced.materials = {{&texture, &texture}};

  shared::gpu_bake_scene_t scene = shared::build_gpu_bake_scene(map, traced);
  assert(scene.textures.size() == 1 && scene.textures[0] == &texture);
  assert(scene.materials[0].albedo_texture == 0 && scene.materials[0].emissive_texture == 0);

  // An empty map has no triangle to ask through, so give the scene one naming
  // material 0.
  scene.triangles.push_back({0, 0, {0.f, 0.f}, {1.f, 0.f}, {0.f, 1.f}});
  const auto albedo_at = [&](float u) {
    return shared::gpu_surface_at(scene, 0, {u, 0.5f}).albedo;
  };
  const float texel = 1.f / (float)WIDTH;

  assert(albedo_at(0.5f * texel).x == 0.f);
  assert(albedo_at(1.5f * texel).x > 0.999f);
  assert(albedo_at(-0.5f * texel).x > 0.999f);
  assert(albedo_at(1.f + 0.5f * texel).x == 0.f);

  const linalg::vec3 through_the_tracer =
      shared::sample_texture(texture, {1.5f * texel, 0.5f}, {0.f, 0.f, 0.f});
  const linalg::vec3 through_the_scene = albedo_at(1.5f * texel);
  for (int channel = 0; channel < 3; ++channel)
    assert(through_the_scene[channel] == through_the_tracer[channel]);
}

// --- Step 4's pin, the CPU half -----------------------------------------------

// A floor under a ceiling: every record on the floor's top face looks up and
// hits the ceiling's underside at the gap less the bias, and every record on
// its bottom face looks down into nothing. The report is then shown a candidate
// that is deliberately wrong in both ways, because a report that cannot see a
// difference would pass the GPU whatever it answered.
void probe_rays_hit_what_the_bvh_says_they_hit()
{
  const shared::map_t map = map_with_a_floor_a_ceiling_and_a_light_between_them();
  const shared::lightmap_t lightmap = pack_for(map);

  shared::lightmap_solve_settings_t solve;
  solve.samples_per_texel_edge = 1;
  const Bounding_Volume_Hierarchy bvh = shared::build_occluder_bvh(map);
  const std::vector<shared::gpu_sample_t> samples =
      shared::collect_lightmap_samples(lightmap, solve, bvh);
  assert(!samples.empty());

  const float bias = 0.25f;
  const std::vector<float> distances =
      shared::probe_ray_distances(bvh, samples, bias, 100000.f);
  assert(distances.size() == samples.size());

  // The floor's top is y = 0 and the ceiling's underside y = 64.
  size_t upward = 0;
  size_t downward = 0;
  for (size_t i = 0; i < samples.size(); ++i)
  {
    const shared::gpu_sample_t &sample = samples[i];
    if (sample.normal.y > 0.9f && sample.position.y < 1.f)
    {
      ++upward;
      assert(std::abs(distances[i] - (64.f - bias)) < 1e-2f);
    }
    if (sample.normal.y < -0.9f && sample.position.y < -1.f)
    {
      ++downward;
      assert(distances[i] < 0.f);
    }
  }
  assert(upward > 0 && downward > 0);

  const shared::probe_ray_report_t same = shared::compare_probe_rays(distances, distances, 0.1f);
  assert(same.agrees());
  assert(same.sample_count == samples.size());
  assert(same.both_hit + same.both_missed == samples.size());
  assert(same.largest_distance_error == 0.f);

  std::vector<float> wrong = distances;
  size_t flipped = 0;
  size_t moved = 0;
  for (size_t i = 0; i < wrong.size() && (flipped == 0 || moved == 0); ++i)
  {
    if (wrong[i] < 0.f && flipped == 0)
    {
      wrong[i] = 10.f;
      flipped = i + 1;
    }
    else if (wrong[i] >= 0.f && moved == 0)
    {
      wrong[i] += 1.f;
      moved = i + 1;
    }
  }
  assert(flipped != 0 && moved != 0);

  const shared::probe_ray_report_t differs = shared::compare_probe_rays(distances, wrong, 0.1f);
  assert(!differs.agrees());
  assert(differs.candidate_only_hit == 1 && differs.reference_only_hit == 0);
  assert(differs.hits_outside_tolerance == 1);
  assert(differs.candidate_farther == 1 && differs.candidate_nearer == 0);
  assert(differs.worst_sample == (int64_t)(moved - 1));
  assert(std::abs(differs.largest_distance_error - 1.f) < 1e-4f);

  // A reference of exactly zero is a ray that started inside a solid: its own
  // category, not a disagreement, whatever the candidate answered there.
  std::vector<float> buried = distances;
  buried[moved - 1] = 0.f;
  const shared::probe_ray_report_t inside = shared::compare_probe_rays(buried, wrong, 0.1f);
  assert(inside.reference_started_inside_a_solid == 1);
  assert(inside.hits_outside_tolerance == 0);
  assert(inside.worst_sample == -1 || inside.largest_distance_error == 0.f);
}

// A wall standing IN a floor: the wall's side face runs on below the floor's
// top, and the texels down there are buried inside the floor's solid. They used
// to bake black -- the BVH answers an origin inside a solid as a hit at zero,
// which every shadow ray reads as occluded -- and bilinear filtering dragged that
// black up the visible wall. A buried sample is no surface and is dropped at
// collection, so the buried strip is filled from the exposed strip above it and
// reads exactly what it reads.
// Step 5's pin, the half of it that needs no device: the paired test that reads
// two solvers' answers over one record list. Identical answers agree; a bias on
// one chart is that chart, first, beyond tolerance; a difference below the
// relative floor is float noise and passes however small its spread.
void the_indirect_comparison_flags_a_bias_and_passes_itself()
{
  constexpr size_t RECORDS_PER_CHART = 200;
  std::vector<shared::gpu_sample_t> samples;
  std::vector<shared::indirect_sh_l1_t> reference;
  const size_t charts[] = {4, 9, 17};
  for (uint32_t chart = 0; chart < 3; ++chart)
    for (uint32_t i = 0; i < RECORDS_PER_CHART; ++i)
    {
      shared::gpu_sample_t sample;
      sample.chart_index = chart;
      sample.seed = shared::sample_hash((int)i, (int)chart, 0, 0);
      samples.push_back(sample);

      // Noisy around a per-chart mean, the way chains are.
      shared::indirect_sh_l1_t value;
      const float noise = shared::unit_float_from(sample.seed) - 0.5f;
      value.l0 = {1.f + 0.2f * (float)chart + noise, 0.5f + noise * 0.5f, 0.25f};
      value.l1[0] = {noise, 0.f, 0.f};
      value.l1[1] = {0.f, 0.1f + noise * 0.1f, 0.f};
      value.l1[2] = {0.f, 0.f, -0.2f};
      reference.push_back(value);
    }

  const shared::record_comparison_report_t same = shared::compare_indirect_results(
      samples, Span<const size_t>(charts), reference, reference);
  assert(same.agrees());
  assert(same.record_count == samples.size());
  assert(same.chart_count == 3);
  assert(same.charts.size() == 3);
  assert(same.mean_absolute_difference_over(0, 3) == 0.f);
  assert(same.reference_mean_over(0, 3) > 0.5f);
  for (const shared::record_chart_comparison_t &chart : same.charts)
  {
    assert(chart.record_count == RECORDS_PER_CHART);
    assert(chart.largest_sigma == 0.f);
    assert(chart.largest_sigma_coefficient == -1);
  }

  // A constant bias on chart 9's L0.g: every record moves the same way, the
  // spread of the differences is zero, and the sigma is unbounded.
  std::vector<shared::indirect_sh_l1_t> biased = reference;
  for (size_t i = 0; i < samples.size(); ++i)
    if (samples[i].chart_index == 1) biased[i].l0.y += 0.1f;

  const shared::record_comparison_report_t bias = shared::compare_indirect_results(
      samples, Span<const size_t>(charts), reference, biased);
  assert(!bias.agrees());
  assert(bias.charts_beyond_tolerance == 1);
  assert(bias.charts.front().chart == 9);
  assert(bias.charts.front().largest_sigma_coefficient == 1);
  assert(bias.charts.front().largest_sigma > shared::RECORD_COMPARISON_SIGMA);
  assert(std::abs(bias.charts.front().candidate_mean[1] - bias.charts.front().reference_mean[1] -
                  0.1f) < 1e-4f);
  assert(bias.charts[1].largest_sigma == 0.f && bias.charts[2].largest_sigma == 0.f);

  // One ulp on every record of one chart: a standard error of zero and a mean
  // difference of nothing, which the relative floor reads as agreement.
  std::vector<shared::indirect_sh_l1_t> nudged = reference;
  for (size_t i = 0; i < samples.size(); ++i)
    if (samples[i].chart_index == 2) nudged[i].l0.x += 1e-6f;
  const shared::record_comparison_report_t noise = shared::compare_indirect_results(
      samples, Span<const size_t>(charts), reference, nudged);
  assert(noise.agrees());
  assert(noise.mean_absolute_difference_over(0, 3) > 0.f);

  // One record with a real difference is beyond tolerance too: nothing to
  // average against, so it is taken at its word.
  std::vector<shared::gpu_sample_t> one_sample(1);
  one_sample[0].chart_index = 0;
  const size_t one_chart[] = {2};
  std::vector<shared::indirect_sh_l1_t> one_reference(1);
  one_reference[0].l0 = {1.f, 1.f, 1.f};
  std::vector<shared::indirect_sh_l1_t> one_candidate = one_reference;
  one_candidate[0].l0.z = 1.5f;
  const shared::record_comparison_report_t lone = shared::compare_indirect_results(
      one_sample, Span<const size_t>(one_chart), one_reference, one_candidate);
  assert(!lone.agrees());
  assert(lone.charts.size() == 1 && lone.charts[0].chart == 2 &&
         lone.charts[0].largest_sigma_coefficient == 2);
}

// Step 6's pin, the half that needs no device: the direct term's answers -- an
// irradiance, then a coverage and a weight per light -- through the same paired
// test. A bias on one light's coverage names that coefficient; a coverage that
// is zero everywhere on both sides is agreement, not a floor of zero failing on
// nothing.
void the_direct_comparison_flags_a_bias_per_light_and_names_it()
{
  constexpr size_t RECORDS_PER_CHART = 150;
  constexpr size_t LIGHT_COUNT = 3;
  std::vector<shared::gpu_sample_t> samples;
  shared::gpu_direct_results_t reference;
  const size_t charts[] = {2, 5};
  for (uint32_t chart = 0; chart < 2; ++chart)
    for (uint32_t i = 0; i < RECORDS_PER_CHART; ++i)
    {
      shared::gpu_sample_t sample;
      sample.chart_index = chart;
      sample.seed = shared::sample_hash((int)i, (int)chart, 1, 0);
      samples.push_back(sample);
    }
  reference.resize(samples.size(), LIGHT_COUNT);
  for (size_t i = 0; i < samples.size(); ++i)
  {
    const float noise = shared::unit_float_from(samples[i].seed) - 0.5f;
    reference.irradiance[i] = {2.f + noise, 1.f + noise * 0.5f, 0.5f};
    reference.coverage[i * LIGHT_COUNT + 0] = 1.f;
    reference.coverage[i * LIGHT_COUNT + 1] = noise > 0.f ? 1.f : 0.5f;
    reference.coverage[i * LIGHT_COUNT + 2] = 0.f;
    reference.weight[i * LIGHT_COUNT + 0] = 3.f + noise;
    reference.weight[i * LIGHT_COUNT + 1] = 0.25f;
    reference.weight[i * LIGHT_COUNT + 2] = 0.f;
  }

  const shared::record_comparison_report_t same =
      shared::compare_direct_results(samples, Span<const size_t>(charts), reference, reference);
  assert(same.agrees());
  assert(same.coefficient_count == 3 + 2 * LIGHT_COUNT);
  assert(same.charts.size() == 2);
  assert(same.group_scale.size() == 3);
  assert(same.group_scale[0] > 0.f && same.group_scale[1] > 0.f && same.group_scale[2] > 0.f);
  assert(same.mean_absolute_difference_over(0, same.coefficient_count) == 0.f);
  assert(same.largest_absolute_difference_over(0, same.coefficient_count) == 0.f);
  assert(same.reference_nonzero_records == samples.size());
  assert(same.differing_records == 0);
  for (const shared::record_chart_comparison_t &chart : same.charts)
  {
    assert(chart.record_count == RECORDS_PER_CHART);
    assert(chart.reference_mean.size() == same.coefficient_count);
    assert(chart.largest_sigma == 0.f && chart.largest_sigma_coefficient == -1);
  }

  // Light 1's coverage on chart 5 moved on every record: the flagged
  // coefficient is coverage[1], and it is named as such.
  shared::gpu_direct_results_t biased = reference;
  for (size_t i = 0; i < samples.size(); ++i)
    if (samples[i].chart_index == 1) biased.coverage[i * LIGHT_COUNT + 1] -= 0.1f;
  const shared::record_comparison_report_t bias =
      shared::compare_direct_results(samples, Span<const size_t>(charts), reference, biased);
  assert(!bias.agrees());
  assert(bias.charts_beyond_tolerance == 1);
  assert(bias.differing_records == RECORDS_PER_CHART);
  assert(std::abs(bias.largest_absolute_difference_over(3, LIGHT_COUNT) - 0.1f) < 1e-6f);
  assert(bias.largest_absolute_difference_over(0, 3) == 0.f);
  assert(bias.charts.front().chart == 5);
  assert(bias.charts.front().largest_sigma_coefficient == 3 + 1);
  assert(bias.charts.front().largest_sigma > shared::RECORD_COMPARISON_SIGMA);
  assert(bias.charts[1].largest_sigma == 0.f);

  char storage[shared::DIRECT_COEFFICIENT_NAME_CAPACITY];
  assert(shared::direct_coefficient_name(0, LIGHT_COUNT, Span<char>(storage)) == "irradiance.r");
  assert(shared::direct_coefficient_name(2, LIGHT_COUNT, Span<char>(storage)) == "irradiance.b");
  assert(shared::direct_coefficient_name(4, LIGHT_COUNT, Span<char>(storage)) == "coverage[1]");
  assert(shared::direct_coefficient_name(8, LIGHT_COUNT, Span<char>(storage)) == "weight[2]");

  // A weight of one ulp more on every record of chart 2: below the weight
  // group's floor, so noise, whatever the zero standard error says.
  shared::gpu_direct_results_t nudged = reference;
  for (size_t i = 0; i < samples.size(); ++i)
    if (samples[i].chart_index == 0) nudged.weight[i * LIGHT_COUNT + 1] += 1e-6f;
  const shared::record_comparison_report_t noise =
      shared::compare_direct_results(samples, Span<const size_t>(charts), reference, nudged);
  assert(noise.agrees());
  assert(noise.mean_absolute_difference_over(3 + LIGHT_COUNT, LIGHT_COUNT) > 0.f);
}

// The picture the comparison writes: every record's L0 averaged onto the texel it
// came from, and nothing else touched -- no gutter, no ranking. The sample set is
// the bake's own records with their origins kept, so a record lands on the texel
// the bake would have reduced it into.
void the_indirect_l0_pages_are_the_records_reduced_by_texel()
{
  const shared::map_t map = map_with_a_floor_a_ceiling_and_a_light_between_them();
  const shared::lightmap_t lightmap = pack_for(map);

  shared::lightmap_solve_settings_t solve;
  solve.samples_per_texel_edge = 2;
  const Bounding_Volume_Hierarchy bvh = shared::build_occluder_bvh(map);
  const shared::lightmap_sample_set_t set =
      shared::collect_lightmap_sample_set(lightmap, solve, bvh);
  assert(!set.samples.empty());
  assert(set.origins.size() == set.samples.size());
  assert(set.charts.size() == lightmap.charts.size());
  for (size_t i = 0; i < set.charts.size(); ++i) assert(set.charts[i] == i);

  // The same list the plain collector hands out, record for record.
  const std::vector<shared::gpu_sample_t> plain =
      shared::collect_lightmap_samples(lightmap, solve, bvh);
  assert(plain.size() == set.samples.size());
  for (size_t i = 0; i < plain.size(); ++i)
    assert(plain[i].seed == set.samples[i].seed &&
           plain[i].chart_index == set.samples[i].chart_index);

  // Every record answers the same constant, so a texel with any record holds it
  // exactly (1, 2 and 3 are exact in RGB9E5) and a texel with none stays black.
  const std::vector<linalg::vec3> results(set.samples.size(), linalg::vec3{1.f, 2.f, 3.f});

  const shared::lightmap_pages_t pages =
      shared::reduce_record_values_to_pages(lightmap, set, results);
  assert(pages.page_count == lightmap.atlas.page_count);
  assert(pages.size_in_texels == lightmap.atlas.size_in_texels);

  std::vector<uint8_t> touched(pages.texel_count(), 0);
  const int gutter = lightmap.settings.gutter_in_texels;
  for (size_t i = 0; i < set.samples.size(); ++i)
  {
    const shared::lightmap_chart_t &chart = lightmap.charts[set.charts[set.samples[i].chart_index]];
    const int x = chart.atlas_rect.min_x + gutter + set.origins[i].texel_x;
    const int y = chart.atlas_rect.min_y + gutter + set.origins[i].texel_y;
    touched[((size_t)chart.page * (size_t)pages.size_in_texels + (size_t)y) *
                (size_t)pages.size_in_texels +
            (size_t)x] = 1;
  }

  size_t lit = 0;
  for (int page = 0; page < pages.page_count; ++page)
    for (int y = 0; y < pages.size_in_texels; ++y)
      for (int x = 0; x < pages.size_in_texels; ++x)
      {
        const linalg::vec3 texel = pages.load(page, x, y);
        const bool was_touched =
            touched[((size_t)page * (size_t)pages.size_in_texels + (size_t)y) *
                        (size_t)pages.size_in_texels +
                    (size_t)x] != 0;
        if (was_touched)
        {
          ++lit;
          assert(texel.x == 1.f && texel.y == 2.f && texel.z == 3.f);
        }
        else
        {
          assert(texel.x == 0.f && texel.y == 0.f && texel.z == 0.f);
        }
      }
  assert(lit > 0);

  // |a - a| is black everywhere; |a - 0| is a again.
  const shared::lightmap_pages_t nothing = shared::absolute_difference_pages(pages, pages);
  shared::lightmap_pages_t black;
  black.allocate(lightmap.atlas, shared::lightmap_pixel_format_t::Rgb9e5);
  const shared::lightmap_pages_t itself = shared::absolute_difference_pages(pages, black);
  for (int page = 0; page < pages.page_count; ++page)
    for (int y = 0; y < pages.size_in_texels; ++y)
      for (int x = 0; x < pages.size_in_texels; ++x)
      {
        const linalg::vec3 zero = nothing.load(page, x, y);
        assert(zero.x == 0.f && zero.y == 0.f && zero.z == 0.f);
        const linalg::vec3 same = itself.load(page, x, y);
        const linalg::vec3 original = pages.load(page, x, y);
        assert(same.x == original.x && same.y == original.y && same.z == original.z);
      }
}

void a_texel_buried_in_a_neighbouring_brush_reads_as_its_exposed_neighbour()
{
  shared::map_t map;
  map.geometry.push_back(
      {map.next_uid++, shared::make_box_brush({0, -32, 0}, {128, 32, 128})});
  const shared::entity_uid_t wall = map.next_uid;
  map.geometry.push_back({map.next_uid++, shared::make_box_brush({0, 0, 0}, {8, 64, 8})});

  std::shared_ptr<entities::Point_Light_Entity> light =
      std::make_shared<entities::Point_Light_Entity>();
  light->position = {400, 200, 0};
  light->range = 4096.f;
  light->light.color = {1.f, 1.f, 1.f};
  light->light.intensity = 100.f;
  map.entities.push_back({map.next_uid++, light});

  const shared::lightmap_t lightmap = bake_for(map);
  assert(lightmap.light_uids.size() == 1);

  const shared::lightmap_chart_t *face = nullptr;
  for (const shared::lightmap_chart_t &chart : lightmap.charts)
    if (chart.object_uid == wall && chart.plane.normal.x > 0.9f) face = &chart;
  assert(face && face->page >= 0);
  assert(face->light_slots[0] == 0);

  const int gutter = lightmap.settings.gutter_in_texels;
  const int width = shared::chart_covered_width(*face, lightmap.settings);
  const int height = shared::chart_covered_height(*face, lightmap.settings);
  size_t buried = 0;
  size_t exposed = 0;
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
    {
      const linalg::vec2 chart_space = {((float)x + 0.5f) * face->world_units_per_texel,
                                        ((float)y + 0.5f) * face->world_units_per_texel};
      const shared::texel_sample_t sample = shared::sample_chart(*face, chart_space);
      if (!sample.on_surface) continue;
      // The row straddling the floor's top is a mix by construction; skip it.
      if (sample.position.y > -4.f && sample.position.y < 4.f) continue;

      const Array<float, shared::LIGHTMAP_LIGHTS_PER_CHART> visibility =
          lightmap.visibility_pages.load_visibility(face->page, face->atlas_rect.min_x + gutter + x,
                                                    face->atlas_rect.min_y + gutter + y);
      assert(visibility[0] == 1.f);
      if (sample.position.y < 0.f) ++buried;
      else ++exposed;
    }
  assert(buried > 0 && exposed > 0);
}

// --- The seam: a batched bake is the reference bake (lightmap_gpu_plan.md 3) --

// Everything a bake decides, compared whole: the four page sets, the resolve
// table, every chart's slots, and the debug masks.
void assert_bakes_are_identical(const shared::lightmap_t &reference,
                                const shared::lightmap_t &batched,
                                const shared::lightmap_visibility_masks_t &reference_masks,
                                const shared::lightmap_visibility_masks_t &batched_masks)
{
  assert(!reference.irradiance_pages.bytes.empty());
  assert(reference.irradiance_pages.bytes == batched.irradiance_pages.bytes);
  assert(reference.visibility_pages.bytes == batched.visibility_pages.bytes);
  assert(reference.indirect_l0_pages.bytes == batched.indirect_l0_pages.bytes);
  assert(reference.indirect_l1_pages.bytes == batched.indirect_l1_pages.bytes);
  assert(reference.light_uids == batched.light_uids);

  assert(reference.charts.size() == batched.charts.size());
  for (size_t i = 0; i < reference.charts.size(); ++i)
    for (uint32_t slot = 0; slot < shared::LIGHTMAP_LIGHTS_PER_CHART; ++slot)
      assert(reference.charts[i].light_slots[slot] == batched.charts[i].light_slots[slot]);

  assert(!reference_masks.coverage.empty());
  assert(reference_masks.light_uids == batched_masks.light_uids);
  assert(reference_masks.coverage == batched_masks.coverage);
}

// The batch loop is what a GPU solver will run under, so it is pinned with no
// device: a bake through the CPU shade behind the seam must equal the reference
// bit for bit. Once with every chart in ONE batch, and once with a budget too
// small for two charts, so a batch boundary falls between every pair. Returns
// how many direct dispatches the one-batch bake took, which is where the
// residual dispatch shows.
size_t check_a_batched_bake_against_the_reference(const shared::map_t &map,
                                                  const shared::lightmap_solve_settings_t &solve)
{
  shared::lightmap_visibility_masks_t reference_masks;
  const shared::lightmap_t reference = bake_for(map, solve, &reference_masks);

  size_t packed_charts = 0;
  for (const shared::lightmap_chart_t &chart : reference.charts)
    if (chart.page >= 0) ++packed_charts;
  assert(packed_charts > 1);

  shared::cpu_batch_solver_t whole;
  shared::lightmap_visibility_masks_t whole_masks;
  shared::lightmap_t in_one_batch = pack_for(map);
  shared::bake_lightmap(map, in_one_batch, solve, &whole, &whole_masks);
  assert_bakes_are_identical(reference, in_one_batch, reference_masks, whole_masks);
  assert(whole.statistics().direct_dispatches >= 1 && whole.statistics().direct_dispatches <= 2);

  shared::cpu_batch_solver_t split;
  split.result_budget = 1;
  shared::lightmap_visibility_masks_t split_masks;
  shared::lightmap_t one_chart_per_batch = pack_for(map);
  shared::bake_lightmap(map, one_chart_per_batch, solve, &split, &split_masks);
  assert_bakes_are_identical(reference, one_chart_per_batch, reference_masks, split_masks);
  assert(split.statistics().direct_dispatches >= packed_charts);

  // Splitting moved no work either: the same rays and the same chains.
  assert(split.statistics().shade.direct_rays == whole.statistics().shade.direct_rays);
  assert(split.statistics().shade.chains == whole.statistics().shade.chains);

  return whole.statistics().direct_dispatches;
}

void a_batched_bake_is_the_reference_bake_bit_for_bit()
{
  // A chart that DROPS a light: the residual dispatch, which is the second one.
  shared::lightmap_solve_settings_t supersampled;
  supersampled.samples_per_texel_edge = 2;
  assert(check_a_batched_bake_against_the_reference(
             map_with_a_floor_and_a_residual_light(64.f, 1.f), supersampled) == 2);

  // Visibility mode has no residual: one dispatch, whatever a chart dropped.
  shared::lightmap_solve_settings_t visibility;
  visibility.mode = shared::lightmap_solve_mode_t::Visibility;
  assert(check_a_batched_bake_against_the_reference(
             map_with_a_floor_and_a_residual_light(64.f, 1.f), visibility) == 1);

  // The tracer on: the indirect dispatch beside the direct one.
  check_a_batched_bake_against_the_reference(
      map_with_a_floor_a_ceiling_and_a_light_between_them(), traced_solve_settings());

  // A mesh, whose charts answer sample_chart through triangles rather than a
  // plane, lit as a_box_mesh_bakes_what_the_light_delivers lights it.
  shared::map_t mesh_map = map_with_a_box_mesh();
  {
    std::shared_ptr<entities::Point_Light_Entity> light =
        std::make_shared<entities::Point_Light_Entity>();
    light->position = {0, 128, 0};
    light->range = 1024.f;
    light->light.color = {1.f, 1.f, 1.f};
    light->light.intensity = 1.f;
    mesh_map.entities.push_back({mesh_map.next_uid++, light});
    add_the_four_lights_that_outrank_everything(mesh_map);
  }
  assert(check_a_batched_bake_against_the_reference(mesh_map, {}) == 2);
}

// --- lightmap_gpu_plan.md step 7: the probe half through the seam --------------

// The probe volume a solver bakes is the reference volume BYTE FOR BYTE, with a
// Mixed light beside the Baked one so both a channel and a direct term are
// exercised -- and with the tracer off, no chain is fired on either path, which
// is what pins the chain count being zeroed ONCE in bake_lightmap rather than
// on the probe path alone.
void a_batched_probe_bake_is_the_reference_bake_bit_for_bit()
{
  shared::map_t map = map_with_a_floor_a_ceiling_and_a_light_between_them();
  {
    std::shared_ptr<entities::Point_Light_Entity> mixed =
        std::make_shared<entities::Point_Light_Entity>();
    mixed->position = {32, 40, 16};
    mixed->range = 512.f;
    mixed->light.color = {1.f, 0.5f, 0.25f};
    mixed->light.intensity = 200.f;
    mixed->light.mode = entities::Light_Mode::Mixed;
    mixed->light.source_radius = 4.f;
    map.entities.push_back({map.next_uid++, mixed});
  }

  const auto check = [&](const shared::lightmap_solve_settings_t &solve) {
    const shared::lightmap_t reference = bake_probes_for(map, 16.f, solve);
    assert(!reference.probes.empty());

    shared::cpu_batch_solver_t solver;
    shared::lightmap_t batched = pack_for(map);
    batched.settings.probe_spacing_in_world_units = 16.f;
    shared::bake_lightmap(map, batched, solve, &solver);

    assert(!batched.probes.empty());
    assert(reference.probes.l0_bytes == batched.probes.l0_bytes);
    assert(reference.probes.l1_bytes == batched.probes.l1_bytes);
    assert(reference.probes.visibility_bytes == batched.probes.visibility_bytes);
    for (uint32_t channel = 0; channel < shared::PROBE_VISIBILITY_CHANNELS; ++channel)
      assert(reference.probes.visibility_slots[channel] ==
             batched.probes.visibility_slots[channel]);
    assert(solver.statistics().probe_dispatches == 1);
    return solver.statistics().shade.chains;
  };

  const shared::lightmap_solve_settings_t traced = probe_solve_settings();
  assert(check(traced) > 0);

  shared::lightmap_solve_settings_t untraced = probe_solve_settings();
  untraced.trace_indirect_light = false;
  assert(check(untraced) == 0);
}

// The records a probe bake hands a solver: one per OPEN probe, its grid index
// as chart_index, its hash as the seed, in grid order.
void probe_records_name_their_probe_and_skip_the_buried_ones()
{
  shared::map_t map = map_with_a_floor_a_ceiling_and_a_light_between_them();
  const std::optional<shared::probe_grid_t> grid = shared::try_build_probe_grid(map, 16.f);
  assert(grid);
  const Bounding_Volume_Hierarchy bvh = shared::build_occluder_bvh(map);
  const std::vector<uint8_t> inside = shared::classify_probes_inside_solid(*grid, bvh);

  size_t open = 0;
  for (const uint8_t flag : inside) open += flag == 0;
  const std::vector<shared::gpu_sample_t> samples = shared::collect_probe_samples(*grid, inside);
  assert(samples.size() == open);
  assert(open > 0 && open < grid->probe_count());

  size_t previous = 0;
  for (size_t i = 0; i < samples.size(); ++i)
  {
    const shared::gpu_sample_t &sample = samples[i];
    assert(i == 0 || sample.chart_index > previous);
    previous = sample.chart_index;
    assert(!inside[sample.chart_index]);
    const linalg::vec3i at = grid->coordinates_of(sample.chart_index);
    const linalg::vec3 position = grid->position_of(at);
    assert(sample.position.x == position.x && sample.position.y == position.y &&
           sample.position.z == position.z);
    assert(sample.seed == shared::sample_hash(at.x, at.y, at.z, 0x50524f42));
  }
}

// The comparison over probe answers: sixteen coefficients in three groups, a
// visibility channel biased on one slice flagged and named.
void the_probe_comparison_flags_a_bias_in_a_channel_and_names_it()
{
  constexpr size_t RECORDS_PER_SLICE = 150;
  std::vector<shared::gpu_sample_t> samples;
  std::vector<shared::probe_trace_t> reference;
  const size_t slices[] = {0, 1};
  for (uint32_t slice = 0; slice < 2; ++slice)
    for (uint32_t i = 0; i < RECORDS_PER_SLICE; ++i)
    {
      shared::gpu_sample_t sample;
      sample.chart_index = slice;
      sample.seed = shared::sample_hash((int)i, (int)slice, 0, 0x50524f42);
      samples.push_back(sample);

      const float noise = shared::unit_float_from(sample.seed) - 0.5f;
      shared::probe_trace_t value;
      value.light.l0 = {2.f + noise, 1.f + noise * 0.5f, 0.5f};
      value.light.l1[1] = {0.5f + noise * 0.1f, 0.25f, 0.f};
      value.visibility[0] = noise > 0.f ? 1.f : 0.5f;
      value.visibility[1] = 0.75f;
      reference.push_back(value);
    }

  const shared::record_comparison_report_t same = shared::compare_probe_results(
      samples, Span<const size_t>(slices), reference, reference);
  assert(same.agrees());
  assert(same.coefficient_count == shared::PROBE_COEFFICIENT_COUNT);
  assert(same.charts.size() == 2);
  assert(same.group_scale.size() == 3);
  assert(same.group_scale[2] > 0.f);
  assert(same.reference_nonzero_records == samples.size());
  assert(same.differing_records == 0);

  std::vector<shared::probe_trace_t> biased = reference;
  for (size_t i = 0; i < samples.size(); ++i)
    if (samples[i].chart_index == 1) biased[i].visibility[1] -= 0.1f;
  const shared::record_comparison_report_t bias = shared::compare_probe_results(
      samples, Span<const size_t>(slices), reference, biased);
  assert(!bias.agrees());
  assert(bias.charts_beyond_tolerance == 1);
  assert(bias.differing_records == RECORDS_PER_SLICE);
  assert(bias.charts.front().chart == 1);
  assert(bias.charts.front().largest_sigma_coefficient ==
         (int)shared::SH_L1_COEFFICIENT_COUNT + 1);
  assert(bias.largest_absolute_difference_over(0, shared::SH_L1_COEFFICIENT_COUNT) == 0.f);
  assert(std::abs(bias.largest_absolute_difference_over(shared::SH_L1_COEFFICIENT_COUNT,
                                                        shared::PROBE_VISIBILITY_CHANNELS) -
                  0.1f) < 1e-6f);

  assert(std::string_view(shared::probe_coefficient_name(0)) == "L0.r");
  assert(std::string_view(shared::probe_coefficient_name(6)) == "L1y.r");
  assert(std::string_view(shared::probe_coefficient_name(13)) == "visibility[1]");
}

static assets::asset_state_t g_asset_state{};

int main()
{
  assets::set_state(&g_asset_state);
  assets::mount_asset_source();

  a_static_mesh_is_unwrapped_into_charts();
  a_texel_of_a_mesh_chart_samples_the_mesh_surface();
  a_sphere_unwraps_into_few_charts();
  a_box_mesh_bakes_what_the_light_delivers();
  a_lightmapped_static_mesh_draws_through_its_unwrap();
  a_sidecar_round_trips_an_unwrap();
  a_static_mesh_casts_a_shadow_in_the_bake();

  the_gpu_scene_is_made_of_what_the_tracer_sees();
  a_texture_reaches_the_gpu_scene_at_its_own_size();

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
  the_light_mode_decides_where_a_light_is_evaluated();
  an_occluder_darkens_both_modes();
  a_mask_slot_is_named_by_its_light_and_carries_the_shadow();
  a_mask_is_not_gated_on_the_flat_face_normal();
  asking_for_masks_does_not_move_a_pixel();
  a_source_radius_softens_a_shadow_edge();
  a_punctual_light_spends_no_extra_rays();
  one_shadow_ray_toward_the_disc_averages_to_the_spiral();
  a_source_radius_clamps_the_near_field();
  intensity_is_the_irradiance_at_the_reference_distance();
  the_gutter_is_filled_from_the_chart_that_owns_it();
  supersampling_resolves_a_shadow_edge_the_centre_sample_cannot();
  a_rebake_reproduces_itself_byte_for_byte();
  a_chart_names_its_lights_and_stores_their_coverage();
  a_chart_keeps_its_strongest_lights_and_drops_the_rest();
  the_resolve_table_holds_only_baked_lights();
  a_face_no_light_reaches_keeps_no_slot();
  the_two_modes_store_the_same_visibility();
  a_light_a_chart_drops_becomes_the_residual();
  the_gather_indexes_baked_lights_by_their_slot();
  a_mixed_light_is_in_the_array_twice_and_a_baked_one_once();
  a_slot_no_live_light_claims_carries_no_radiance();
  a_bake_with_no_light_clears_what_the_last_one_left();

  the_srgb_decode_is_the_one_albedo_reads_through();
  nothing_bounces_where_there_is_nothing_to_bounce_off();
  a_ceiling_bounces_light_back_onto_the_floor();
  emission_is_the_emissive_map_and_nothing_else();
  an_emissive_surface_lights_a_room_with_no_lights_in_it();
  the_bounce_does_not_scale_with_the_chain_count();
  the_bounce_knows_which_way_it_came_from();
  the_l1_encoding_does_not_clip_on_a_legal_bake();
  a_sidecar_round_trips_the_bounce();
  an_indirect_rebake_reproduces_itself_byte_for_byte();
  tracing_indirect_light_moves_no_direct_pixel();
  a_spot_light_on_the_floor_lights_the_wall_it_is_aimed_at();
  a_batched_bake_is_the_reference_bake_bit_for_bit();
  probe_rays_hit_what_the_bvh_says_they_hit();
  a_texel_buried_in_a_neighbouring_brush_reads_as_its_exposed_neighbour();
  the_indirect_comparison_flags_a_bias_and_passes_itself();
  the_direct_comparison_flags_a_bias_per_light_and_names_it();
  the_indirect_l0_pages_are_the_records_reduced_by_texel();

  a_probe_grid_pads_the_geometry_by_one_spacing();
  a_probe_grid_snaps_to_the_spacing_and_not_to_the_brush();
  a_probe_inside_a_brush_is_inside_and_one_on_its_face_is_too();
  an_empty_map_has_no_probe_grid();
  a_grid_too_long_for_a_3d_texture_is_refused();

  a_capture_in_a_rectangular_room_measures_the_room();
  a_capture_lattice_snaps_to_the_probe_spacing();
  a_capture_inside_a_solid_is_absent_and_a_pillar_bounds_its_neighbour();
  a_capture_facing_nothing_is_open_on_that_face();
  a_reflection_volume_overrides_the_measured_box();
  the_capture_pick_is_the_nearest_four_weighted_by_distance();
  a_cube_texel_direction_points_into_its_face();
  a_capture_sees_an_emissive_ceiling_directly_and_the_floor_reflects_it();
  a_batched_capture_bake_is_the_reference_bake_bit_for_bit();
  a_sidecar_round_trips_the_reflection_captures();
  identical_capture_answers_agree();
  a_cube_texel_direction_round_trips_to_its_texel();
  a_prefiltered_cube_keeps_mip_zero_and_averages_a_uniform_cube();
  a_prefiltered_cube_spreads_a_bright_texel_over_its_own_hemisphere_only();
  the_environment_brdf_reads_one_and_zero_head_on_and_rises_at_grazing();

  a_probe_reads_a_baked_light_directly_and_knows_where_it_is();
  a_probe_reads_a_mixed_light_through_its_bounce_only();
  a_probe_a_wall_hides_from_the_light_reads_nothing_direct();
  probe_visibility_channels_go_to_the_first_four_mixed_lights();
  a_probe_stores_a_mixed_lights_visibility_and_not_its_light();
  a_probe_bake_carries_a_mixed_lights_visibility_through_the_volume();
  a_probe_sees_the_floor_bounce_from_below();

  a_probe_bake_fills_the_volume_and_dilates_into_solids();
  a_bake_without_probes_clears_the_volume();
  a_sidecar_round_trips_the_probes();
  a_probe_volume_round_trips_its_own_codec();

  a_batched_probe_bake_is_the_reference_bake_bit_for_bit();
  probe_records_name_their_probe_and_skip_the_buried_ones();
  the_probe_comparison_flags_a_bias_in_a_channel_and_names_it();

  std::printf("lightmap_bake_test passed\n");
  return 0;
}
