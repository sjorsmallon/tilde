#pragma once
// TODO: Implement AABB-BVH Intersection
#include "bsp.hpp"
#include "entity.hpp"
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
  uint32_t
      index; // Entity generation check happens externally if type == Entity
};

struct BVH_Node
{
  AABB aabb;

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
  AABB aabb;
};

// Input is same as Primitive for now
using BVH_Input = BVH_Primitive;

struct Bounding_Volume_Hierarchy
{
  uint32_t root_node_idx = 0;
  std::vector<BVH_Node> nodes;
  std::vector<BVH_Primitive> primitives;
};

Bounding_Volume_Hierarchy build_bvh(const std::vector<BVH_Input> &inputs);

struct Ray_Hit
{
  bool hit;
  float t;
  Collision_Id id;
};

bool bvh_intersect_ray(const Bounding_Volume_Hierarchy &bvh,
                       const vec3f &origin, const vec3f &dir, Ray_Hit &out_hit);

void bvh_intersect_aabb(const Bounding_Volume_Hierarchy &bvh, const AABB &aabb,
                        std::vector<Collision_Id> &out_ids);