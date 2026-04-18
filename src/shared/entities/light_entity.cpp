#include "light_entity.hpp"

namespace network
{

DEFINE_SCHEMA_CLASS(Light_Entity, Entity)
{
  BEGIN_SCHEMA_FIELDS()
  REGISTER_SCHEMA_FIELD(direction);
  REGISTER_SCHEMA_FIELD(color);
  REGISTER_SCHEMA_FIELD(intensity);
  REGISTER_SCHEMA_FIELD(range);
  REGISTER_SCHEMA_FIELD(spot_inner_degrees);
  REGISTER_SCHEMA_FIELD(spot_outer_degrees);
  REGISTER_SCHEMA_FIELD(light_type);
  END_SCHEMA_FIELDS()
}

} // namespace network
