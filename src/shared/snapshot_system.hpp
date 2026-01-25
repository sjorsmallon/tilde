#pragma once

#include "ecs.hpp"
#include "game.pb.h"
#include "log.hpp"

namespace game {

// --- ECS Component Helpers ---
// Ideally these would be in a component_registry.hpp or similar
struct Position {
  float x, y;
};
struct Velocity {
  float dx, dy;
};

inline Snapshot create_snapshot(ecs::Registry &registry, uint32_t tick_id) {
  Snapshot snapshot;
  snapshot.set_tick_id(tick_id);
  snapshot.set_delta_from_tick_id(0); // Full snapshot for now

  // We need a way to iterate ALL entities in the registry.
  // Our Registry implementation currently has `entity_masks` which maps Entity
  // -> Mask. Ideally we'd iterate that. Since `entity_masks` is private, we
  // should expose an iterator or use pools. For now, let's assume we can get a
  // list of active entities. Wait, ecs::Registry doesn't expose iteration yet.
  // I need to update ecs.hpp to allow getting all entities.
  // OR, I can iterate a "primary" component pool (like Position) if all network
  // entities have Position.

  // Let's assume all networked entities MUST have a Position for now.
  // A better ECS would provide a `view<T...>` or `each()`.

  auto &pos_pool = registry.get_pool<Position>();

  // Iterate the dense vector of the pool (if we want speed) OR index_to_entity
  // map
  for (auto const &[index, entity] : pos_pool.index_to_entity) {
    EntityState *state = snapshot.add_entities();
    state->set_entity_id(entity);

    // Position (Guaranteed by loop)
    const auto &pos = pos_pool.data[index];
    state->mutable_position()->set_x(pos.x);
    state->mutable_position()->set_y(pos.y);
    state->mutable_position()->set_z(0.0f); // 2D for now

    // Velocity (Optional)
    if (registry.has_component<Velocity>(entity)) {
      const auto &vel = registry.get_component<Velocity>(entity);
      state->mutable_velocity()->set_x(vel.dx);
      state->mutable_velocity()->set_y(vel.dy);
      state->mutable_velocity()->set_z(0.0f);
    }
  }

  return snapshot;
}

inline void apply_snapshot(ecs::Registry &registry, const Snapshot &snapshot) {
  // Naive Approach:
  // 1. Iterate snapshot entities.
  // 2. If entity exists locally, update it.
  // 3. If not, create it.
  // 4. (Hard part) Remove entities not in snapshot?
  //    For a FULL snapshot, yes, we should sync completely.

  if (snapshot.delta_from_tick_id() != 0) {
    log_error("Delta snapshots not yet supported!");
    return;
  }

  // Track which entities we touched to identify removals
  // (Skipping removal logic for this simple draft to avoid complexity with ID
  // mapping) Real implementation would map ServerEntityID -> LocalEntityID. For
  // this draft, let's assume LocalEntityID = ServerEntityID (Shared Registry
  // logic from before, but separated). Actually, in a replica model, Client
  // Registry usually maps ServerID -> ClientID. To keep it simple: Client sets
  // its local entity ID to match Server ID directly.
  // ecs::Registry::create_entity uses a counter. We might need
  // `create_entity_with_id(id)`.

  for (const auto &entity_state : snapshot.entities()) {
    ecs::Entity id = entity_state.entity_id();

    // Ensure entity technically "exists" or components are just added
    // In our ECS, `add_component` doesn't strictly require `create_entity`
    // called first IF we assume IDs are valid. But `create_entity` initializes
    // the mask. We need a `ensure_entity(id)` or similar in ECS. For now, let's
    // just add components.

    // Position
    if (entity_state.has_position()) {
      game::Position pos;
      pos.x = entity_state.position().x();
      pos.y = entity_state.position().y();
      registry.add_component<Position>(id, pos);
    }

    // Velocity
    if (entity_state.has_velocity()) {
      game::Velocity vel;
      vel.dx = entity_state.velocity().x();
      vel.dy = entity_state.velocity().y();
      registry.add_component<Velocity>(id, vel);
    }
  }
}

} // namespace game
