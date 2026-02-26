#pragma once

#include "../entity.hpp"

namespace network
{

// Placed in the map editor to mark where players (or bots) spawn.
// Only exists in the map; the server consumes it at load time and removes it.
// position/orientation inherited from Entity.
class Player_Spawn_Entity : public Entity
{
public:
  // 0 = human spawn point (default), 1 = bot spawn point
  SCHEMA_FIELD(int32, spawn_type, Schema_Flags::Saveable | Schema_Flags::Editable);

  DECLARE_SCHEMA(Player_Spawn_Entity)
};

SCHEMA_NAME_FOR_TYPE(Player_Spawn_Entity)

// Runtime networked player entity, created by the server when a client connects.
// Never saved in map files.
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

SCHEMA_NAME_FOR_TYPE(Player_Entity)

} // namespace network
