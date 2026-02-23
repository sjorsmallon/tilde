#include "rocket_entity.hpp"

namespace network
{

DEFINE_SCHEMA_CLASS(Rocket_Entity, Entity)
{
  BEGIN_SCHEMA_FIELDS()
    REGISTER_SCHEMA_FIELD(velocity);
    REGISTER_SCHEMA_FIELD(lifetime);
    REGISTER_SCHEMA_FIELD(damage_radius);
    REGISTER_SCHEMA_FIELD(damage_amount);
    REGISTER_SCHEMA_FIELD(knockback_force);
    REGISTER_SCHEMA_FIELD(owner_id);
    REGISTER_SCHEMA_FIELD(render);
    REGISTER_SCHEMA_FIELD(hitbox);
  END_SCHEMA_FIELDS()
}

} // namespace network
