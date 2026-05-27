#pragma once

#include "../entity.hpp"

namespace network
{

enum class light_type_t : int32_t
{
  Point = 0,
  Spot = 1,
  Directional = 2
};

class Light_Entity : public Entity_Of<::entity_type::LIGHT>
{
public:
  SCHEMA_FIELD(vec3f, direction, Schema_Flags::Networked | Schema_Flags::Editable);
  SCHEMA_FIELD(vec3f, color, Schema_Flags::Networked | Schema_Flags::Editable);
  SCHEMA_FIELD(float, intensity, Schema_Flags::Networked | Schema_Flags::Editable);
  SCHEMA_FIELD(float, range, Schema_Flags::Networked | Schema_Flags::Editable);
  SCHEMA_FIELD(float, spot_inner_degrees, Schema_Flags::Networked | Schema_Flags::Editable);
  SCHEMA_FIELD(float, spot_outer_degrees, Schema_Flags::Networked | Schema_Flags::Editable);
  SCHEMA_FIELD(int32, light_type, Schema_Flags::Networked | Schema_Flags::Editable);

  DECLARE_SCHEMA(Light_Entity)
};

SCHEMA_NAME_FOR_TYPE(Light_Entity)

} // namespace network
