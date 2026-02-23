#pragma once

#include "../entity.hpp"

namespace network
{

class Player_Entity : public Entity
{
public:
  SCHEMA_FIELD(float32, view_angle_yaw,
               Schema_Flags::Networked | Schema_Flags::Editable);
  SCHEMA_FIELD(float32, view_angle_pitch,
               Schema_Flags::Networked | Schema_Flags::Editable);

  SCHEMA_FIELD(int32, health, Schema_Flags::Networked | Schema_Flags::Editable);
  SCHEMA_FIELD(int32, ammo, Schema_Flags::Networked | Schema_Flags::Editable);
  SCHEMA_FIELD(int32, active_weapon_id, Schema_Flags::Networked);
  SCHEMA_FIELD(int32, client_slot_index, Schema_Flags::Networked);
  SCHEMA_FIELD(vec3f, velocity, Schema_Flags::Networked);

  SCHEMA_FIELD(render_component_t, render,
               Schema_Flags::Networked | Schema_Flags::Editable);

  // Combat hitbox (separate from physics collision used in player_move)
  // Physics uses 16x36 half-extents, combat hitbox can be tuned independently
  SCHEMA_FIELD(hitbox_component_t, hitbox,
               Schema_Flags::Networked | Schema_Flags::Editable);

  DECLARE_SCHEMA(Player_Entity)
};

// Schema name registration (must be at namespace scope)
SCHEMA_NAME_FOR_TYPE(Player_Entity)

} // namespace network
