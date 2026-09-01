// Round-trip + migration test for the geometry exit.
//
// Verifies on the real `maps/test` fixture (which is a PRE-EXIT map: its
// geometry is still written as `entity` blocks with classname aabb_entity /
// displacement_entity, plus one retired wedge_entity):
//   1. Legacy geometry entities convert into map.geometry at load, and land in
//      the geometry list rather than the entity list -- a displacement_entity
//      into a brush with one subdivided face (geometry_def.md Track D).
//   2. Wedge entities are dropped at load.
//   3. Saving the converted map and re-loading it is lossless for geometry, and
//      the saved file no longer contains the legacy form.

#include "../shared/entities/entity_reflection.hpp"
#include "log.hpp"
#include "map.hpp"
#include "network/map_transfer.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>

using namespace shared;

static int fail(const char *msg)
{
  log_error("map_migration_test: {}", msg);
  return 1;
}

template <typename T> static size_t count_of(const map_t &m)
{
  size_t n = 0;
  for (const auto &e : m.entities)
    if (dynamic_cast<const T *>(e.entity.get())) ++n;
  return n;
}

static size_t count_geometry_of_kind(const map_t &m, geometry_kind_t kind)
{
  size_t n = 0;
  for (const map_geometry_t &g : m.geometry)
    if (get_kind(g.value) == kind) ++n;
  return n;
}

// The brushes, in map order. The fixture's legacy aabb_entity blocks convert to
// these -- a box IS a brush since geometry_def.md Track B.
static std::vector<const brush_geometry_t *> get_brushes(const map_t &m)
{
  std::vector<const brush_geometry_t *> out;
  for (const map_geometry_t &g : m.geometry)
    if (const auto *brush = std::get_if<brush_geometry_t>(&g.value))
      out.push_back(brush);
  return out;
}

int main()
{
  const std::string fixture = "maps/test";
  if (!std::filesystem::exists(fixture))
    return fail("fixture 'maps/test' missing — run from project root");

  // --- 1. Load pre-migration fixture --------------------------------------
  std::optional<map_t> loaded_opt = try_load_map(fixture);
  if (!loaded_opt)
    return fail("try_load_map(maps/test) failed");
  map_t &loaded = *loaded_opt;

  const size_t brushes = count_geometry_of_kind(loaded, geometry_kind_t::Brush);
  if (brushes == 0)
    return fail("conversion: expected at least 1 brush from the fixture's "
                "legacy aabb_entity blocks");

  // A displacement is a brush with one SUBDIVIDED FACE now (geometry_def.md
  // Track D), so the fixture's displacement_entity converts into the brush list
  // like everything else -- and what identifies it is the grid it kept.
  size_t subdivided_faces = 0;
  for (const brush_geometry_t *brush : get_brushes(loaded))
    for (const face_surface_t &face : brush->face_surfaces)
      if (face.subdivision_level > 0)
        ++subdivided_faces;

  if (subdivided_faces != 1)
    return fail("conversion: expected the fixture's displacement_entity to "
                "become exactly one subdivided brush face");

  // Geometry must NOT have landed in the entity list — that's the whole point.
  for (const auto &e : loaded.entities)
  {
    const std::string classname = entities::classname_of(e.entity.get());
    if (classname == "aabb_entity" || classname == "displacement_entity" ||
        classname == "static_mesh_entity" || classname == "wedge_entity")
      return fail("conversion: geometry is still in the entity list");
  }

  // Every converted box must carry the legacy volume's half_extents, not a
  // default — a zeroed or default-sized box means the "volume" blob wasn't read.
  // The extents live in the point set now, so they are read back off the bound.
  for (const brush_geometry_t *brush : get_brushes(loaded))
  {
    if (!brush_is_axis_aligned_box(brush->hull_points))
      return fail("conversion: a converted aabb_entity is not a box-shaped brush");

    const aabb_bounds_t bounds = compute_brush_bounds(brush->hull_points);
    const linalg::vec3  h      = (bounds.max - bounds.min) * 0.5f;
    if (h.x == 0.f && h.y == 0.f && h.z == 0.f)
      return fail("conversion: box brush loaded with zeroed half_extents");
    if (h.x == 1.f && h.y == 1.f && h.z == 1.f)
      return fail("conversion: box brush kept the default half_extents — the "
                  "legacy \"volume\" blob was not parsed");
  }

  // A converted grid must be sized for its subdivision level, which is the
  // invariant the fixed-size schema array used to guarantee -- and the brush it
  // sits on must still build a displaced polyhedron, which is what says the grid
  // landed on a QUAD.
  for (const brush_geometry_t *brush : get_brushes(loaded))
  {
    for (const face_surface_t &face : brush->face_surfaces)
    {
      if (face.subdivision_level <= 0)
        continue;
      if (!face_is_subdivided(face))
        return fail("conversion: a subdivided face's grid does not match its "
                    "subdivision level");
      if (face.blend.size() != face.offsets.size())
        return fail("conversion: a subdivided face's blend array is not sized "
                    "for its grid");
    }

    if (!try_build_displaced_polyhedron(*brush))
      return fail("conversion: a converted brush does not build a displaced "
                  "polyhedron");
  }

  // --- 2. Save & re-load --------------------------------------------------
  const std::string out = "maps/test.roundtrip";
  if (!save_map(out, loaded))
    return fail("save_map(maps/test.roundtrip) failed");

  std::optional<map_t> reloaded_opt = try_load_map(out);
  if (!reloaded_opt)
    return fail("try_load_map(maps/test.roundtrip) failed");
  map_t &reloaded = *reloaded_opt;

  // Cleanup the temp file immediately (also any .navmesh sidecar).
  std::filesystem::remove(out);
  std::filesystem::remove(out + ".navmesh");

  // --- 3. Compare counts --------------------------------------------------
  if (count_geometry_of_kind(reloaded, geometry_kind_t::Brush) != brushes)
    return fail("roundtrip: brush count drift");
  {
    size_t reloaded_subdivided_faces = 0;
    for (const brush_geometry_t *brush : get_brushes(reloaded))
      for (const face_surface_t &face : brush->face_surfaces)
        if (face.subdivision_level > 0)
          ++reloaded_subdivided_faces;
    if (reloaded_subdivided_faces != subdivided_faces)
      return fail("roundtrip: subdivided face count drift");
  }
  if (reloaded.geometry.size() != loaded.geometry.size())
    return fail("roundtrip: geometry count drift");
  if (reloaded.entities.size() != loaded.entities.size())
    return fail("roundtrip: entity count drift");

  // --- 4. Compare per-object geometry, value for value -------------------
  // geometry_values_equal is bit-exact, so this catches any precision lost in
  // the text form as well as any field the writer forgot to emit.
  for (size_t i = 0; i < loaded.geometry.size(); ++i)
  {
    if (loaded.geometry[i].uid != reloaded.geometry[i].uid)
      return fail("roundtrip: geometry uid drift");
    if (!geometry_values_equal(loaded.geometry[i].value, reloaded.geometry[i].value))
      return fail("roundtrip: geometry value drift");
  }

  // The saved text must be in the NEW form: geometry blocks, no legacy geometry
  // entity blocks. Otherwise the conversion isn't one-time — it would re-run on
  // every load forever.
  {
    const std::string saved = serialize_map_to_string(loaded);
    if (saved.find("\"aabb_entity\"") != std::string::npos ||
        saved.find("\"displacement_entity\"") != std::string::npos ||
        saved.find("\"wedge_entity\"") != std::string::npos)
      return fail("roundtrip: saved text still contains legacy geometry "
                  "entity blocks");
    // A box is a brush now (Track B), so "box" is a READ-ONLY legacy keyword:
    // the writer must never emit one again, or the next load converts forever.
    if (saved.find("\nbrush\n{") == std::string::npos)
      return fail("roundtrip: saved text has no brush geometry block");
    if (saved.find("\nbox\n{") != std::string::npos)
      return fail("roundtrip: saved text still writes legacy box blocks");
    // Same for "displacement" (Track D): the kind is gone and the keyword is
    // read-only, so the grid must have been written as a face sub-block.
    if (saved.find("\ndisplacement\n{") != std::string::npos)
      return fail("roundtrip: saved text still writes legacy displacement "
                  "blocks");
    if (saved.find("subdivision_level") == std::string::npos)
      return fail("roundtrip: the converted grid was not written as a face "
                  "subdivision");
  }

  // --- 4b. Rotations are quaternions, and the legacy euler converts once ---
  //
  // Two halves, and they discriminate DIFFERENTLY on purpose. A geometry block's
  // keys are hand-written, so `orientation` became a read-only key exactly like
  // the `box` and `displacement` keywords and the writer emits `rotation`. An
  // entity's key IS its field name in entities.def, so it stays `orientation`
  // and the legacy arm keys on the value having three components instead of
  // four. Both are one-time; only the first is provably one-time.
  {
    map_t rotated;
    static_mesh_geometry_t mesh;
    mesh.position           = {10.f, 20.f, 30.f};
    mesh.orientation        = linalg::from_view_angles(35.f, -12.f);
    mesh.surface.mesh_path  = "resources/obj/Pyramid.obj";
    rotated.add_geometry(mesh);

    const std::string saved = serialize_map_to_string(rotated);
    if (saved.find("\"rotation\"") == std::string::npos)
      return fail("rotation: a saved static mesh has no \"rotation\" key");
    if (saved.find("\"orientation\"") != std::string::npos)
      return fail("rotation: the writer still emits the legacy \"orientation\" key");

    const map_t reloaded = parse_map_from_string(saved);
    if (reloaded.geometry.size() != 1)
      return fail("rotation: the static mesh did not survive the round trip");

    const auto *reloaded_mesh = std::get_if<static_mesh_geometry_t>(&reloaded.geometry[0].value);
    if (!reloaded_mesh)
      return fail("rotation: the static mesh came back as another kind");

    // Bit-exact, because geometry_values_equal is and it is the undo primitive:
    // a lossy round trip pushes a phantom undo entry the moment anything is
    // touched. That is what %.9g in format_quat_exact is for.
    if (!geometry_values_equal(reloaded.geometry[0].value, rotated.geometry[0].value))
      return fail("rotation: a saved-and-reloaded static mesh compares unequal to itself");

    // The legacy geometry key, converted through the same euler convention the
    // pre-cutover writer used.
    std::string legacy = saved;
    const size_t rotation_key = legacy.find("\"rotation\"");
    const size_t line_end     = legacy.find('\n', rotation_key);
    legacy.replace(rotation_key, line_end - rotation_key, "\"orientation\" \"0 90 0\"");

    const map_t converted = parse_map_from_string(legacy);
    const auto *converted_mesh = std::get_if<static_mesh_geometry_t>(&converted.geometry[0].value);
    if (!converted_mesh)
      return fail("rotation: the legacy orientation key did not read back a static mesh");

    const linalg::vec3f converted_facing = linalg::forward(converted_mesh->orientation);
    const linalg::vec3f expected_facing  = linalg::forward(linalg::from_euler_degrees({0, 90, 0}));
    if (linalg::length(converted_facing - expected_facing) > 1e-5f)
      return fail("rotation: the legacy euler orientation did not convert to the "
                  "rotation it named");
  }

  // --- 5. Trigger volume round-trip (the "is save losing it?" check) -----
  // Build a tiny map with one trigger volume programmatically, save, reload,
  // and assert every saveable field survives. This is independent of the
  // maps/test fixture (which has no triggers).
  {
    map_t trig_map;
    trig_map.name = "trig_roundtrip";
    auto t = create_map_entity("trigger_volume_entity");
    auto *trig = entity_as<entities::Trigger_Volume_Entity>(t.get());
    if (!trig) return fail("trigger: factory returned wrong type");
    trig->position = {7.f, 8.f, 9.f};
    trig->volume.half_extents = {11.f, 12.f, 13.f};
    trig->action = entities::Trigger_Action::Warp_To_Spawn;
    trig->fire_mode = entities::Fire_Mode::Every_Tick;
    trig->param_target_name.set("spawn_a");
    trig->param_string.set("hello");
    trig->param_float = 42.5f;
    trig_map.add_entity(t);

    const std::string trig_path = "maps/trig_roundtrip.source";
    if (!save_map(trig_path, trig_map))
      return fail("trigger: save_map failed");

    std::optional<map_t> reloaded_trig_opt = try_load_map(trig_path);
    if (!reloaded_trig_opt)
      return fail("trigger: try_load_map failed");
    map_t &reloaded_trig = *reloaded_trig_opt;
    std::filesystem::remove(trig_path);
    std::filesystem::remove(trig_path + ".navmesh");

    size_t n = 0;
    entities::Trigger_Volume_Entity *rt = nullptr;
    for (const auto &e : reloaded_trig.entities)
      if (auto *tt = entity_as<entities::Trigger_Volume_Entity>(e.entity.get()))
      { rt = tt; ++n; }
    if (n != 1) return fail("trigger: expected exactly 1 reloaded trigger");
    if (rt->position.x != 7.f || rt->position.y != 8.f || rt->position.z != 9.f)
      return fail("trigger: position drift");
    if (rt->volume.half_extents.x != 11.f || rt->volume.half_extents.y != 12.f ||
        rt->volume.half_extents.z != 13.f)
      return fail("trigger: volume drift");
    if (rt->action != entities::Trigger_Action::Warp_To_Spawn)
      return fail("trigger: action drift");
    if (rt->fire_mode != entities::Fire_Mode::Every_Tick)
      return fail("trigger: fire_mode drift");
    if (std::string(rt->param_target_name.c_str()) != "spawn_a")
      return fail("trigger: param_target_name drift");
    if (std::string(rt->param_string.c_str()) != "hello")
      return fail("trigger: param_string drift");
    if (rt->param_float != 42.5f)
      return fail("trigger: param_float drift");
  }

  // --- 5b. Spot light round-trip (the three-types-not-one-enum split) -----
  // Lights were one Light_Entity carrying a Light_Type enum, where five of the
  // seven fields were dead for at least one kind. Splitting them made the
  // per-kind fields real fields, so what this checks is that the shared Light
  // component and the kind-specific ones both survive save/load, and that the
  // three types come back as three DISTINCT types rather than collapsing to
  // whichever one the classname lookup saw first.
  {
    map_t light_map;
    light_map.name = "light_roundtrip";

    auto spot_holder = create_map_entity("spot_light_entity");
    auto *spot = entity_as<entities::Spot_Light_Entity>(spot_holder.get());
    if (!spot) return fail("light: factory returned wrong type for spot");
    spot->position      = {1.f, 2.f, 3.f};
    spot->orientation   = linalg::from_view_angles(90.f, 0.f);
    spot->light.color   = {0.25f, 0.5f, 0.75f};
    spot->light.intensity = 3.5f;
    spot->range         = 640.f;
    spot->inner_degrees = 12.f;
    spot->outer_degrees = 48.f;
    light_map.add_entity(spot_holder);

    auto point_holder = create_map_entity("point_light_entity");
    auto *point = entity_as<entities::Point_Light_Entity>(point_holder.get());
    if (!point) return fail("light: factory returned wrong type for point");
    point->range = 96.f;
    light_map.add_entity(point_holder);

    auto directional_holder = create_map_entity("directional_light_entity");
    if (!entity_as<entities::Directional_Light_Entity>(directional_holder.get()))
      return fail("light: factory returned wrong type for directional");
    light_map.add_entity(directional_holder);

    const std::string light_path = "maps/light_roundtrip.source";
    if (!save_map(light_path, light_map))
      return fail("light: save_map failed");

    std::optional<map_t> reloaded_opt = try_load_map(light_path);
    if (!reloaded_opt)
      return fail("light: try_load_map failed");
    map_t &reloaded_lights = *reloaded_opt;
    std::filesystem::remove(light_path);
    std::filesystem::remove(light_path + ".navmesh");

    size_t point_count = 0, spot_count = 0, directional_count = 0;
    entities::Spot_Light_Entity *reloaded_spot = nullptr;
    entities::Point_Light_Entity *reloaded_point = nullptr;
    for (const auto &e : reloaded_lights.entities)
    {
      if (auto *p = entity_as<entities::Point_Light_Entity>(e.entity.get()))
      { reloaded_point = p; ++point_count; }
      if (auto *sp = entity_as<entities::Spot_Light_Entity>(e.entity.get()))
      { reloaded_spot = sp; ++spot_count; }
      if (entity_as<entities::Directional_Light_Entity>(e.entity.get()))
        ++directional_count;
    }
    if (point_count != 1 || spot_count != 1 || directional_count != 1)
      return fail("light: the three kinds did not come back as three distinct types");

    if (reloaded_spot->light.color.x != 0.25f || reloaded_spot->light.color.y != 0.5f ||
        reloaded_spot->light.color.z != 0.75f)
      return fail("light: shared Light component colour drift");
    if (reloaded_spot->light.intensity != 3.5f)
      return fail("light: shared Light component intensity drift");
    if (reloaded_spot->range != 640.f)
      return fail("light: spot range drift");
    if (reloaded_spot->inner_degrees != 12.f || reloaded_spot->outer_degrees != 48.f)
      return fail("light: spot cone angle drift");
    // Asserted on the FACING rather than on a component: a rotation's identity is
    // where it points, and reading .y off one is exactly the euler habit the
    // quaternion migration exists to delete.
    const linalg::vec3f reloaded_facing = linalg::forward(reloaded_spot->orientation);
    const linalg::vec3f authored_facing = linalg::direction_from_angles(90.f, 0.f);
    if (linalg::length(reloaded_facing - authored_facing) > 1e-4f)
      return fail("light: spot orientation drift -- it is what replaced the direction field");
    if (reloaded_point->range != 96.f)
      return fail("light: point range drift");
    if (reloaded_point->light.intensity != 1.0f)
      return fail("light: a field left at its default did not come back as the default");
  }

  // --- 6. Canonical serialization + content hash --------------------------
  // serialize_map_to_string / parse_map_from_string are the pure (no-I/O) core
  // of save_map / load_map, and compute_map_content_hash hashes that canonical
  // string. Verify: (a) the canonical text round-trips through parse without
  // entity loss, (b) the hash is stable across serialize->parse->serialize, and
  // (c) the hash is formatting-independent — reformatting the on-disk text (extra
  // whitespace/blank lines) must NOT change the hash, since both sides re-derive
  // it from the canonical form. That last property is the whole reason we hash
  // the canonical string instead of the raw file bytes.
  {
    const uint32_t hash0 = compute_map_content_hash(loaded);

    const std::string canonical = serialize_map_to_string(loaded);
    const map_t from_canonical = parse_map_from_string(canonical);

    if (count_geometry_of_kind(from_canonical, geometry_kind_t::Brush) != brushes)
      return fail("canonical: brush count drift through string round-trip");

    // Hash must be stable through a full serialize->parse->hash cycle.
    if (compute_map_content_hash(from_canonical) != hash0)
      return fail("canonical: hash changed across serialize/parse round-trip");

    // Re-serializing the reparsed map must produce byte-identical text.
    if (serialize_map_to_string(from_canonical) != canonical)
      return fail("canonical: re-serialized text is not stable");

    // Formatting-independence: mangle whitespace in the source text (double every
    // newline, inject blank lines) — the parser tokenizes on whitespace, so this
    // yields the same entities and therefore the same canonical hash.
    std::string mangled;
    for (char c : canonical)
    {
      mangled += c;
      if (c == '\n') mangled += "\n   \n";
    }
    const map_t from_mangled = parse_map_from_string(mangled);
    if (compute_map_content_hash(from_mangled) != hash0)
      return fail("canonical: hash is not formatting-independent");
  }

  // --- 7. Compiled map package round-trip ---------------------------------
  // The compiled package (the wire artifact) bundles the entity text with baked
  // sidecars (navmesh). Verify: (a) build->serialize->deserialize round-trips
  // entity_text, map_name, and the navmesh exactly; (b) the package hash is
  // stable across a serialize cycle; (c) a corrupted magic is rejected rather
  // than yielding a half-built package.
  {
    // Give the map a small hand-built navmesh so we exercise sidecar packing
    // even though the maps/test fixture ships without one.
    map_t packaged = loaded;
    packaged.navmesh.vertices = {
        {{0.f, 0.f, 0.f}}, {{4.f, 0.f, 0.f}}, {{4.f, 0.f, 4.f}},
        {{0.f, 0.f, 4.f}}, {{8.f, 1.f, 0.f}}};
    nav_polygon_t poly0;
    poly0.vertices     = {0, 1, 2, 3};
    poly0.neighbors = {-1, 1, -1, -1};
    poly0.island    = 0;
    nav_polygon_t poly1;
    poly1.vertices     = {1, 4, 2};
    poly1.neighbors = {-1, -1, 0};
    poly1.island    = 0;
    packaged.navmesh.polygons = {poly0, poly1};

    // The lightmap rides the package for the same reason the navmesh does: a
    // downloaded map has no .lightmap sidecar beside it -- it never sees the
    // .source -- so without this every brush on a networked client draws unlit
    // while the listen server hosting the same map looks correct.
    lightmap_chart_t chart;
    chart.object_uid            = 7;
    chart.plane.point           = {1.f, 2.f, 3.f};
    chart.plane.normal          = {0.f, 1.f, 0.f};
    chart.origin                = {-4.f, 2.f, 8.5f};
    chart.tangent_u             = {1.f, 0.f, 0.f};
    chart.tangent_v             = {0.f, 0.f, 1.f};
    chart.world_units_per_texel = 4.f;
    chart.page                  = 1;
    chart.atlas_rect            = {3, 5, 16, 24};
    chart.light_slots[0]        = 1;
    chart.light_slots[1]        = 0;

    packaged.lightmap.charts                              = {chart};
    packaged.lightmap.settings.texels_per_world_unit      = 0.25f;
    packaged.lightmap.settings.gutter_in_texels           = 2;
    packaged.lightmap.settings.max_chart_extent_in_texels = 512;
    packaged.lightmap.settings.atlas_size_in_texels       = 64;
    packaged.lightmap.atlas.size_in_texels                = 64;
    packaged.lightmap.atlas.page_count                    = 2;
    packaged.lightmap.irradiance_pages.allocate(packaged.lightmap.atlas,
                                     lightmap_pixel_format_t::Rgb9e5);
    // On the second page, so a page-major layout mistake shows up as a texel in
    // the wrong place rather than as nothing at all.
    packaged.lightmap.irradiance_pages.store(1, 3, 5, {2.f, 0.5f, 0.125f});

    // The per-light half, which is a second page set and a table naming what
    // each of its channels is OF. A package carrying the pixels and not the
    // table hands every chart four unnamed numbers.
    packaged.lightmap.light_uids = {41, 42};
    packaged.lightmap.visibility_pages.allocate(packaged.lightmap.atlas,
                                                lightmap_pixel_format_t::Unorm8x4);
    packaged.lightmap.visibility_pages.store_visibility(1, 3, 5,
                                                        {{1.f, 0.5f, 0.f, 0.f}});
    set_lightmap_geometry_id(packaged.lightmap);

    map_package_t package = build_map_package(packaged);
    if (package.entity_text != serialize_map_to_string(packaged))
      return fail("package: entity_text is not the canonical serialization");

    std::vector<uint8_t> blob = serialize_map_package(package);
    const uint32_t package_hash = compute_map_package_hash(blob);

    map_package_t restored;
    if (!deserialize_map_package(blob, restored))
      return fail("package: deserialize_map_package failed on a valid blob");

    if (restored.map_name != package.map_name)
      return fail("package: map_name drift");
    if (restored.entity_text != package.entity_text)
      return fail("package: entity_text drift");
    if (restored.navmesh.vertices.size() != package.navmesh.vertices.size() ||
        restored.navmesh.polygons.size() != package.navmesh.polygons.size())
      return fail("package: navmesh size drift");
    for (size_t i = 0; i < package.navmesh.vertices.size(); ++i)
    {
      const auto &a = package.navmesh.vertices[i].position;
      const auto &b = restored.navmesh.vertices[i].position;
      if (a.x != b.x || a.y != b.y || a.z != b.z)
        return fail("package: navmesh vertex drift");
    }
    for (size_t i = 0; i < package.navmesh.polygons.size(); ++i)
    {
      const auto &a = package.navmesh.polygons[i];
      const auto &b = restored.navmesh.polygons[i];
      if (a.vertices != b.vertices || a.neighbors != b.neighbors ||
          a.island != b.island)
        return fail("package: navmesh polygon drift");
    }

    if (restored.lightmap.charts.size() != package.lightmap.charts.size())
      return fail("package: lightmap chart count drift");
    if (restored.lightmap.irradiance_pages.bytes != package.lightmap.irradiance_pages.bytes)
      return fail("package: lightmap page bytes drift");
    if (restored.lightmap.visibility_pages.bytes !=
        package.lightmap.visibility_pages.bytes)
      return fail("package: lightmap visibility page bytes drift");
    if (restored.lightmap.visibility_pages.format !=
        package.lightmap.visibility_pages.format)
      return fail("package: lightmap visibility format drift");
    if (restored.lightmap.light_uids != package.lightmap.light_uids)
      return fail("package: lightmap resolve table drift");
    // geometry_id is a content hash over the charts, the settings and the atlas
    // dimensions, so one comparison covers every field the wire carries -- and
    // it is RECOMPUTED on the receiving side rather than sent, which is what
    // makes it an independent check rather than an echo.
    if (restored.lightmap.geometry_id != package.lightmap.geometry_id)
      return fail("package: lightmap geometry id drift");
    {
      const lightmap_chart_t &a = package.lightmap.charts[0];
      const lightmap_chart_t &b = restored.lightmap.charts[0];
      if (a.object_uid != b.object_uid || a.page != b.page ||
          a.atlas_rect.min_x != b.atlas_rect.min_x ||
          a.atlas_rect.min_y != b.atlas_rect.min_y ||
          a.atlas_rect.width != b.atlas_rect.width ||
          a.atlas_rect.height != b.atlas_rect.height ||
          a.world_units_per_texel != b.world_units_per_texel ||
          a.origin.x != b.origin.x || a.origin.y != b.origin.y ||
          a.origin.z != b.origin.z || a.plane.normal.y != b.plane.normal.y)
        return fail("package: lightmap chart field drift");
    }

    // A map with no bake must round-trip as no bake, not as a zero-chart atlas
    // that every brush then samples.
    {
      map_t unbaked = packaged;
      unbaked.lightmap = {};
      map_package_t unbaked_restored;
      if (!deserialize_map_package(serialize_map_package(build_map_package(unbaked)),
                                   unbaked_restored))
        return fail("package: an unbaked map failed to deserialize");
      if (!unbaked_restored.lightmap.empty())
        return fail("package: an unbaked map came back carrying a lightmap");
    }

    // Re-serializing the restored package must be byte-identical (stable hash).
    if (compute_map_package_hash(serialize_map_package(restored)) !=
        package_hash)
      return fail("package: hash changed across serialize/deserialize cycle");

    // A corrupted magic must be rejected, not silently half-parsed.
    std::vector<uint8_t> corrupt = blob;
    corrupt[0] ^= 0xFF;
    map_package_t should_fail;
    if (deserialize_map_package(corrupt, should_fail))
      return fail("package: deserialize accepted a corrupted magic");
  }

  // --- 8. Per-map cvar settings round-trip --------------------------------
  // map_t::attached_cvars is written as a 'cvars' block and read back as one
  // line per property. The block's properties are a std::map, so the reloaded
  // order is by cvar name -- the fixture below is already in that order.
  {
    map_t cvar_map;
    cvar_map.name = "cvar_roundtrip";
    cvar_map.attached_cvars = {"g_gravity 200", "pm_maxspeed 400"};

    const std::string canonical = serialize_map_to_string(cvar_map);
    if (canonical.find("\ncvars\n{") == std::string::npos)
      return fail("cvars: saved text has no cvars block");

    const map_t reparsed = parse_map_from_string(canonical);
    if (reparsed.attached_cvars != cvar_map.attached_cvars)
      return fail("cvars: attached_cvars drift through a round-trip");

    // A cvars block must not become an object in either list, and the text must
    // be stable (so the content hash is).
    if (!reparsed.entities.empty() || !reparsed.geometry.empty())
      return fail("cvars: a cvars block produced a map object");
    if (serialize_map_to_string(reparsed) != canonical)
      return fail("cvars: re-serialized text is not stable");

    // A value with spaces survives, and a bare name (no value) stays bare.
    map_t odd_map;
    odd_map.attached_cvars = {"announce hello there", "sv_lag_compensation"};
    const map_t odd_reparsed = parse_map_from_string(serialize_map_to_string(odd_map));
    if (odd_reparsed.attached_cvars != odd_map.attached_cvars)
      return fail("cvars: a spaced value or a bare name did not survive");
  }

  // --- 9. Per-face surfaces and the material table ------------------------
  // A brush's faces are written as `face` sub-blocks under its own block -- the
  // one place the grammar nests -- and the paths they index live in a `materials`
  // block. Three things have to hold: a brush with no face blocks reads exactly
  // as it did before they existed, a brush WITH them survives a round trip
  // bit-exactly, and save drops the table entries nothing names any more.
  {
    map_t face_map;
    face_map.name = "face_roundtrip";

    // Three declared, two used: entry 1 is what the drop-and-remap pass has to
    // take out, which is also what moves entry 2 down to 1.
    face_map.materials = {"", "resources/textures/unused_here",
                          "resources/textures/harsh_bricks"};

    brush_geometry_t brush;
    brush.hull_points = make_box_brush_points({0, 0, 0}, {64, 64, 64});
    sync_face_surfaces(brush);
    if (brush.face_surfaces.size() != 6)
      return fail("faces: a box brush did not hull into six faces");

    brush.face_surfaces[0].material       = 2;
    brush.face_surfaces[1].emits_geometry = false;
    brush.face_surfaces[2].uv.u_scale     = 32.0f;
    brush.face_surfaces[2].uv.u_shift     = 7.5f;
    face_map.add_geometry(brush);

    const std::string canonical = serialize_map_to_string(face_map);
    if (canonical.find("\n  face\n  {") == std::string::npos)
      return fail("faces: saved text has no nested face block");
    if (canonical.find("\nmaterials\n{") == std::string::npos)
      return fail("faces: saved text has no materials block");
    if (canonical.find("unused_here") != std::string::npos)
      return fail("faces: save kept a material no face names");

    const map_t reparsed = parse_map_from_string(canonical);
    if (reparsed.materials.size() != 2 ||
        reparsed.materials[1] != "resources/textures/harsh_bricks")
      return fail("faces: the material table did not compact to the two in use");
    if (reparsed.geometry.size() != 1)
      return fail("faces: the brush did not survive the round trip");

    const brush_geometry_t &reloaded =
        std::get<brush_geometry_t>(reparsed.geometry.front().value);
    if (reloaded.face_surfaces.size() != brush.face_surfaces.size())
      return fail("faces: a face was lost across the round trip");
    if (reloaded.face_surfaces[0].material != 1)
      return fail("faces: the remap did not follow the material it kept");
    if (reloaded.face_surfaces[1].emits_geometry)
      return fail("faces: emits_geometry did not survive");
    if (reloaded.face_surfaces[2].uv.u_scale != 32.0f ||
        reloaded.face_surfaces[2].uv.u_shift != 7.5f)
      return fail("faces: the uv channel did not survive bit-exactly");

    if (serialize_map_to_string(reparsed) != canonical)
      return fail("faces: re-serialized text is not stable");

    // The pre-faces form: no face blocks at all. Every face falls back to the
    // brush default, which is what makes this format change need no version.
    brush_geometry_t faceless_brush;
    faceless_brush.hull_points = make_box_brush_points({0, 0, 0}, {32, 32, 32});
    map_t faceless_map;
    faceless_map.add_geometry(faceless_brush);
    const map_t faceless_reparsed =
        parse_map_from_string(serialize_map_to_string(faceless_map));
    if (faceless_reparsed.geometry.size() != 1)
      return fail("faces: a brush with no face blocks did not load");
    if (!std::get<brush_geometry_t>(faceless_reparsed.geometry.front().value)
             .face_surfaces.empty())
      return fail("faces: a brush with no face blocks invented some");
  }

  printf("map_migration_test: OK (%zu brushes, %zu subdivided faces converted from "
         "legacy entity blocks, wedges dropped, trigger round-trip OK, "
         "canonical hash stable, package round-trip OK)\n",
         brushes, subdivided_faces);
  return 0;
}
