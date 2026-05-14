#include "physics_body_entity.hpp"

namespace network
{

DEFINE_SCHEMA_CLASS(Physics_Body_Entity, Entity)
{
  BEGIN_SCHEMA_FIELDS()
    REGISTER_SCHEMA_FIELD(shape_type);
    REGISTER_SCHEMA_FIELD(size);
    REGISTER_SCHEMA_FIELD(velocity);
    REGISTER_SCHEMA_FIELD(mass);
    REGISTER_SCHEMA_FIELD(render);
    REGISTER_SCHEMA_FIELD(hitbox);
  END_SCHEMA_FIELDS()
}

} // namespace network
