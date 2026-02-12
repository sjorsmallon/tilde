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

  SCHEMA_FIELD(render_component_t, render,
               Schema_Flags::Networked | Schema_Flags::Editable);

  DECLARE_SCHEMA(Player_Entity)
};

} // namespace network
