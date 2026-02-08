#include "shapes.hpp"

namespace shared
{

BEGIN_SCHEMA(aabb_t)
DEFINE_FIELD(center, network::Field_Type::Vec3f)
DEFINE_FIELD(half_extents, network::Field_Type::Vec3f)
END_SCHEMA(aabb_t)

BEGIN_SCHEMA(pyramid_t)
DEFINE_FIELD(position, network::Field_Type::Vec3f)
DEFINE_FIELD(size, network::Field_Type::Float32)
DEFINE_FIELD(height, network::Field_Type::Float32)
END_SCHEMA(pyramid_t)

BEGIN_SCHEMA(wedge_t)
DEFINE_FIELD(center, network::Field_Type::Vec3f)
DEFINE_FIELD(half_extents, network::Field_Type::Vec3f)
DEFINE_FIELD(orientation, network::Field_Type::Int32)
END_SCHEMA(wedge_t)

} // namespace shared
