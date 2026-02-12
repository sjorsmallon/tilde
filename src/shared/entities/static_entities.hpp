#pragma once

#include "../entity.hpp"
#include <string>

namespace network
{

class AABB_Entity : public Entity
{
public:
  SCHEMA_FIELD(vec3f, center, Schema_Flags::Networked | Schema_Flags::Editable);
  SCHEMA_FIELD(vec3f, half_extents,
               Schema_Flags::Networked | Schema_Flags::Editable);

  SCHEMA_FIELD(render_component_t, render,
               Schema_Flags::Networked | Schema_Flags::Editable);

  DECLARE_SCHEMA(AABB_Entity)
};

class Wedge_Entity : public Entity
{
public:
  SCHEMA_FIELD(vec3f, center, Schema_Flags::Networked | Schema_Flags::Editable);
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
  SCHEMA_FIELD_DEFAULT(vec3f, scale, Schema_Flags::Networked | Schema_Flags::Editable, (vec3f{0.1f, 0.1f, 0.1f}));
  SCHEMA_FIELD(int32, asset_id,
               Schema_Flags::Networked | Schema_Flags::Editable);

  SCHEMA_FIELD(render_component_t, render,
               Schema_Flags::Networked | Schema_Flags::Editable);

  DECLARE_SCHEMA(Static_Mesh_Entity)
};

} // namespace network
