#pragma once

#include "../entity.hpp"

namespace network
{

class Weapon_Entity : public Entity_Of<::entity_type::WEAPON>
{
public:
  SCHEMA_FIELD(int32, ammo, Schema_Flags::Networked | Schema_Flags::Editable);
  SCHEMA_FIELD(int32, active_weapon_id, Schema_Flags::Networked);

  SCHEMA_FIELD(render_component_t, render,
               Schema_Flags::Networked | Schema_Flags::Editable);

  DECLARE_SCHEMA(Weapon_Entity)
};

// Schema name registration (must be at namespace scope)
SCHEMA_NAME_FOR_TYPE(Weapon_Entity)

} // namespace network
