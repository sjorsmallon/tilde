#include "particle_emitter_entity.hpp"

namespace network
{

DEFINE_SCHEMA_CLASS(Particle_Emitter_Entity, Entity)
{
  BEGIN_SCHEMA_FIELDS()
  REGISTER_SCHEMA_FIELD(sprite_path);
  REGISTER_SCHEMA_FIELD(emit_rate);
  REGISTER_SCHEMA_FIELD(max_particles);
  REGISTER_SCHEMA_FIELD(lifetime_min);
  REGISTER_SCHEMA_FIELD(lifetime_max);
  REGISTER_SCHEMA_FIELD(velocity_min);
  REGISTER_SCHEMA_FIELD(velocity_max);
  REGISTER_SCHEMA_FIELD(spread);
  REGISTER_SCHEMA_FIELD(gravity);
  REGISTER_SCHEMA_FIELD(drag);
  REGISTER_SCHEMA_FIELD(size_start);
  REGISTER_SCHEMA_FIELD(size_end);
  REGISTER_SCHEMA_FIELD(rotation_speed_min);
  REGISTER_SCHEMA_FIELD(rotation_speed_max);
  REGISTER_SCHEMA_FIELD(color_start);
  REGISTER_SCHEMA_FIELD(color_end);
  REGISTER_SCHEMA_FIELD(alpha_start);
  REGISTER_SCHEMA_FIELD(alpha_end);
  REGISTER_SCHEMA_FIELD(emitter_lifetime);
  REGISTER_SCHEMA_FIELD(parent_entity_id);
  END_SCHEMA_FIELDS()
}

} // namespace network
