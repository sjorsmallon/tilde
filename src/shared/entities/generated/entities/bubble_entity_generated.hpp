// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
//
// Bubble_Entity: what it IS, and what it can be TOLD.
//
// The includes are relative to THIS file rather than to src/shared: a
// quoted include is resolved against the including file's directory first.
#pragma once

#include "../entities_core_generated.hpp"

namespace entities
{

struct Bubble_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Bubble_Entity;

  Bubble_Entity() { type = entity_type::Bubble_Entity; }

  Projectile projectile = {.weapon_id = Weapon::Bubble};
  Fixed_Arc_Flight flight = {};
  uint32_t popped_tick = {};
  shared::entity_uid_t popped_by = {};
  linalg::vec3f popped_direction = {};
  float swell_seconds = 0.08f;
  float swell_scale = 1.2f;
  float peel_seconds = 0.2f;
  float linger_seconds = 0.35f;
  float flight_seconds = 1.0f;
  float rest_seconds = 8.0f;
  float arm_seconds = 0.2f;
  float radius = 32.0f;
  float bounce_speed = 700.0f;
  Render render = {.mesh = assets::mesh_asset::high_res_sphere, .scale = {2.0f, 2.0f, 2.0f}, .material = {.shader_type = Shader_Type::Ghost, .color = {0.55f, 0.85f, 1.0f}}};
};

// The entity pool is a byte buffer: it copies with memcpy and runs no
// destructor. A field that breaks either of these corrupts or leaks
// silently, so the check lives here rather than in a test nobody runs
// before the pool does.
static_assert(std::is_trivially_copyable_v<Bubble_Entity>,
              "Bubble_Entity must stay trivially copyable: pooled storage, snapshot "
              "baselines and undo all copy entities with memcpy");
static_assert(std::is_trivially_destructible_v<Bubble_Entity>,
              "Bubble_Entity must stay trivially destructible: the entity pool frees a "
              "slot by overwriting it and runs no destructor");
static_assert(std::is_base_of_v<Entity, Bubble_Entity>,
              "Bubble_Entity must derive from Entity: the generated tables hand out "
              "Entity* for every entity type");

} // namespace entities
