#include "collision_detection.hpp"
#include <algorithm>
#include <cfloat>
#include <functional>

using namespace linalg;
using namespace shared;

Bounding_Volume_Hierarchy build_bvh(const std::vector<BVH_Input> &inputs)
{
  Bounding_Volume_Hierarchy bvh;
  if (inputs.empty())
  {
    return bvh;
  }

  // Work with indices to avoid copying inputs around repeatedly
  std::vector<uint32_t> active_indices(inputs.size());
  for (size_t i = 0; i < inputs.size(); ++i)
  {
    active_indices[i] = static_cast<uint32_t>(i);
  }


  //@NOTE(SJM): this is a std::function because
  // it calls itself and otherwise, auto deduction fails
  // because auto definitions cannot contain themselves.
  std::function<uint32_t(uint32_t, uint32_t)> build_recursive =
      [&](uint32_t range_start, uint32_t range_end) -> uint32_t
  {
    uint32_t count = range_end - range_start;
    uint32_t node_idx = static_cast<uint32_t>(bvh.nodes.size());
    bvh.nodes.emplace_back();

    // 1. Compute AABB for this node
    // Also compute centroids AABB for splitting
    aabb_bounds_t node_aabb = inputs[active_indices[range_start]].aabb;
    vec3f first_center = get_aabb_center(node_aabb);
    aabb_bounds_t centroid_aabb = {first_center, first_center};

    for (uint32_t i = range_start + 1; i < range_end; ++i)
    {
      const auto &input = inputs[active_indices[i]];
      node_aabb = union_aabb(node_aabb, input.aabb);

      vec3f center = get_aabb_center(input.aabb);
      expand_aabb_to_include_point(centroid_aabb, center);
    }

    bvh.nodes[node_idx].aabb = node_aabb;

    // 2. Check for leaf condition
    if (count <= BVH_Node::MAX_ENTITIES_PER_LEAF)
    {
      // Create Leaf
      bvh.nodes[node_idx].first_entity_index =
          static_cast<uint32_t>(bvh.primitives.size());
      bvh.nodes[node_idx].entity_count = count;
      bvh.nodes[node_idx].left = 0;
      bvh.nodes[node_idx].right = 0;

      for (uint32_t i = range_start; i < range_end; ++i)
      {
        bvh.primitives.push_back(inputs[active_indices[i]]);
      }

      return node_idx;
    }

    // 3. Split
    // Find longest axis of centroid AABB
    vec3f extent = centroid_aabb.max - centroid_aabb.min;
    int axis = 0;
    if (extent.y > extent.x)
      axis = 1;
    if (extent.z > extent[axis])
      axis = 2;

    float split_position =
        (centroid_aabb.min[axis] + centroid_aabb.max[axis]) * 0.5f;

    // Partition
    auto split_pointer = std::partition(
        active_indices.begin() + range_start,
        active_indices.begin() + range_end, [&](uint32_t idx)
        { return get_aabb_center(inputs[idx].aabb)[axis] < split_position; });

    uint32_t mid =
        static_cast<uint32_t>(std::distance(active_indices.begin(), split_pointer));

    // If split failed, simply split in half
    if (mid == range_start || mid == range_end)
    {
      mid = range_start + (count / 2);
    }

    uint32_t left_child = build_recursive(range_start, mid);
    uint32_t right_child = build_recursive(mid, range_end);

    // Re-access node
    bvh.nodes[node_idx].left = left_child;
    bvh.nodes[node_idx].right = right_child;
    bvh.nodes[node_idx].parent = 0;

    bvh.nodes[left_child].parent = node_idx;
    bvh.nodes[right_child].parent = node_idx;

    return node_idx;
  };

  bvh.nodes.reserve(inputs.size() * 2);
  bvh.primitives.reserve(inputs.size());

  bvh.root_node_idx = build_recursive(0, static_cast<uint32_t>(inputs.size()));

  return bvh;
}

bool intersect_ray_convex_hull(Span<const Plane> planes, const vec3f& origin,
                               const vec3f& dir, float &out_t, float &out_t_exit,
                               vec3f& out_normal)
{
  if (planes.count == 0)
    return false;

  // Parallel means the ray never crosses that face's plane, so the face can
  // only reject (origin outside it) and never bound the interval.
  constexpr float parallel_epsilon = 1e-8f;

  float t_enter = -FLT_MAX;
  float t_exit  = FLT_MAX;
  vec3f enter_normal{0.f, 0.f, 0.f};

  for (const Plane &plane : planes)
  {
    const float denominator   = linalg::dot(dir, plane.normal);
    const float signed_distance = linalg::dot(origin - plane.point, plane.normal);

    if (std::abs(denominator) < parallel_epsilon)
    {
      if (signed_distance > 0.f)
        return false; // outside a face the ray runs alongside
      continue;
    }

    const float t = -signed_distance / denominator;

    if (denominator < 0.f)
    {
      // Ray runs against the outward normal: this face is entered.
      if (t > t_enter)
      {
        t_enter      = t;
        enter_normal = plane.normal;
      }
    }
    else if (t < t_exit)
    {
      t_exit = t;
    }

    if (t_enter > t_exit)
      return false;
  }

  if (t_exit < 0.f)
    return false; // hull is entirely behind the origin

  // A closed hull always has a face opposing the ray, so an unset entry here
  // means the plane set was not one.
  if (t_enter == -FLT_MAX)
    return false;

  out_t      = t_enter;
  out_t_exit = t_exit;
  out_normal = enter_normal;
  return true;
}

bool bvh_intersect_ray(const Bounding_Volume_Hierarchy &bvh,
                       const vec3f& origin, const vec3f& dir, ray_hit_result_t &out_hit)
{
  if (bvh.nodes.empty())
    return false;

  out_hit.hit    = false;
  out_hit.t      = FLT_MAX;
  out_hit.t_exit = FLT_MAX;
  out_hit.normal = {0.f, 0.f, 0.f};

  // Use a simple stack for traversal to avoid deep recursion overhead
  // Stack stores node indices
  // Estimate capacity: 64 should be plenty for balanced tree of depth 64 (2^64
  // entities!)
  std::vector<uint32_t> node_stack;
  node_stack.clear();
  node_stack.reserve(64);

  node_stack.push_back(bvh.root_node_idx);

  bool hit_anything = false;

  while (!node_stack.empty())
  {
    uint32_t node_idx = node_stack.back();
    node_stack.pop_back();

    const BVH_Node &node = bvh.nodes[node_idx];

    float t_node_hit;
    if (!intersect_ray_aabb(origin, dir, node.aabb.min, node.aabb.max,
                            t_node_hit))
    {
      continue;
    }

    // Optimization: if the closest hit so far is closer than this node, skip
    // NOTE: This assumes t_node_hit is the entry point.
    // intersect_ray_aabb returns the entry point even if negative (start
    // inside). If we start inside, t_node_hit < 0. We should still check
    // children. If t_node_hit > out_hit.t, then the box is further than our
    // closest hit.
    if (t_node_hit > out_hit.t)
      continue;

    if (node.is_leaf())
    {
      // Check primitives in leaf
      for (uint32_t i = 0; i < node.entity_count; ++i)
      {
        const BVH_Primitive &prim = bvh.primitives[node.first_entity_index + i];

        // The AABB is the broad phase here exactly as it is in
        // resolve_collisions: it rejects cheaply, and a primitive that carries
        // its hull is then clipped against it for the real hit. Reporting the
        // bound as the answer is what made a brush navmesh as its box -- the
        // extruded shape was in the BVH the whole time, just never consulted.
        float t_prim;
        float t_exit_prim;
        vec3f normal_prim;
        if (!intersect_ray_aabb(origin, dir, prim.aabb.min, prim.aabb.max, t_prim,
                                t_exit_prim, normal_prim))
          continue;

        if (t_prim > out_hit.t)
          continue; // the bound alone is already further than the closest hit

        // No planes means the caller built this BVH to pick by bound (the
        // editor's does), so the AABB hit stands.
        if (!prim.collision_planes.empty() &&
            !intersect_ray_convex_hull(prim.collision_planes, origin, dir, t_prim,
                                       t_exit_prim, normal_prim))
          continue;

        // A negative t is an origin inside the solid, which counts as a hit at
        // zero distance -- that is what picking from inside a box means.
        if (t_prim < 0.0f)
          t_prim = 0.0f;

        if (t_prim < out_hit.t)
        {
          out_hit.hit    = true;
          out_hit.t      = t_prim;
          out_hit.t_exit = t_exit_prim;
          out_hit.id     = prim.id;
          out_hit.normal = normal_prim;
          hit_anything   = true;
        }
      }
    }
    else
    {
      // Internal Node: Push children
      // Optimization: Sort children by distance?
      // For simplest solution: just push both.
      if (node.right)
        node_stack.push_back(node.right);
      if (node.left)
        node_stack.push_back(node.left);
    }
  }

  return hit_anything;
}

void bvh_intersect_aabb(const Bounding_Volume_Hierarchy &bvh, const aabb_bounds_t &aabb,
                        std::vector<const BVH_Primitive *> &out_primitives)
{
  if (bvh.nodes.empty())
    return;

  // Use a simple stack for traversal
  std::vector<uint32_t> node_stack;
  node_stack.reserve(64);
  node_stack.push_back(bvh.root_node_idx);

  while (!node_stack.empty())
  {
    uint32_t node_idx = node_stack.back();
    node_stack.pop_back();

    const BVH_Node &node = bvh.nodes[node_idx];

    // Check overlap with Node AABB
    if (!intersect_aabb_aabb(node.aabb.min, node.aabb.max, aabb.min, aabb.max))
    {
      continue;
    }

    if (node.is_leaf())
    {
      // Check primitives in leaf
      for (uint32_t i = 0; i < node.entity_count; ++i)
      {
        const BVH_Primitive &prim = bvh.primitives[node.first_entity_index + i];

        // Check precise primitive AABB overlap
        if (intersect_aabb_aabb(prim.aabb.min, prim.aabb.max, aabb.min,
                                aabb.max))
        {
          out_primitives.push_back(&prim);
        }
      }
    }
    else
    {
      // Internal Node: Push children
      if (node.right)
        node_stack.push_back(node.right);
      if (node.left)
        node_stack.push_back(node.left);
    }
  }
}

void bvh_add_entry(Bounding_Volume_Hierarchy &bvh, Collision_Id id,
                   const aabb_bounds_t &aabb,
                   std::vector<Plane> collision_planes)
{
  std::vector<BVH_Input> inputs;
  inputs.reserve(bvh.primitives.size() + 1);

  // 1. Gather existing entries from primitives
  for (auto &prim : bvh.primitives)
  {
    inputs.push_back(std::move(prim));
  }

  // 2. Add new entry
  inputs.push_back({id, aabb, std::move(collision_planes)});

  // 3. Rebuild
  bvh = build_bvh(inputs);
}
