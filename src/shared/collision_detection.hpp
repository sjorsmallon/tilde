#pragma once
#include "aabb.hpp"
#include "network/network_types.hpp"
#include "plane.hpp"
#include <cmath>
#include <vector>

/*
  Architecture Note (Unified Collision):
  --------------------------------------
  We use a "Unified" Acceleration Structure (BVH) for both Dynamic Entities and
  Static Geometry.

  Why?
  1. Separate Structures (e.g. BSP for world + BVH for entities) require double
  traversal for every raycast. In modern scenes with lots of static props that
  aren't "world brushes", this becomes messy.
  2. "Entity Everything" (making every wall an Entity) is bad for performance:
     - 10k static walls = 10k allocations, vtables, generation IDs, network sync
  overhead.

  Solution:
  We use a `Collision_Id` that acts as a "Fat Pointer" or "Variant Index".
  - It can point to an Entity ID (Dynamic)
  - OR it can point to a Static Geometry Index (Static Array)

  The BVH doesn't care. It just stores AABBs and IDs. The game logic resloves
  the ID to the actual data.
*/
struct Collision_Id
{
  enum class Type : uint8_t
  {
    Entity = 0,
    Static_Geometry = 1
  };

  Type type;
  // The meaning of `index` is owned by whoever built the BVH:
  //   - session BVH (init_session_from_map): a static_entities array position
  //   - editor BVH (build_editor_bvh):       an entity uid (map.find_by_uid)
  // Each subsystem only ever queries its own BVH, so there is no ambiguity.
  uint32_t
      index; // Entity generation check happens externally if type == Entity
};

struct BVH_Node
{
  shared::aabb_bounds_t aabb;

  // Internal Node Data
  uint32_t left = 0;
  uint32_t right = 0;
  uint32_t parent = 0;

  // Leaf Node Data
  uint32_t first_entity_index = 0;
  uint32_t entity_count = 0;

  bool is_leaf() const { return left == 0 && right == 0; }

  static constexpr uint32_t MAX_ENTITIES_PER_LEAF = 8;
};

struct BVH_Primitive
{
  Collision_Id id;
  shared::aabb_bounds_t aabb;
  std::vector<Plane> collision_planes; // convex hull faces, normals pointing outward
  std::vector<std::vector<vec3f>> face_polygons; // face_polygons[i] ↔ collision_planes[i]
};

using BVH_Input = BVH_Primitive;

struct Bounding_Volume_Hierarchy
{
  uint32_t root_node_idx = 0;
  std::vector<BVH_Node> nodes;
  std::vector<BVH_Primitive> primitives;
};

void bvh_add_entry(Bounding_Volume_Hierarchy &bvh, Collision_Id id,
                   const shared::aabb_bounds_t &aabb,
                   std::vector<Plane> collision_planes = {});

Bounding_Volume_Hierarchy build_bvh(const std::vector<BVH_Input> &inputs);

struct ray_hit_result_t
{
  bool hit;
  float t;
  Collision_Id id;
};

bool bvh_intersect_ray(const Bounding_Volume_Hierarchy &bvh,
                       const vec3f &origin, const vec3f &dir, ray_hit_result_t &out_hit);

void bvh_intersect_aabb(const Bounding_Volume_Hierarchy &bvh, const shared::aabb_bounds_t &aabb,
                        std::vector<const BVH_Primitive *> &out_primitives);

// Möller–Trumbore ray-triangle intersection. Returns true and sets out_t to the
// hit distance when the ray crosses the triangle in front of the origin.
inline bool ray_triangle(const vec3f &origin, const vec3f &dir, const vec3f &v0,
                         const vec3f &v1, const vec3f &v2, float &out_t)
{
  constexpr float epsilon = 1e-6f;
  vec3f edge1 = v1 - v0;
  vec3f edge2 = v2 - v0;
  vec3f h = linalg::cross(dir, edge2);
  float a = linalg::dot(edge1, h);
  if (std::abs(a) < epsilon)
    return false; // ray parallel to triangle
  float f = 1.0f / a;
  vec3f s = origin - v0;
  float u = f * linalg::dot(s, h);
  if (u < 0.0f || u > 1.0f)
    return false;
  vec3f q = linalg::cross(s, edge1);
  float v = f * linalg::dot(dir, q);
  if (v < 0.0f || u + v > 1.0f)
    return false;
  out_t = f * linalg::dot(edge2, q);
  return out_t > epsilon;
}