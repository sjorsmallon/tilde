#pragma once

#include "../entity.hpp"

namespace network
{

class Rocket_Entity : public Entity_Of<::entity_type::ROCKET>
{
public:
  SCHEMA_FIELD(vec3f, velocity, Schema_Flags::Networked | Schema_Flags::Editable);
  SCHEMA_FIELD(float32, lifetime, Schema_Flags::Networked | Schema_Flags::Editable);
  SCHEMA_FIELD(float32, damage_radius, Schema_Flags::Networked | Schema_Flags::Editable);
  SCHEMA_FIELD(float32, damage_amount, Schema_Flags::Networked | Schema_Flags::Editable);
  SCHEMA_FIELD(float32, knockback_force, Schema_Flags::Networked | Schema_Flags::Editable);
  SCHEMA_FIELD(uint32, owner_id, Schema_Flags::Networked);  // entity_uid_t of who fired this rocket; 0 = no owner
  SCHEMA_FIELD(render_component_t, render,
               Schema_Flags::Networked | Schema_Flags::Editable);
  SCHEMA_FIELD(hitbox_component_t, hitbox,
               Schema_Flags::Networked | Schema_Flags::Editable);
  DECLARE_SCHEMA(Rocket_Entity)
};

// Schema name registration (must be at namespace scope)
SCHEMA_NAME_FOR_TYPE(Rocket_Entity)

} // namespace network
