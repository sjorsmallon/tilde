// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
//
// Guided_Rocket_Entity: what it IS, and what it can be TOLD.
//
// The includes are relative to THIS file rather than to src/shared: a
// quoted include is resolved against the including file's directory first.
#pragma once

#include "../entities_core_generated.hpp"

namespace entities
{

struct Guided_Rocket_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Guided_Rocket_Entity;

  Guided_Rocket_Entity() { type = entity_type::Guided_Rocket_Entity; }

  shared::entity_uid_t pilot_uid = {};
  Weapon weapon_id = Weapon::Guided_Rocket;
  Render render = {.mesh = assets::mesh_asset::rocket};
};

// The entity pool is a byte buffer: it copies with memcpy and runs no
// destructor. A field that breaks either of these corrupts or leaks
// silently, so the check lives here rather than in a test nobody runs
// before the pool does.
static_assert(std::is_trivially_copyable_v<Guided_Rocket_Entity>,
              "Guided_Rocket_Entity must stay trivially copyable: pooled storage, snapshot "
              "baselines and undo all copy entities with memcpy");
static_assert(std::is_trivially_destructible_v<Guided_Rocket_Entity>,
              "Guided_Rocket_Entity must stay trivially destructible: the entity pool frees a "
              "slot by overwriting it and runs no destructor");
static_assert(std::is_base_of_v<Entity, Guided_Rocket_Entity>,
              "Guided_Rocket_Entity must derive from Entity: the generated tables hand out "
              "Entity* for every entity type");

} // namespace entities
