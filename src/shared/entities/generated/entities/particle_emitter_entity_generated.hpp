// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
//
// Particle_Emitter_Entity: what it IS, and what it can be TOLD.
//
// The includes are relative to THIS file rather than to src/shared: a
// quoted include is resolved against the including file's directory first.
#pragma once

#include "../entities_core_generated.hpp"

namespace entities
{

struct Particle_Emitter_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Particle_Emitter_Entity;

  Particle_Emitter_Entity();

  assets::texture_asset sprite;
  float emit_rate;
  int32_t max_particles;
  float lifetime_min;
  float lifetime_max;
  float velocity_min;
  float velocity_max;
  float spread;
  linalg::vec3f gravity;
  float drag;
  float size_start;
  float size_end;
  float rotation_speed_min;
  float rotation_speed_max;
  linalg::vec3f color_start;
  linalg::vec3f color_end;
  float alpha_start;
  float alpha_end;
};

// The entity pool is a byte buffer: it copies with memcpy and runs no
// destructor. A field that breaks either of these corrupts or leaks
// silently, so the check lives here rather than in a test nobody runs
// before the pool does.
static_assert(std::is_trivially_copyable_v<Particle_Emitter_Entity>,
              "Particle_Emitter_Entity must stay trivially copyable: pooled storage, snapshot "
              "baselines and undo all copy entities with memcpy");
static_assert(std::is_trivially_destructible_v<Particle_Emitter_Entity>,
              "Particle_Emitter_Entity must stay trivially destructible: the entity pool frees a "
              "slot by overwriting it and runs no destructor");
static_assert(std::is_base_of_v<Entity, Particle_Emitter_Entity>,
              "Particle_Emitter_Entity must derive from Entity: the generated tables hand out "
              "Entity* for every entity type");

} // namespace entities
