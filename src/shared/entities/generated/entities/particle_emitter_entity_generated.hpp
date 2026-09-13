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

  Particle_Emitter_Entity() { type = entity_type::Particle_Emitter_Entity; }

  assets::texture_asset sprite = assets::texture_asset::Smoke;
  float emit_rate = 20.0f;
  int32_t max_particles = 64;
  float lifetime_min = 0.5f;
  float lifetime_max = 1.5f;
  float velocity_min = 2.0f;
  float velocity_max = 5.0f;
  float spread = 0.5f;
  linalg::vec3f gravity = {0.0f, 0.5f, 0.0f};
  float drag = 0.3f;
  float size_start = 0.5f;
  float size_end = 2.0f;
  float rotation_speed_min = -1.0f;
  float rotation_speed_max = 1.0f;
  linalg::vec3f color_start = {1.0f, 1.0f, 1.0f};
  linalg::vec3f color_end = {0.5f, 0.5f, 0.5f};
  float alpha_start = 0.8f;
  float alpha_end = 0.0f;
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
