#pragma once

#include "../entity.hpp"
#include <string>

namespace network
{

class AABB_Entity : public Entity
{
public:
  SCHEMA_FIELD(vec3f, half_extents,
               Schema_Flags::Networked | Schema_Flags::Editable);

  SCHEMA_FIELD(render_component_t, render,
               Schema_Flags::Networked | Schema_Flags::Editable);

  DECLARE_SCHEMA(AABB_Entity)
};

class Wedge_Entity : public Entity
{
public:
  SCHEMA_FIELD(vec3f, half_extents,
               Schema_Flags::Networked | Schema_Flags::Editable);
  SCHEMA_FIELD(int32, orientation,
               Schema_Flags::Networked | Schema_Flags::Editable);

  SCHEMA_FIELD(render_component_t, render,
               Schema_Flags::Networked | Schema_Flags::Editable);

  DECLARE_SCHEMA(Wedge_Entity)
};

class Static_Mesh_Entity : public Entity
{
public:
  SCHEMA_FIELD(render_component_t, render,
               Schema_Flags::Networked | Schema_Flags::Editable);

  DECLARE_SCHEMA(Static_Mesh_Entity)
};

// Schema name registrations (must be at namespace scope)
SCHEMA_NAME_FOR_TYPE(AABB_Entity)
SCHEMA_NAME_FOR_TYPE(Wedge_Entity)
SCHEMA_NAME_FOR_TYPE(Static_Mesh_Entity)

} // namespace network
