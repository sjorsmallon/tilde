// Round-trip + migration test for the geometry exit.
//
// Verifies on the real `maps/test` fixture (which is a PRE-EXIT map: its
// geometry is still written as `entity` blocks with classname aabb_entity /
// displacement_entity, plus one retired wedge_entity):
//   1. Legacy geometry entities convert into map.geometry at load, and land in
//      the geometry list rather than the entity list.
//   2. Wedge entities are dropped at load.
//   3. Saving the converted map and re-loading it is lossless for geometry, and
//      the saved file no longer contains the legacy form.

#include "entities/trigger_volume_entity.hpp"
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

// The box brushes, in map order.
static std::vector<const box_geometry_t *> get_boxes(const map_t &m)
{
  std::vector<const box_geometry_t *> out;
  for (const map_geometry_t &g : m.geometry)
    if (const auto *box = std::get_if<box_geometry_t>(&g.value))
      out.push_back(box);
  return out;
}

int main()
{
  const std::string fixture = "maps/test";
  if (!std::filesystem::exists(fixture))
    return fail("fixture 'maps/test' missing — run from project root");

  // --- 1. Load pre-migration fixture --------------------------------------
  map_t loaded;
  if (!load_map(fixture, loaded))
    return fail("load_map(maps/test) failed");

  const size_t boxes = count_geometry_of_kind(loaded, geometry_kind_t::Box);
  if (boxes == 0)
    return fail("conversion: expected at least 1 box brush from the fixture's "
                "legacy aabb_entity blocks");

  const size_t displacements =
      count_geometry_of_kind(loaded, geometry_kind_t::Displacement);
  if (displacements == 0)
    return fail("conversion: expected the fixture's displacement_entity to "
                "become displacement geometry");

  // Geometry must NOT have landed in the entity list — that's the whole point.
  for (const auto &e : loaded.entities)
  {
    const std::string classname = get_classname_for_entity(e.entity.get());
    if (classname == "aabb_entity" || classname == "displacement_entity" ||
        classname == "static_mesh_entity" || classname == "wedge_entity")
      return fail("conversion: geometry is still in the entity list");
  }

  // Every converted box must carry the legacy volume's half_extents, not a
  // default — a zeroed or default-sized box means the "volume" blob wasn't read.
  for (const box_geometry_t *box : get_boxes(loaded))
  {
    const auto &h = box->half_extents;
    if (h.x == 0.f && h.y == 0.f && h.z == 0.f)
      return fail("conversion: box brush loaded with zeroed half_extents");
    if (h.x == 1.f && h.y == 1.f && h.z == 1.f)
      return fail("conversion: box brush kept the default half_extents — the "
                  "legacy \"volume\" blob was not parsed");
  }

  // The converted displacement's grid must be sized for its subdivision level,
  // which is the invariant the fixed-size schema array used to guarantee.
  for (const map_geometry_t &g : loaded.geometry)
  {
    const auto *displacement = std::get_if<displacement_geometry_t>(&g.value);
    if (!displacement) continue;
    if ((int)displacement->displacements.size() != displacement->vertex_count())
      return fail("conversion: displacement grid size does not match its "
                  "subdivision level");
  }

  // --- 2. Save & re-load --------------------------------------------------
  const std::string out = "maps/test.roundtrip";
  if (!save_map(out, loaded))
    return fail("save_map(maps/test.roundtrip) failed");

  map_t reloaded;
  if (!load_map(out, reloaded))
    return fail("load_map(maps/test.roundtrip) failed");

  // Cleanup the temp file immediately (also any .navmesh sidecar).
  std::filesystem::remove(out);
  std::filesystem::remove(out + ".navmesh");

  // --- 3. Compare counts --------------------------------------------------
  if (count_geometry_of_kind(reloaded, geometry_kind_t::Box) != boxes)
    return fail("roundtrip: box count drift");
  if (count_geometry_of_kind(reloaded, geometry_kind_t::Displacement) != displacements)
    return fail("roundtrip: displacement count drift");
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
    if (saved.find("\nbox\n{") == std::string::npos)
      return fail("roundtrip: saved text has no box geometry block");
  }

  // --- 5. Trigger volume round-trip (the "is save losing it?" check) -----
  // Build a tiny map with one trigger volume programmatically, save, reload,
  // and assert every saveable field survives. This is independent of the
  // maps/test fixture (which has no triggers).
  {
    map_t trig_map;
    trig_map.name = "trig_roundtrip";
    auto t = create_entity_by_classname("trigger_volume");
    auto *trig = entity_as<network::Trigger_Volume_Entity>(t.get());
    if (!trig) return fail("trigger: factory returned wrong type");
    trig->position = {7.f, 8.f, 9.f};
    trig->volume.half_extents = {11.f, 12.f, 13.f};
    trig->action_name.set("warp_to_spawn");
    trig->fire_mode.set("every_tick");
    trig->param_target_name.set("spawn_a");
    trig->param_string.set("hello");
    trig->param_float = 42.5f;
    trig_map.add_entity(t);

    const std::string trig_path = "maps/trig_roundtrip.source";
    if (!save_map(trig_path, trig_map))
      return fail("trigger: save_map failed");

    map_t reloaded_trig;
    if (!load_map(trig_path, reloaded_trig))
      return fail("trigger: load_map failed");
    std::filesystem::remove(trig_path);
    std::filesystem::remove(trig_path + ".navmesh");

    size_t n = 0;
    network::Trigger_Volume_Entity *rt = nullptr;
    for (const auto &e : reloaded_trig.entities)
      if (auto *tt = entity_as<network::Trigger_Volume_Entity>(e.entity.get()))
      { rt = tt; ++n; }
    if (n != 1) return fail("trigger: expected exactly 1 reloaded trigger");
    if (rt->position.x != 7.f || rt->position.y != 8.f || rt->position.z != 9.f)
      return fail("trigger: position drift");
    if (rt->volume.half_extents.x != 11.f || rt->volume.half_extents.y != 12.f ||
        rt->volume.half_extents.z != 13.f)
      return fail("trigger: volume drift");
    if (std::string(rt->action_name.c_str()) != "warp_to_spawn")
      return fail("trigger: action_name drift");
    if (std::string(rt->fire_mode.c_str()) != "every_tick")
      return fail("trigger: fire_mode drift");
    if (std::string(rt->param_target_name.c_str()) != "spawn_a")
      return fail("trigger: param_target_name drift");
    if (std::string(rt->param_string.c_str()) != "hello")
      return fail("trigger: param_string drift");
    if (rt->param_float != 42.5f)
      return fail("trigger: param_float drift");
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
    map_t from_canonical;
    if (!parse_map_from_string(canonical, from_canonical))
      return fail("canonical: parse_map_from_string failed");

    if (count_geometry_of_kind(from_canonical, geometry_kind_t::Box) != boxes)
      return fail("canonical: box count drift through string round-trip");

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
    map_t from_mangled;
    if (!parse_map_from_string(mangled, from_mangled))
      return fail("canonical: parse of reformatted text failed");
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
    poly0.verts     = {0, 1, 2, 3};
    poly0.neighbors = {-1, 1, -1, -1};
    poly0.island    = 0;
    nav_polygon_t poly1;
    poly1.verts     = {1, 4, 2};
    poly1.neighbors = {-1, -1, 0};
    poly1.island    = 0;
    packaged.navmesh.polygons = {poly0, poly1};

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
      const auto &a = package.navmesh.vertices[i].pos;
      const auto &b = restored.navmesh.vertices[i].pos;
      if (a.x != b.x || a.y != b.y || a.z != b.z)
        return fail("package: navmesh vertex drift");
    }
    for (size_t i = 0; i < package.navmesh.polygons.size(); ++i)
    {
      const auto &a = package.navmesh.polygons[i];
      const auto &b = restored.navmesh.polygons[i];
      if (a.verts != b.verts || a.neighbors != b.neighbors ||
          a.island != b.island)
        return fail("package: navmesh polygon drift");
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

  printf("map_migration_test: OK (%zu boxes, %zu displacements converted from "
         "legacy entity blocks, wedges dropped, trigger round-trip OK, "
         "canonical hash stable, package round-trip OK)\n",
         boxes, displacements);
  return 0;
}
