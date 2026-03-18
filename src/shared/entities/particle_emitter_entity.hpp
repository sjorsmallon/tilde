#pragma once

#include "../entity.hpp"

namespace network
{

class Particle_Emitter_Entity : public Entity
{
public:
  // Sprite sheet path
  SCHEMA_FIELD_DEFAULT(pascal_string, sprite_path,
                       Schema_Flags::Editable | Schema_Flags::Saveable,
                       "resources/sprites/smoke.png");

  // Emission
  SCHEMA_FIELD_DEFAULT(float32, emit_rate,
                       Schema_Flags::Editable | Schema_Flags::Saveable,
                       20.0f);
  SCHEMA_FIELD_DEFAULT(int32, max_particles,
                       Schema_Flags::Editable | Schema_Flags::Saveable,
                       64);

  // Lifetime
  SCHEMA_FIELD_DEFAULT(float32, lifetime_min,
                       Schema_Flags::Editable | Schema_Flags::Saveable,
                       0.5f);
  SCHEMA_FIELD_DEFAULT(float32, lifetime_max,
                       Schema_Flags::Editable | Schema_Flags::Saveable,
                       1.5f);

  // Velocity
  SCHEMA_FIELD_DEFAULT(float32, velocity_min,
                       Schema_Flags::Editable | Schema_Flags::Saveable,
                       2.0f);
  SCHEMA_FIELD_DEFAULT(float32, velocity_max,
                       Schema_Flags::Editable | Schema_Flags::Saveable,
                       5.0f);
  SCHEMA_FIELD_DEFAULT(float32, spread,
                       Schema_Flags::Editable | Schema_Flags::Saveable,
                       0.5f);

  // Physics
  SCHEMA_FIELD_DEFAULT(vec3f, gravity,
                       Schema_Flags::Editable | Schema_Flags::Saveable,
                       (vec3f{0, 0.5f, 0}));
  SCHEMA_FIELD_DEFAULT(float32, drag,
                       Schema_Flags::Editable | Schema_Flags::Saveable,
                       0.3f);

  // Size over lifetime
  SCHEMA_FIELD_DEFAULT(float32, size_start,
                       Schema_Flags::Editable | Schema_Flags::Saveable,
                       0.5f);
  SCHEMA_FIELD_DEFAULT(float32, size_end,
                       Schema_Flags::Editable | Schema_Flags::Saveable,
                       2.0f);

  // Rotation speed range (radians/sec)
  SCHEMA_FIELD_DEFAULT(float32, rotation_speed_min,
                       Schema_Flags::Editable | Schema_Flags::Saveable,
                       -1.0f);
  SCHEMA_FIELD_DEFAULT(float32, rotation_speed_max,
                       Schema_Flags::Editable | Schema_Flags::Saveable,
                       1.0f);

  // Color over lifetime (ABGR uint32 format, same as renderer)
  SCHEMA_FIELD_DEFAULT(vec3f, color_start,
                       Schema_Flags::Editable | Schema_Flags::Saveable,
                       (vec3f{1.0f, 1.0f, 1.0f}));
  SCHEMA_FIELD_DEFAULT(vec3f, color_end,
                       Schema_Flags::Editable | Schema_Flags::Saveable,
                       (vec3f{0.5f, 0.5f, 0.5f}));
  SCHEMA_FIELD_DEFAULT(float32, alpha_start,
                       Schema_Flags::Editable | Schema_Flags::Saveable,
                       0.8f);
  SCHEMA_FIELD_DEFAULT(float32, alpha_end,
                       Schema_Flags::Editable | Schema_Flags::Saveable,
                       0.0f);

  // Emitter lifetime — how long the emitter itself stays alive (seconds).
  // 0 = infinite (default for placed emitters). >0 = auto-destroy after this
  // many seconds (used for explosions and other transient effects).
  SCHEMA_FIELD_DEFAULT(float32, emitter_lifetime,
                       Schema_Flags::Networked | Schema_Flags::Editable,
                       0.0f);

  // Parent entity to follow (0 = no parent, uses own position)
  SCHEMA_FIELD_DEFAULT(uint64, parent_entity_id,
                       Schema_Flags::Networked | Schema_Flags::Editable,
                       0);

  DECLARE_SCHEMA(Particle_Emitter_Entity)
};

SCHEMA_NAME_FOR_TYPE(Particle_Emitter_Entity)

} // namespace network
