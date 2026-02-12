#include "shapes.hpp"

namespace shared
{

BEGIN_SCHEMA(aabb_t)
REGISTER_FIELD(center)
REGISTER_FIELD(half_extents)
END_SCHEMA(aabb_t)

BEGIN_SCHEMA(pyramid_t)
REGISTER_FIELD(position)
REGISTER_FIELD(size)
REGISTER_FIELD(height)
END_SCHEMA(pyramid_t)

BEGIN_SCHEMA(wedge_t)
REGISTER_FIELD(center)
REGISTER_FIELD(half_extents)
REGISTER_FIELD(orientation)
END_SCHEMA(wedge_t)

} // namespace shared
