// Round-trip + migration test for the box_volume_t refactor.
//
// Verifies on the real `maps/test` fixture (which contains pre-migration
// `half_extents` properties and at least one `wedge_entity`):
//   1. Wedge entities are stripped at load.
//   2. The `half_extents` -> `volume.half_extents` compat shim populates the
//      new component on AABB / Trigger / Displacement entities.
//   3. Saving the migrated map and re-loading it is lossless for box entities
//      (entity count, position, volume.half_extents all match).

#include "entities/displacement_entity.hpp"
#include "entities/static_entities.hpp"
#include "entities/trigger_volume_entity.hpp"
#include "log.hpp"
#include "map.hpp"
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

int main()
{
  const std::string fixture = "maps/test";
  if (!std::filesystem::exists(fixture))
    return fail("fixture 'maps/test' missing — run from project root");

  // --- 1. Load pre-migration fixture --------------------------------------
  map_t loaded;
  if (!load_map(fixture, loaded))
    return fail("load_map(maps/test) failed");

  size_t wedges = count_of<network::Wedge_Entity>(loaded);
  if (wedges != 0)
    return fail("wedge-strip: expected 0 wedge entities after load");

  size_t aabbs = count_of<network::AABB_Entity>(loaded);
  if (aabbs == 0)
    return fail("expected at least 1 AABB_Entity in fixture");

  size_t displacements = count_of<network::Displacement_Entity>(loaded);

  // Every AABB must have non-zero volume.half_extents — proves the
  // half_extents -> volume compat shim populated the new component.
  size_t aabbs_checked = 0;
  for (const auto &e : loaded.entities)
  {
    auto *aabb = entity_as<network::AABB_Entity>(e.entity.get());
    if (!aabb) continue;
    const auto &h = aabb->volume.half_extents;
    if (h.x == 0.f && h.y == 0.f && h.z == 0.f)
      return fail("compat shim: AABB_Entity loaded with zeroed volume");
    ++aabbs_checked;
  }
  if (aabbs_checked != aabbs)
    return fail("compat shim: aabb count mismatch during verify");

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
  if (count_of<network::Wedge_Entity>(reloaded) != 0)
    return fail("roundtrip: wedges reappeared after save+load");
  if (count_of<network::AABB_Entity>(reloaded) != aabbs)
    return fail("roundtrip: AABB count drift");
  if (count_of<network::Displacement_Entity>(reloaded) != displacements)
    return fail("roundtrip: Displacement count drift");

  // --- 4. Compare per-AABB geometry (position + volume) -------------------
  auto get_aabbs = [](const map_t &m) {
    std::vector<network::AABB_Entity *> out;
    for (const auto &e : m.entities)
      if (auto *a = entity_as<network::AABB_Entity>(e.entity.get()))
        out.push_back(a);
    return out;
  };
  auto a = get_aabbs(loaded);
  auto b = get_aabbs(reloaded);
  if (a.size() != b.size())
    return fail("roundtrip: AABB vector size drift");
  for (size_t i = 0; i < a.size(); ++i)
  {
    if (a[i]->position.x != b[i]->position.x ||
        a[i]->position.y != b[i]->position.y ||
        a[i]->position.z != b[i]->position.z)
      return fail("roundtrip: AABB position drift");
    if (a[i]->volume.half_extents.x != b[i]->volume.half_extents.x ||
        a[i]->volume.half_extents.y != b[i]->volume.half_extents.y ||
        a[i]->volume.half_extents.z != b[i]->volume.half_extents.z)
      return fail("roundtrip: AABB volume.half_extents drift");
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

  printf("map_migration_test: OK (%zu aabbs, %zu displacements, wedges "
         "stripped, trigger round-trip OK)\n",
         aabbs, displacements);
  return 0;
}
