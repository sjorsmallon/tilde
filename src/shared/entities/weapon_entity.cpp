#include "weapon_entity.hpp"

namespace network
{

DEFINE_SCHEMA_CLASS(Weapon_Entity, Entity)
{
  BEGIN_SCHEMA_FIELDS()
  REGISTER_SCHEMA_FIELD(ammo);
  REGISTER_SCHEMA_FIELD(active_weapon_id);
  REGISTER_SCHEMA_FIELD(asset_id);
  REGISTER_SCHEMA_FIELD(render);
  END_SCHEMA_FIELDS()
}

} // namespace network