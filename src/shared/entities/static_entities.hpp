#pragma once

#include "entity.hpp"
#include <string>

namespace network
{

class AABB_Entity : public Entity
{
public:
  Network_Var<vec3f> center;
  Network_Var<vec3f> half_extents;

  DECLARE_SCHEMA(AABB_Entity)
};

class Wedge_Entity : public Entity
{
public:
  Network_Var<vec3f> center;
  Network_Var<vec3f> half_extents;
  Network_Var<int32> orientation;

  DECLARE_SCHEMA(Wedge_Entity)
};

class Static_Mesh_Entity : public Entity
{
public:
  Network_Var<vec3f> position;
  Network_Var<vec3f> rotation; // Euler angles
  Network_Var<vec3f> scale;

  // For this task, I will add `asset_id` (hash) which is an int or uint64.
  // That is trivial.
  Network_Var<int32> asset_id;

  DECLARE_SCHEMA(Static_Mesh_Entity)
};

} // namespace network
