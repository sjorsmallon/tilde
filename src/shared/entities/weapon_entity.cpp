#include "weapon_entity.hpp"

namespace network
{

BEGIN_SCHEMA(Weapon_Entity)
DEFINE_FIELD(position, Field_Type::Vec3f)
DEFINE_FIELD(angles, Field_Type::Vec3f)
DEFINE_FIELD(ammo, Field_Type::Int32)
END_SCHEMA(Weapon_Entity)

} // namespace network