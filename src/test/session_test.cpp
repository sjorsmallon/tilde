#include "../shared/entities/entity_reflection.hpp"
#include "disabled_geometry.hpp"
#include "game_session.hpp"
#include "log.hpp"
#include "map.hpp" // shared::create_entity_by_classname
#include <cassert>
#include <cmath>
#include <iostream>
#include <utility>
#include <vector>

using namespace shared;

int main()
{
  log_error("Starting Session Test");

  // 1. Create a dummy map_t
  map_t test_map;
  test_map.name = "Test Map";

  // A box brush — map-owned geometry, not an entity.
  const entity_uid_t floor_uid =
      test_map.add_geometry(make_box_brush({0, 0, 0}, {10, 10, 10}));

  // Add a Player Spawn marker
  auto spawn_ent = shared::create_map_entity("player_spawn_entity");
  if (auto *p = entities::entity_as<entities::Player_Spawn_Entity>(spawn_ent.get()))
  {
    p->position = {5, 5, 0};
  }
  const entity_uid_t spawn_uid = test_map.add_entity(spawn_ent);

  // Geometry and entities share one uid space, so the two must differ.
  if (floor_uid != 1 || spawn_uid != 2)
  {
    log_error("uids should be allocated from one shared counter; got floor={}, "
              "spawn={}",
              floor_uid, spawn_uid);
    return 1;
  }

  // Taken BEFORE any session exists, so the checks at the bottom of this test
  // can compare against a map nothing has had a chance to touch.
  const uint32_t map_hash_before_init = compute_map_content_hash(test_map);

  // 2. Build the session
  game_session_t session = build_session(test_map);

  // 3. Verify

  // Verify Map Name
  if (session.map_name != "Test Map")
  {
    log_error("Map Name Mismatch");
    return 1;
  }

  // The session copies the map's geometry; entities go to the entity system.
  if (session.geometry.size() != 1)
  {
    log_error("Session geometry size mismatch. Expected 1, got {}",
              session.geometry.size());
    return 1;
  }

  if (session.geometry[0].uid != floor_uid)
  {
    log_error("Session geometry uid mismatch. Expected {}, got {}", floor_uid,
              session.geometry[0].uid);
    return 1;
  }

  // The session's geometry must be a COPY, not an alias of the map's. Editing
  // the session must not write through into the map — the old shared_ptr path
  // did exactly that, and P7's ownership work depends on it staying broken apart.
  {
    brush_geometry_t *session_brush =
        std::get_if<brush_geometry_t>(&session.geometry[0].value);
    if (!session_brush)
    {
      log_error("Session geometry 0 should be a brush");
      return 1;
    }
    const std::vector<linalg::vec3> original_vertices = session_brush->hull_points;
    session_brush->hull_points = make_box_brush_points({0, 0, 0}, {99, 99, 99});

    const map_geometry_t  *map_entry = test_map.find_geometry_by_uid(floor_uid);
    const brush_geometry_t *map_brush =
        map_entry ? std::get_if<brush_geometry_t>(&map_entry->value) : nullptr;
    if (!map_brush)
    {
      log_error("Map geometry {} should still be a brush", floor_uid);
      return 1;
    }
    if (compute_brush_bounds(map_brush->hull_points).max.x != 10.f)
    {
      log_error("Session aliases the map's geometry: writing the session's copy "
                "changed the map's bounds to {}",
                compute_brush_bounds(map_brush->hull_points).max.x);
      return 1;
    }

    // Put it back so the BVH check below still describes the real geometry.
    session_brush->hull_points = original_vertices;
  }

  // Verify the runtime spawn counter was seeded past the highest map uid.
  // The box is uid 1, the spawn is uid 2, so map.next_uid is 3 → entity_system
  // next_entity_id must also be 3 so the next spawn() can't collide.
  if (session.entity_system.next_entity_id != 3)
  {
    log_error("entity_system.next_entity_id should be seeded to 3 (= map.next_uid), got {}",
              session.entity_system.next_entity_id);
    return 1;
  }

  // Verify BVH (built over the session's geometry)
  if (session.bvh.nodes.empty())
  {
    log_error("BVH is empty; it should have one leaf for the box brush");
    return 1;
  }

  // Verify Entity System (Player spawn marker).
  // "player_start" classname maps to Player_Spawn_Entity / entities::entity_type::Player_Spawn_Entity.
  // Player_Entity (entities::entity_type::Player_Entity) is the live player, runtime-spawned
  // when a client connects — not a map-loaded thing.
  Span<entities::Player_Spawn_Entity> spawns =
      session.entity_system.entities_of<entities::Player_Spawn_Entity>();

  if (spawns.empty())
  {
    log_error("No Player Spawn markers found");
    return 1;
  }

  const entities::Player_Spawn_Entity &spawn = spawns[0];

  // Check if properties were applied correctly
  if (spawn.position.x != 5.0f || spawn.position.y != 5.0f)
  {
    log_error("Spawn marker position mismatch. Expected 5,5,0. Got: {},{},{}",
              spawn.position.x, spawn.position.y,
              spawn.position.z);
    return 1;
  }

  // Verify spawn marker's entity_id matches its map uid (added second, uid = 2).
  if (spawn.entity_id != spawn_uid)
  {
    log_error("Spawn marker entity_id should equal map uid {}, got {}", spawn_uid,
              spawn.entity_id);
    return 1;
  }

  // --- P7 step 1: build_session does not mutate the map -----------------------
  //
  // The load-bearing assertion is the DIRECT field read below, not the hash.
  // `entity_id` is deliberately not @Editable (it is runtime identity, not map
  // data), so it does not appear in the canonical text at all and a content-hash
  // comparison cannot see it being stomped. The hash check is kept anyway
  // because it covers everything that IS editable, but on its own it would have
  // passed against the very bug this test exists for.
  if (spawn_ent->entity_id != 0)
  {
    log_error("build_session wrote entity_id {} into the MAP's entity. The map "
              "owns that object; the session is supposed to stamp the uid on its own "
              "copy. `const map_t&` is a lie again.",
              spawn_ent->entity_id);
    return 1;
  }

  const uint32_t hash_after_init = compute_map_content_hash(test_map);
  if (hash_after_init != map_hash_before_init)
  {
    log_error("Map content hash changed across build_session: {} -> {}",
              map_hash_before_init, hash_after_init);
    return 1;
  }

  // Two sessions from one map. This is the failure the const lie produced: the
  // second init used to renumber the entity the first was already using.
  {
    game_session_t second_session = build_session(test_map);

    Span<entities::Player_Spawn_Entity> second_spawns =
        second_session.entity_system.entities_of<entities::Player_Spawn_Entity>();
    if (second_spawns.size() != 1 || second_spawns[0].entity_id != spawn_uid)
    {
      log_error("Second session from the same map did not get uid {} on its spawn marker",
                spawn_uid);
      return 1;
    }

    // ...and the first session is still intact, because neither wrote through
    // the map.
    if (spawns[0].entity_id != spawn_uid)
    {
      log_error("Initializing a second session disturbed the first session's uid: {}",
                spawns[0].entity_id);
      return 1;
    }

    if (spawn_ent->entity_id != 0)
    {
      log_error("Two inits later, the map's entity_id is {} instead of 0",
                spawn_ent->entity_id);
      return 1;
    }
  }

  // --- P7 step 2: the uid index -----------------------------------------------
  {
    Entity_System &entity_system = session.entity_system;

    if (entity_system.get<entities::Player_Spawn_Entity>(spawn_uid) != &spawns[0])
    {
      log_error("get<Player_Spawn_Entity>({}) did not resolve to the pooled spawn marker",
                spawn_uid);
      return 1;
    }

    // Right uid, wrong type: nullptr, not the entity. This is what lets the
    // damage and rocket dispatch use get<T> as both lookup AND type test.
    if (entity_system.get<entities::Player_Entity>(spawn_uid) != nullptr)
    {
      log_error("get<Player_Entity>({}) resolved, but that uid names a Player_Spawn_Entity",
                spawn_uid);
      return 1;
    }

    // A uid that was never issued.
    if (entity_system.get<entities::Player_Spawn_Entity>(9999) != nullptr)
    {
      log_error("get<Player_Spawn_Entity>(9999) resolved something; no such uid exists");
      return 1;
    }

    if (!entity_system.validate_locations())
    {
      log_error("uid index disagrees with the pools right after map load");
      return 1;
    }

    // Spawn churn, then remove from the MIDDLE so the swap-and-pop actually
    // moves an entity and the index has to be repaired. Removing only the tail
    // would exercise nothing.
    entity_uid_t rocket_uids[5] = {};
    for (int index = 0; index < 5; ++index)
    {
      // spawn() hands back the uid, and get<T>() is how the fields get written.
      // The pointer is used and dropped inside this iteration: the next loop
      // iteration spawns into the same pool and would invalidate it.
      rocket_uids[index] = entity_system.spawn<entities::Rocket_Entity>();
      entities::Rocket_Entity *rocket =
          entity_system.get<entities::Rocket_Entity>(rocket_uids[index]);
      if (!rocket)
      {
        log_error("Could not spawn rocket {}", index);
        return 1;
      }
      rocket->damage_amount = (float)index; // a per-entity marker to identify it by
      if (rocket->entity_id != rocket_uids[index])
      {
        log_error("spawn() returned uid {} but the entity carries {}",
                  rocket_uids[index], rocket->entity_id);
        return 1;
      }
    }

    if (!entity_system.destroy(rocket_uids[1]))
    {
      log_error("destroy({}) found nothing to destroy", rocket_uids[1]);
      return 1;
    }

    if (entity_system.get<entities::Rocket_Entity>(rocket_uids[1]) != nullptr)
    {
      log_error("uid {} still resolves after being destroyed", rocket_uids[1]);
      return 1;
    }

    // Every survivor must still resolve, and resolve to ITSELF — the swap moved
    // the last rocket into slot 1, so a broken fixup shows up here as a uid
    // pointing at the wrong entity rather than as a crash.
    for (int index = 0; index < 5; ++index)
    {
      if (index == 1)
        continue;

      entities::Rocket_Entity *rocket =
          entity_system.get<entities::Rocket_Entity>(rocket_uids[index]);
      if (!rocket)
      {
        log_error("Rocket uid {} (spawn #{}) stopped resolving after an unrelated removal",
                  rocket_uids[index], index);
        return 1;
      }
      if (rocket->entity_id != rocket_uids[index] || rocket->damage_amount != (float)index)
      {
        log_error("Rocket uid {} resolved to the wrong entity (got uid {}, marker {})",
                  rocket_uids[index], rocket->entity_id, rocket->damage_amount);
        return 1;
      }
    }

    if (!entity_system.validate_locations())
    {
      log_error("uid index disagrees with the pools after spawn/destroy churn");
      return 1;
    }

    // Destroying a uid nobody holds is an ordinary "already gone", not an error.
    if (entity_system.destroy(rocket_uids[1]))
    {
      log_error("destroy({}) reported success on an already-destroyed uid",
                rocket_uids[1]);
      return 1;
    }

    // reset() has to clear the index too, or every stale uid keeps resolving
    // into a pool that no longer holds it.
    entity_system.reset();
    if (entity_system.get<entities::Rocket_Entity>(rocket_uids[0]) != nullptr ||
        entity_system.get<entities::Player_Spawn_Entity>(spawn_uid) != nullptr)
    {
      log_error("A uid still resolves after Entity_System::reset()");
      return 1;
    }
    if (!entity_system.validate_locations())
    {
      log_error("uid index disagrees with the pools after reset");
      return 1;
    }
  }

  // --- entities_with<Component_T...>: the component aggregate -----------------
  //
  // The invariant no compiler checks. entities_with walks pools at the type's
  // runtime stride and offsets each component by a value resolved once per pool
  // — so a wrong stride or a wrong offset reads element 0 correctly and garbles
  // every element after it. The guard is therefore a comparison against the
  // brute-force walk it replaced, entity by entity, not a count.
  {
    game_session_t component_session = build_session(test_map);
    Entity_System &entity_system     = component_session.entity_system;

    // A deliberate mix: four types carrying Render (and one of them spawned
    // more than once, so the inner slot walk has somewhere to go), one carrying
    // Box_Volume alone, one carrying BOTH, and two carrying neither — the pools
    // that must be skipped.
    entity_system.spawn<entities::Rocket_Entity>();
    entity_system.spawn<entities::Rocket_Entity>();
    entity_system.spawn<entities::Rocket_Entity>();
    entity_system.spawn<entities::Hook_Entity>();
    entity_system.spawn<entities::Physics_Body_Entity>();
    entity_system.spawn<entities::Trigger_Volume_Entity>();
    entity_system.spawn<entities::Spot_Light_Entity>();
    entity_system.spawn<entities::Player_Spectate_Entity>();
    const entity_uid_t damageable_uid = entity_system.spawn<entities::Damageable_Entity>();

    // What `for (pool) for (slot) if (get_render(entity))` used to produce.
    std::vector<std::pair<entity_uid_t, const void *>> expected_render;
    for (Entity_Pool &pool : entity_system.pools)
    {
      for (uint32_t slot = 0; slot < pool.count; ++slot)
      {
        const entities::Entity     *entity = pool.at(slot);
        const entities::Render *render = entities::get_render(entity);
        if (render)
          expected_render.push_back({entity->entity_id, render});
      }
    }

    if (expected_render.size() != 6)
    {
      log_error("the brute-force walk found {} renderable entities; 6 were spawned",
                expected_render.size());
      return 1;
    }

    std::vector<std::pair<entity_uid_t, const void *>> actual_render;
    for (auto [entity, render] : entity_system.entities_with<entities::Render>())
    {
      // Same object, not merely the same values: the row's component reference
      // must point INTO the pooled entity, which is what makes writing through
      // it write the entity.
      if (&render != entities::get_render(&entity))
      {
        log_error("entities_with<Render> handed a component that is not the entity's own "
                  "(uid {})",
                  entity.entity_id);
        return 1;
      }
      actual_render.push_back({entity.entity_id, &render});
    }

    // Both walk pools in type order then slot order, so this is an exact
    // sequence compare rather than a set compare.
    if (actual_render != expected_render)
    {
      log_error("entities_with<Render> visited {} entities; the brute-force walk visited {} "
                "(or visited them in a different order)",
                actual_render.size(), expected_render.size());
      return 1;
    }

    uint32_t volume_count = 0;
    for (auto [entity, volume] : entity_system.entities_with<entities::Box_Volume>())
    {
      if (&volume != entities::get_box_volume(&entity))
      {
        log_error("entities_with<Box_Volume> handed a foreign component (uid {})",
                  entity.entity_id);
        return 1;
      }
      ++volume_count;
    }
    if (volume_count != 2)
    {
      log_error("entities_with<Box_Volume> found {} entities; a Trigger_Volume and a "
                "Damageable were spawned",
                volume_count);
      return 1;
    }

    // The intersection form. Exactly one spawned type declares both, so the
    // fold over the pack must AND the bits: taking either alone over-matches
    // (the trigger, or the five other renderables) and shows up here as a
    // second row.
    uint32_t both_count = 0;
    for (auto [entity, render, volume] :
         entity_system.entities_with<entities::Render, entities::Box_Volume>())
    {
      (void)render;
      (void)volume;
      if (entity.entity_id != damageable_uid)
      {
        log_error("entities_with<Render, Box_Volume> matched uid {}; only the Damageable "
                  "has both",
                  entity.entity_id);
        return 1;
      }
      ++both_count;
    }
    if (both_count != 1)
    {
      log_error("entities_with<Render, Box_Volume> matched {} entities; one Damageable was "
                "spawned",
                both_count);
      return 1;
    }

    // The empty case: begin() must settle all the way to end() rather than
    // stopping on the first pool that happens to be empty.
    Entity_System empty_system;
    if (empty_system.entities_with<entities::Render>().begin() !=
        empty_system.entities_with<entities::Render>().end())
    {
      log_error("entities_with<Render> on an empty Entity_System is not empty");
      return 1;
    }
  }

  // --- entities_with_trait<Trait_T>: the trait aggregate ----------------------
  //
  // The component view above asks what the LAYOUT says; this asks what
  // entities.def DECLARES, and the two are free to disagree the moment a type
  // gains a component without opting into the trait that reads it. Same
  // stride/offset hazard, so the same guard: a comparison against the
  // brute-force walk, entity by entity, not a count.
  {
    game_session_t trait_session = build_session(test_map);
    Entity_System &entity_system = trait_session.entity_system;

    // Two Mortal types (one of them twice, so the inner slot walk has somewhere
    // to go), two Switchable ones, one Objective, and two carrying no trait at
    // all -- the pools that must be skipped.
    entity_system.spawn<entities::Player_Entity>();
    entity_system.spawn<entities::Damageable_Entity>();
    entity_system.spawn<entities::Damageable_Entity>();
    entity_system.spawn<entities::Trigger_Volume_Entity>();
    entity_system.spawn<entities::Jump_Pad_Entity>();
    entity_system.spawn<entities::Game_Rules_Entity>();
    entity_system.spawn<entities::Rocket_Entity>();
    entity_system.spawn<entities::Physics_Body_Entity>();

    // What `for (pool) for (slot) if (is<Mortal>(entity))` produces.
    std::vector<std::pair<entity_uid_t, const void *>> expected_mortal;
    for (Entity_Pool &pool : entity_system.pools)
    {
      for (uint32_t slot = 0; slot < pool.count; ++slot)
      {
        const entities::Entity *entity = pool.at(slot);
        if (!entities::is<entities::Mortal>(*entity))
          continue;
        expected_mortal.push_back(
            {entity->entity_id, entities::get_component<entities::Health>(entity)});
      }
    }

    // Or the sequence compare below passes on two empty lists.
    if (expected_mortal.size() < 3)
    {
      log_error("the brute-force walk found {} Mortal entities; a Player and two Damageables "
                "were spawned",
                expected_mortal.size());
      return 1;
    }

    std::vector<std::pair<entity_uid_t, const void *>> actual_mortal;
    for (auto [entity, health] : entity_system.entities_with_trait<entities::Mortal>())
    {
      // Same object, not merely the same values: `requires Health` is what makes
      // this reference non-null by declaration, and it must point INTO the
      // pooled entity.
      if (&health != entities::get_component<entities::Health>(&entity))
      {
        log_error("entities_with_trait<Mortal> handed a component that is not the entity's "
                  "own (uid {})",
                  entity.entity_id);
        return 1;
      }
      actual_mortal.push_back({entity.entity_id, &health});
    }

    // Both walk pools in type order then slot order, so this is an exact
    // sequence compare rather than a set compare.
    if (actual_mortal != expected_mortal)
    {
      log_error("entities_with_trait<Mortal> visited {} entities; the brute-force walk "
                "visited {} (or visited them in a different order)",
                actual_mortal.size(), expected_mortal.size());
      return 1;
    }

    // A trait with more than one `requires`, so the pack is expanded in
    // DECLARATION order -- Box_Volume then Enabled. Swapping the two here would
    // still compile and would hand back a Box_Volume's bytes as an Enabled.
    uint32_t touchable_count = 0;
    for (auto [entity, box, switch_state] :
         entity_system.entities_with_trait<entities::Touchable>())
    {
      if (&box != entities::get_component<entities::Box_Volume>(&entity) ||
          &switch_state != entities::get_component<entities::Enabled>(&entity))
      {
        log_error("entities_with_trait<Touchable> handed the requires pack out of order "
                  "(uid {})",
                  entity.entity_id);
        return 1;
      }
      ++touchable_count;
    }
    if (touchable_count != 2)
    {
      log_error("entities_with_trait<Touchable> found {} entities; a Trigger_Volume and a "
                "Jump_Pad were spawned",
                touchable_count);
      return 1;
    }

    // A trait with NO `requires`: the row is the Entity alone rather than a
    // one-element structured binding, which is what the empty-pack arm of
    // Pool_View::row_t exists for.
    uint32_t objective_count = 0;
    for (entities::Entity &entity : entity_system.entities_with_trait<entities::Objective>())
    {
      if (!entities::is<entities::Objective>(entity))
      {
        log_error("entities_with_trait<Objective> visited uid {}, which is not Objective",
                  entity.entity_id);
        return 1;
      }
      ++objective_count;
    }
    if (objective_count != 1)
    {
      log_error("entities_with_trait<Objective> found {} entities; one Game_Rules was spawned",
                objective_count);
      return 1;
    }

    // The trait filter is not the component filter. Nothing carries Playback
    // except the one Playable type, and none was spawned -- an over-matching
    // trait mask (a shifted bit, say) shows up here as a non-empty result.
    for (auto [entity, playback] : entity_system.entities_with_trait<entities::Playable>())
    {
      (void)playback;
      log_error("entities_with_trait<Playable> matched uid {}; no Sound_Emitter was spawned",
                entity.entity_id);
      return 1;
    }

    // The empty case: begin() must settle all the way to end() rather than
    // stopping on the first pool that happens to be empty.
    Entity_System empty_system;
    if (empty_system.entities_with_trait<entities::Mortal>().begin() !=
        empty_system.entities_with_trait<entities::Mortal>().end())
    {
      log_error("entities_with_trait<Mortal> on an empty Entity_System is not empty");
      return 1;
    }
  }

  // --- The tie, and the bit that comes out of it (prediction_def.md §4) -------
  //
  // Three questions in one fixture, because they are one fact seen from three
  // places: build_session derives the reverse direction, the collect turns a
  // switch into a bitset keyed the way a BVH leaf is keyed, and the queries skip
  // exactly what the bitset names.
  {
    map_t tie_map;
    tie_map.name = "Tie Map";

    // A wall the ray hits first, and a wall behind it that it must reach once
    // the first is switched off. Both boxes, 20 units apart along +X.
    const entity_uid_t near_wall = tie_map.add_geometry(make_box_brush({0, 0, 0}, {4, 64, 64}));
    const entity_uid_t far_wall  = tie_map.add_geometry(make_box_brush({40, 0, 0}, {4, 64, 64}));

    auto [owner_uid, owner] = spawn_entity(tie_map, entities::entity_type::Geometry_Owner_Entity);
    if (!owner)
    {
      log_error("a geometry_owner_entity would not spawn");
      return 1;
    }
    set_owner_uid(tie_map.find_geometry_by_uid(near_wall)->value, owner_uid);

    game_session_t tie_session = build_session(tie_map);

    // Derived, and keyed by INDEX -- the session's geometry order, which is the
    // map's, which is what Collision_Id::index names.
    if (tie_session.owner_of.size() != 2 || tie_session.owner_of[0] != owner_uid ||
        tie_session.owner_of[1] != null_entity_uid)
    {
      log_error("build_session did not derive owner_of from the geometry's owner key");
      return 1;
    }

    const vec3f origin{-100.f, 0.f, 0.f};
    const vec3f forward{1.f, 0.f, 0.f};

    disabled_geometry_t disabled;
    collect_disabled_geometry(tie_session.entity_system, tie_session.owner_of, disabled);
    if (disabled.size() != 2 || disabled[0] != 0 || disabled[1] != 0)
    {
      log_error("an enabled owner disabled something");
      return 1;
    }

    ray_hit_result_t hit{};
    if (!bvh_intersect_ray(tie_session.bvh, origin, forward, hit, disabled) ||
        tie_session.geometry[hit.id.index].uid != near_wall)
    {
      log_error("the ray did not hit the near wall while it was switched on");
      return 1;
    }

    tie_session.entity_system.get<entities::Geometry_Owner_Entity>(owner_uid)->switch_state.value = false;
    collect_disabled_geometry(tie_session.entity_system, tie_session.owner_of, disabled);
    if (disabled.size() != 2 || disabled[0] == 0 || disabled[1] != 0)
    {
      log_error("switching the owner off did not reach the geometry it owns");
      return 1;
    }

    // The whole point: the SAME tree, the SAME ray, a different answer -- and
    // the answer with no bitset is unchanged, which is what lets the bake, the
    // editor and every other BVH go on passing nothing.
    hit = {};
    if (!bvh_intersect_ray(tie_session.bvh, origin, forward, hit, disabled) ||
        tie_session.geometry[hit.id.index].uid != far_wall)
    {
      log_error("a ray through a disabled brush did not reach what is behind it");
      return 1;
    }

    hit = {};
    if (!bvh_intersect_ray(tie_session.bvh, origin, forward, hit) ||
        tie_session.geometry[hit.id.index].uid != near_wall)
    {
      log_error("the same ray with an empty bitset stopped naming the near wall");
      return 1;
    }

    // A point inside the disabled wall is not inside a solid any more: the
    // probe goes through bvh_intersect_aabb, so one filter covers all three
    // queries rather than three that can disagree.
    if (!bvh_point_is_inside_solid(tie_session.bvh, {0.f, 0.f, 0.f}) ||
        bvh_point_is_inside_solid(tie_session.bvh, {0.f, 0.f, 0.f}, disabled))
    {
      log_error("bvh_point_is_inside_solid does not honour the disabled set");
      return 1;
    }
  }

  // A mover's geometry is in the per-tick cut and not in the tree (mover_def.md ss9 step 2).
  {
    map_t mover_map;
    const entity_uid_t platform = mover_map.add_geometry(make_box_brush({0, 0, 0}, {16, 4, 16}));
    const entity_uid_t wall     = mover_map.add_geometry(make_box_brush({0, 0, 200}, {16, 16, 4}));

    auto [start_uid, start_entity] = spawn_entity(mover_map, entities::entity_type::Path_Node_Entity);
    auto [end_uid, end_entity]     = spawn_entity(mover_map, entities::entity_type::Path_Node_Entity);
    auto [mover_uid, mover_entity] = spawn_entity(mover_map, entities::entity_type::Mover_Entity);

    entities::Path_Node_Entity* start = entities::entity_as<entities::Path_Node_Entity>(start_entity.get());
    entities::Path_Node_Entity* end   = entities::entity_as<entities::Path_Node_Entity>(end_entity.get());
    entities::Mover_Entity*     mover = entities::entity_as<entities::Mover_Entity>(mover_entity.get());
    start->position    = {0, 0, 0};
    start->next        = end_uid;
    end->position      = {0, 100, 0};
    mover->position    = {300, 40, -300}; // an icon handle, off the lift: it must move nothing
    mover->follow.from = start_uid;
    mover->follow.segment_start_tick = 1;
    set_owner_uid(mover_map.find_geometry_by_uid(platform)->value, mover_uid);

    if (!validate_map_paths(mover_map).empty())
    {
      log_error("a well-formed chain was refused");
      return 1;
    }

    const game_session_t mover_session = build_session(mover_map);
    if (mover_session.owner_of.size() != 2 || mover_session.owner_of[0] != mover_uid)
    {
      log_error("build_session did not accept a mover as a brush's owner");
      return 1;
    }

    const auto rest = mover_session.mover_rests.find(mover_uid);
    if (rest == mover_session.mover_rests.end() || rest->second.pieces.empty())
    {
      log_error("the mover's brush did not reach mover_rests");
      return 1;
    }
    if (linalg::length(rest->second.frame.position - start->position) > 1e-6f)
    {
      log_error("the rest frame is not the authored start node");
      return 1;
    }

    ray_hit_result_t hit;
    if (bvh_intersect_ray(mover_session.bvh, {0.f, 50.f, 0.f}, {0.f, -1.f, 0.f}, hit))
    {
      log_error("a ray down onto the mover's brush hit the tree, which should not hold it");
      return 1;
    }
    hit = {};
    if (!bvh_intersect_ray(mover_session.bvh, {0.f, 0.f, 100.f}, {0.f, 0.f, 1.f}, hit) ||
        mover_session.geometry[hit.id.index].uid != wall)
    {
      log_error("the plain wall beside the mover left the tree too");
      return 1;
    }

    std::vector<mover_t> movers;
    collect_movers(mover_session.entity_system, mover_session.path_links, mover_session.mover_rests,
                   31, 60.0f, movers);
    if (movers.size() != 1 || movers[0].uid != mover_uid || movers[0].pieces.size() != rest->second.pieces.size())
    {
      log_error("collect_movers did not cut the mover with its pieces");
      return 1;
    }
    const aabb_bounds_t moved = movers[0].pieces[0].bounds;
    if (std::fabs(moved.min.y - 46.f) > 1e-3f || std::fabs(moved.max.y - 54.f) > 1e-3f)
    {
      log_error("halfway along a 100-unit rise the platform spans y [{}, {}], not [46, 54]",
                moved.min.y, moved.max.y);
      return 1;
    }
    if (std::fabs(movers[0].swept_bounds.min.y - (100.f * 29.f / 60.f - 4.f)) > 1e-3f ||
        std::fabs(movers[0].swept_bounds.max.y - 54.f) > 1e-3f)
    {
      log_error("the swept bounds do not cover both ends of the tick");
      return 1;
    }

    map_t broken_chain = mover_map;
    entities::entity_as<entities::Path_Node_Entity>(broken_chain.find_by_uid(start_uid)->entity.get())->next = wall;
    entities::entity_as<entities::Mover_Entity>(broken_chain.find_by_uid(mover_uid)->entity.get())->follow.from = 4242;
    if (validate_map_paths(broken_chain).size() != 2)
    {
      log_error("a next naming a brush and a mover starting from nothing were not both refused");
      return 1;
    }
  }

  // Every object along a ray, nearest first, once each: the editor's click cycle.
  {
    map_t stacked_map;
    const entity_uid_t near_box   = stacked_map.add_geometry(make_box_brush({0, 0, 0}, {8, 8, 8}));
    const entity_uid_t far_box    = stacked_map.add_geometry(make_box_brush({0, 0, 200}, {8, 8, 8}));
    const entity_uid_t middle_box = stacked_map.add_geometry(make_box_brush({0, 0, 100}, {8, 8, 8}));
    (void)stacked_map.add_geometry(make_box_brush({100, 0, 100}, {8, 8, 8}));
    const game_session_t stacked = build_session(stacked_map);

    std::vector<ray_hit_result_t> hits;
    bvh_intersect_ray_all(stacked.bvh, {0.f, 0.f, -100.f}, {0.f, 0.f, 1.f}, hits);
    const auto uid_of = [&](const ray_hit_result_t& hit) { return stacked.geometry[hit.id.index].uid; };
    if (hits.size() != 3 || uid_of(hits[0]) != near_box || uid_of(hits[1]) != middle_box ||
        uid_of(hits[2]) != far_box)
    {
      log_error("bvh_intersect_ray_all did not answer the three stacked boxes nearest first");
      return 1;
    }

    ray_hit_result_t nearest;
    if (!bvh_intersect_ray(stacked.bvh, {0.f, 0.f, -100.f}, {0.f, 0.f, 1.f}, nearest) ||
        nearest.id.index != hits[0].id.index || nearest.t != hits[0].t)
    {
      log_error("bvh_intersect_ray and the first of bvh_intersect_ray_all disagree");
      return 1;
    }
  }

  log_error("Session Test Passed!");
  return 0;
}
