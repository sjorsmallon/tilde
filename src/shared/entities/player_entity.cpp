#include "player_entity.hpp"

namespace network
{

// Bake the schema (offsets and types)
BEGIN_SCHEMA(Player_Entity)
DEFINE_FIELD(position, Field_Type::Vec3f)
DEFINE_FIELD(view_angle_yaw, Field_Type::Float32)
DEFINE_FIELD(view_angle_pitch, Field_Type::Float32)

DEFINE_FIELD(health, Field_Type::Int32)
DEFINE_FIELD(ammo, Field_Type::Int32)
DEFINE_FIELD(active_weapon_id, Field_Type::Int32)
END_SCHEMA(Player_Entity)

} // namespace network
