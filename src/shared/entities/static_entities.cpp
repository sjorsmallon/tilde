#include "static_entities.hpp"

namespace network
{

// AABB Entity
DEFINE_SCHEMA_CLASS(AABB_Entity, Entity)
{
  BEGIN_SCHEMA_FIELDS()
  REGISTER_SCHEMA_FIELD(half_extents);
  REGISTER_SCHEMA_FIELD(render);
  END_SCHEMA_FIELDS()
}

// Wedge Entity
DEFINE_SCHEMA_CLASS(Wedge_Entity, Entity)
{
  BEGIN_SCHEMA_FIELDS()
  REGISTER_SCHEMA_FIELD(half_extents);
  REGISTER_SCHEMA_FIELD(orientation);
  REGISTER_SCHEMA_FIELD(render);
  END_SCHEMA_FIELDS()
}

// Static Mesh Entity
DEFINE_SCHEMA_CLASS(Static_Mesh_Entity, Entity)
{
  BEGIN_SCHEMA_FIELDS()
  REGISTER_SCHEMA_FIELD(render);
  END_SCHEMA_FIELDS()
}

} // namespace network
