#include "static_entities.hpp"

namespace network
{

// AABB Entity
BEGIN_SCHEMA(AABB_Entity)
DEFINE_FIELD(center, Field_Type::Vec3f)
DEFINE_FIELD(half_extents, Field_Type::Vec3f)
END_SCHEMA(AABB_Entity)

// Wedge Entity
BEGIN_SCHEMA(Wedge_Entity)
DEFINE_FIELD(center, Field_Type::Vec3f)
DEFINE_FIELD(half_extents, Field_Type::Vec3f)
DEFINE_FIELD(orientation, Field_Type::Int32)
END_SCHEMA(Wedge_Entity)

// Static Mesh Entity
BEGIN_SCHEMA(Static_Mesh_Entity)
DEFINE_FIELD(position, Field_Type::Vec3f)
DEFINE_FIELD(rotation, Field_Type::Vec3f)
DEFINE_FIELD(scale, Field_Type::Vec3f)
DEFINE_FIELD(asset_id, Field_Type::Int32)
END_SCHEMA(Static_Mesh_Entity)

} // namespace network
