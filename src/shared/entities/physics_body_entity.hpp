#pragma once

#include "../entity.hpp"

namespace network
{

// A runtime-spawned, server-authoritative rigid body simulated by Jolt.
// Use this for pushable crates, debris, dropped items — anything dynamic
// that isn't a player or a projectile with bespoke behavior.
//
// shape_type / size are interpreted the same way as hitbox_component_t:
//   - "box":     size = half_extents (x, y, z)
//   - "sphere":  size.x = radius
//   - "capsule": size.x = radius, size.y = half_height (cylinder portion)
//
// The owning physics_state_t holds the actual Jolt body; this entity carries
// the replicated state (position/orientation inherited from Entity, plus
// velocity for interpolation and the shape parameters needed to render).
class Physics_Body_Entity : public Entity
{
public:
  SCHEMA_FIELD(pascal_string, shape_type,
               Schema_Flags::Networked | Schema_Flags::Editable | Schema_Flags::Saveable);
  SCHEMA_FIELD(vec3f, size,
               Schema_Flags::Networked | Schema_Flags::Editable | Schema_Flags::Saveable);
  SCHEMA_FIELD(vec3f, velocity, Schema_Flags::Networked);
  SCHEMA_FIELD(float32, mass,
               Schema_Flags::Editable | Schema_Flags::Saveable);
  SCHEMA_FIELD(render_component_t, render,
               Schema_Flags::Networked | Schema_Flags::Editable | Schema_Flags::Saveable);
  SCHEMA_FIELD(hitbox_component_t, hitbox,
               Schema_Flags::Networked | Schema_Flags::Editable | Schema_Flags::Saveable);

  DECLARE_SCHEMA(Physics_Body_Entity)
};

SCHEMA_NAME_FOR_TYPE(Physics_Body_Entity)

} // namespace network
