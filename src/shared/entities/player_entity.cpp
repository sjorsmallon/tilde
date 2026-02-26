#include "player_entity.hpp"

namespace network
{

DEFINE_SCHEMA_CLASS(Player_Spawn_Entity, Entity)
{
  BEGIN_SCHEMA_FIELDS()
  REGISTER_SCHEMA_FIELD(spawn_type);
  END_SCHEMA_FIELDS()
}

DEFINE_SCHEMA_CLASS(Player_Entity, Entity)
{
  BEGIN_SCHEMA_FIELDS()
  REGISTER_SCHEMA_FIELD(view_angle_yaw);
  REGISTER_SCHEMA_FIELD(view_angle_pitch);
  REGISTER_SCHEMA_FIELD(health);
  REGISTER_SCHEMA_FIELD(ammo);
  REGISTER_SCHEMA_FIELD(active_weapon_id);
  REGISTER_SCHEMA_FIELD(client_slot_index);
  REGISTER_SCHEMA_FIELD(velocity);
  REGISTER_SCHEMA_FIELD(render);
  REGISTER_SCHEMA_FIELD(hitbox);
  END_SCHEMA_FIELDS()
}

} // namespace network
