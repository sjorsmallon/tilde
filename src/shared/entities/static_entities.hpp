#pragma once

#include "../entity.hpp"
#include "../shapes.hpp"
#include <string>

namespace network
{

class AABB_Entity : public Entity
{
public:
  bool is_collision_geometry() const override { return true; }

  SCHEMA_FIELD(shared::box_volume_t, volume,
               Schema_Flags::Networked | Schema_Flags::Editable | Schema_Flags::Saveable);

  SCHEMA_FIELD(render_component_t, render,
               Schema_Flags::Networked | Schema_Flags::Editable);

  shared::box_volume_t *get_box_volume() override { return &volume; }
  const shared::box_volume_t *get_box_volume() const override { return &volume; }

  DECLARE_SCHEMA(AABB_Entity)
};

class Wedge_Entity : public Entity
{
public:
  bool is_collision_geometry() const override { return true; }

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
  bool is_collision_geometry() const override { return true; }

  SCHEMA_FIELD(render_component_t, render,
               Schema_Flags::Networked | Schema_Flags::Editable);

  DECLARE_SCHEMA(Static_Mesh_Entity)
};

// Schema name registrations (must be at namespace scope)
SCHEMA_NAME_FOR_TYPE(AABB_Entity)
SCHEMA_NAME_FOR_TYPE(Wedge_Entity)
SCHEMA_NAME_FOR_TYPE(Static_Mesh_Entity)

} // namespace network
