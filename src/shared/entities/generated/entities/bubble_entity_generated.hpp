// Generated from /Users/sjors/Desktop/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
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

  Bubble_Entity();

  Projectile projectile;
  Fixed_Arc_Flight flight;
  uint32_t popped_tick;
  shared::entity_uid_t popped_by;
  linalg::vec3f popped_direction;
  float swell_seconds;
  float swell_scale;
  float peel_seconds;
  float linger_seconds;
  float flight_seconds;
  float rest_seconds;
  float arm_seconds;
  float radius;
  float bounce_speed;
  Render render;
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
