#include "trigger_volume_entity.hpp"

namespace network
{

DEFINE_SCHEMA_CLASS(Trigger_Volume_Entity, Entity)
{
  BEGIN_SCHEMA_FIELDS()
  REGISTER_SCHEMA_FIELD(half_extents);
  REGISTER_SCHEMA_FIELD(action);
  END_SCHEMA_FIELDS()
}

} // namespace network
