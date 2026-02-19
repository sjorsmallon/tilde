#pragma once

#include "../entity.hpp"

namespace network
{

class Rocket_Entity : public Entity
{
public:
  SCHEMA_FIELD(vec3f, velocity, Schema_Flags::Networked | Schema_Flags::Editable);
  SCHEMA_FIELD(float32, lifetime, Schema_Flags::Networked | Schema_Flags::Editable);
  SCHEMA_FIELD(float32, damage_radius, Schema_Flags::Networked | Schema_Flags::Editable);
  SCHEMA_FIELD(float32, damage_amount, Schema_Flags::Networked | Schema_Flags::Editable);
  SCHEMA_FIELD(float32, knockback_force, Schema_Flags::Networked | Schema_Flags::Editable);
  SCHEMA_FIELD(render_component_t, render,
               Schema_Flags::Networked | Schema_Flags::Editable);
  DECLARE_SCHEMA(Rocket_Entity)
};

} // namespace network
