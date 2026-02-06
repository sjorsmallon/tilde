#include "collision_detection.hpp"
#include <algorithm>
#include <cfloat>

using namespace linalg;

// Helper to compute the union of two AABBs
static AABB union_aabb(const AABB &a, const AABB &b)
{
  return {{std::min(a.min.x, b.min.x), std::min(a.min.y, b.min.y),
           std::min(a.min.z, b.min.z)},
          {std::max(a.max.x, b.max.x), std::max(a.max.y, b.max.y),
           std::max(a.max.z, b.max.z)}};
}

static vec3f get_aabb_center(const AABB &aabb)
{
  return (aabb.min + aabb.max) * 0.5f;
}

static void expand_aabb(AABB &aabb, const vec3f &p)
{
  aabb.min.x = std::min(aabb.min.x, p.x);
  aabb.min.y = std::min(aabb.min.y, p.y);
  aabb.min.z = std::min(aabb.min.z, p.z);

  aabb.max.x = std::max(aabb.max.x, p.x);
  aabb.max.y = std::max(aabb.max.y, p.y);
  aabb.max.z = std::max(aabb.max.z, p.z);
}

/*
  BVH Construction Algorithm (Midpoint Split):

  This builder uses the "Midpoint Split" heuristic, which is fast (O(N)) and
  effective for real-time applications. It works as follows:

  1. Calculate Node AABB: Compute the union of all primitive AABBs in the
  current range. This becomes the bounding box for the node.

  2. Leaf Check: If the number of primitives is small (<= 8), stop and create a
  Leaf Node.

  3. Split Heuristic:
     - Calculate the "Centroid AABB": the bounding box of the *centers* of all
  primitives.
     - Pick the longest axis of this Centroid AABB (X, Y, or Z).
     - Split the primitives into two groups based on their center position
  relative to the midpoint of the Centroid AABB along that axis.

  4. Recursion: Recursively build the left and right children.
*/
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

  // Recursive builder
  // Returns the index of the created node in bvh.nodes
  // range_start and range_end are indices into active_indices
  std::function<uint32_t(uint32_t, uint32_t)> build_recursive =
      [&](uint32_t range_start, uint32_t range_end) -> uint32_t
  {
    uint32_t count = range_end - range_start;
    uint32_t node_idx = static_cast<uint32_t>(bvh.nodes.size());
    bvh.nodes.emplace_back();

    // 1. Compute AABB for this node
    // Also compute centroids AABB for splitting
    AABB node_aabb = inputs[active_indices[range_start]].aabb;
    vec3f first_center = get_aabb_center(node_aabb);
    AABB centroid_aabb = {first_center, first_center};

    for (uint32_t i = range_start + 1; i < range_end; ++i)
    {
      const auto &input = inputs[active_indices[i]];
      node_aabb = union_aabb(node_aabb, input.aabb);

      vec3f center = get_aabb_center(input.aabb);
      expand_aabb(centroid_aabb, center);
    }

    // Reference to node (warning: reallocations of bvh.nodes invalidate this!)
    // So we just use indices and modify later.
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

    float split_pos =
        (centroid_aabb.min[axis] + centroid_aabb.max[axis]) * 0.5f;

    // Partition
    auto it = std::partition(
        active_indices.begin() + range_start,
        active_indices.begin() + range_end, [&](uint32_t idx)
        { return get_aabb_center(inputs[idx].aabb)[axis] < split_pos; });

    uint32_t mid =
        static_cast<uint32_t>(std::distance(active_indices.begin(), it));

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

bool bvh_intersect_ray(const Bounding_Volume_Hierarchy &bvh,
                       const vec3f &origin, const vec3f &dir, Ray_Hit &out_hit)
{
  if (bvh.nodes.empty())
    return false;

  out_hit.hit = false;
  out_hit.t = FLT_MAX;

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

        float t_prim;
        if (intersect_ray_aabb(origin, dir, prim.aabb.min, prim.aabb.max,
                               t_prim))
        {
          // We only care about forward hits
          if (t_prim < 0.0f)
            t_prim =
                0.0f; // Treat start-inside as 0 distance hit? Or keep t_prim?

          // If we are strictly checking "ray starts at origin", t must be >= 0
          // (or close to 0) But if t_prim is negative, it means we are inside.
          // Usually for "picking", we want the first positive t, OR if we are
          // inside, we hit it immediately. Let's assume t_prim >= 0 check, or
          // logic: If t_prim < 0, but max > 0, we hit. intersect_ray_aabb
          // returns t_min. If start inside, t_min < 0.

          if (t_prim < out_hit.t)
          {
            // If t_prim is negative, we might want to discard if we strictly
            // want points in front. But usually you want "what am I pointing
            // at". If I am inside a box, I am 'hitting' it. Let's clamp to 0 if
            // negative for comparison, or just accept it? If I use t_prim < 0,
            // then t_prim < out_hit.t (which initializes to MAX) is true.

            // Simplest: accept it.
            out_hit.hit = true;
            out_hit.t = t_prim;
            out_hit.id = prim.id;
            hit_anything = true;
          }
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

void bvh_intersect_aabb(const Bounding_Volume_Hierarchy &bvh, const AABB &aabb,
                        std::vector<Collision_Id> &out_ids)
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
          out_ids.push_back(prim.id);
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
                   const AABB &aabb)
{
  std::vector<BVH_Input> inputs;
  inputs.reserve(bvh.primitives.size() + 1);

  // 1. Gather existing entries from primitives
  for (const auto &prim : bvh.primitives)
  {
    inputs.push_back({prim.id, prim.aabb});
  }

  // 2. Add new entry
  inputs.push_back({id, aabb});

  // 3. Rebuild
  bvh = build_bvh(inputs);
}
