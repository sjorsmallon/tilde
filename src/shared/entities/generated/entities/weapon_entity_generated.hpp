// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
//
// Weapon_Entity: what it IS, and what it can be TOLD.
//
// The includes are relative to THIS file rather than to src/shared: a
// quoted include is resolved against the including file's directory first.
#pragma once

#include "../entities_core_generated.hpp"

namespace entities
{

struct Weapon_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Weapon_Entity;

  Weapon_Entity() { type = entity_type::Weapon_Entity; }

  int32_t ammo = {};
  Weapon weapon_id = {};
  uint32_t owner_uid = {};
  uint64_t next_fire_time = {};
  uint32_t pickup_allowed_tick = {};
  Damage_Type damage_type = Damage_Type::Normal;
  Render render = {.mesh = assets::mesh_asset::Box};
};

// The entity pool is a byte buffer: it copies with memcpy and runs no
// destructor. A field that breaks either of these corrupts or leaks
// silently, so the check lives here rather than in a test nobody runs
// before the pool does.
static_assert(std::is_trivially_copyable_v<Weapon_Entity>,
              "Weapon_Entity must stay trivially copyable: pooled storage, snapshot "
              "baselines and undo all copy entities with memcpy");
static_assert(std::is_trivially_destructible_v<Weapon_Entity>,
              "Weapon_Entity must stay trivially destructible: the entity pool frees a "
              "slot by overwriting it and runs no destructor");
static_assert(std::is_base_of_v<Entity, Weapon_Entity>,
              "Weapon_Entity must derive from Entity: the generated tables hand out "
              "Entity* for every entity type");

} // namespace entities
