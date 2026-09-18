#include "collision_detection.hpp"
#include <algorithm>
#include <cfloat>
#include <functional>

using namespace linalg;
using namespace shared;

// this is not that complicated, but I had to relearn it. sigh.
// for all entities, you normally want to do like "binary search" in space.
// e.g. for a raycast, you don't linearly want to visit all entities. or for collision.
// you want to know who's near you. 
// so we build a BVH. what we do here is kind of trivial. we group all entities and then
// want to split them in subgroups. we take the union of all aabbs (read: what are the min(xyz), max(xyz) of all these entities?)
// and then HEURISTIC: split the longest axis.
// there is no verification if this split is balanced, if this is correct, or whatever.
// if the split fails, you just equally divide the entities in indices left and right.
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

    // pick the first one in the active indices to be "node_aabb".
    // same for first_center.
    // centroid_aabb is just initialized from that.
    aabb_bounds_t node_aabb = inputs[active_indices[range_start]].aabb;
    vec3f first_center = get_aabb_center(node_aabb);
    aabb_bounds_t centroid_aabb = {first_center, first_center};

    // for all the aabbs in this thing, union them so you get the maximum aabb?
    // also for centroids (which is the thing we use for splitting because they are points and that's simple.)
    for (uint32_t i = range_start + 1; i < range_end; ++i)
    {
      const auto &input = inputs[active_indices[i]];
      node_aabb = union_aabb(node_aabb, input.aabb);

      vec3f center = get_aabb_center(input.aabb);
      expand_aabb_to_include_point(centroid_aabb, center);
    }

    bvh.nodes[node_idx].aabb = node_aabb;

    // leaf condition: not really worth it to split, just dump everything in this bucket and linear search over it.
    // there's probably some optimal max entities per leaf (or just none) but this is what we use.
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

    // find the axis that's the longest. that's the one we will be splitting on (I guess that's a worthwile heuristic?)
    vec3f extent = centroid_aabb.max - centroid_aabb.min;
    int axis = 0;
    if (extent.y > extent.x)
      axis = 1;
    if (extent.z > extent[axis])
      axis = 2;

    // pick the middle of the longest axis to split.
    float split_position =
        (centroid_aabb.min[axis] + centroid_aabb.max[axis]) * 0.5f;

    // create two partitions: one < split_position, one > split_position.
    auto split_pointer = std::partition(
        active_indices.begin() + range_start,
        active_indices.begin() + range_end, [&](uint32_t idx)
        { return get_aabb_center(inputs[idx].aabb)[axis] < split_position; });

    uint32_t mid =
        static_cast<uint32_t>(std::distance(active_indices.begin(), split_pointer));

    // if the split failed for some fucked up reason:
    // (maybe all the entities are in the same place or whatever):
    // there's nothing clever to be done so just split down the middle.
    if (mid == range_start || mid == range_end)
    {
      mid = range_start + (count / 2);
    }

    // recursively invoke this function to build the left and right child.
    uint32_t left_child = build_recursive(range_start, mid);
    uint32_t right_child = build_recursive(mid, range_end);

    // set up the nodes correctly with the retrieved indices.
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

namespace
{

// The broad phase is the AABB, as in resolve_collisions; a primitive carrying
// its hull is then clipped against it for the real hit. No planes means the
// BVH was built to pick by bound (the editor's), so the AABB hit stands. A
// negative t is an origin inside the solid: a hit at zero distance.
bool intersect_ray_primitive(const BVH_Primitive &prim, const vec3f &origin, const vec3f &dir,
                             float nearest_so_far, ray_hit_result_t &out_hit)
{
  float t_prim;
  float t_exit_prim;
  vec3f normal_prim;
  if (!intersect_ray_aabb(origin, dir, prim.aabb.min, prim.aabb.max, t_prim, t_exit_prim,
                          normal_prim))
    return false;
  if (t_prim > nearest_so_far)
    return false;
  if (!prim.collision_planes.empty() &&
      !intersect_ray_convex_hull(prim.collision_planes, origin, dir, t_prim, t_exit_prim,
                                 normal_prim))
    return false;

  out_hit.hit    = true;
  out_hit.t      = std::max(t_prim, 0.0f);
  out_hit.t_exit = t_exit_prim;
  out_hit.id     = prim.id;
  out_hit.normal = normal_prim;
  return true;
}

// Every leaf primitive the ray reaches whose node entry is not past `cutoff()`.
template <typename Cutoff_T, typename Visit_T>
void walk_ray(const Bounding_Volume_Hierarchy &bvh, const vec3f &origin, const vec3f &dir,
              Span<const uint8_t> disabled_geometry, Cutoff_T cutoff, Visit_T visit)
{
  std::vector<uint32_t> node_stack;
  node_stack.reserve(64);
  node_stack.push_back(bvh.root_node_idx);

  while (!node_stack.empty())
  {
    const BVH_Node &node = bvh.nodes[node_stack.back()];
    node_stack.pop_back();

    float t_node_hit;
    if (!intersect_ray_aabb(origin, dir, node.aabb.min, node.aabb.max, t_node_hit))
      continue;
    if (t_node_hit > cutoff())
      continue;

    if (!node.is_leaf())
    {
      if (node.right)
        node_stack.push_back(node.right);
      if (node.left)
        node_stack.push_back(node.left);
      continue;
    }

    for (uint32_t i = 0; i < node.entity_count; ++i)
    {
      const BVH_Primitive &prim = bvh.primitives[node.first_entity_index + i];
      if (!collision_is_disabled(disabled_geometry, prim.id))
        visit(prim);
    }
  }
}

} // namespace

bool bvh_intersect_ray(const Bounding_Volume_Hierarchy &bvh,
                       const vec3f& origin, const vec3f& dir, ray_hit_result_t &out_hit,
                       Span<const uint8_t> disabled_geometry)
{
  if (bvh.nodes.empty())
    return false;

  out_hit.hit    = false;
  out_hit.t      = FLT_MAX;
  out_hit.t_exit = FLT_MAX;
  out_hit.normal = {0.f, 0.f, 0.f};

  walk_ray(bvh, origin, dir, disabled_geometry, [&] { return out_hit.t; },
           [&](const BVH_Primitive &prim)
           {
             ray_hit_result_t candidate;
             if (intersect_ray_primitive(prim, origin, dir, out_hit.t, candidate) &&
                 candidate.t < out_hit.t)
               out_hit = candidate;
           });
  return out_hit.hit;
}

void bvh_intersect_ray_all(const Bounding_Volume_Hierarchy &bvh, const vec3f& origin,
                           const vec3f& dir, std::vector<ray_hit_result_t> &out_hits,
                           Span<const uint8_t> disabled_geometry)
{
  out_hits.clear();
  if (bvh.nodes.empty())
    return;

  walk_ray(bvh, origin, dir, disabled_geometry, [] { return FLT_MAX; },
           [&](const BVH_Primitive &prim)
           {
             ray_hit_result_t candidate;
             if (!intersect_ray_primitive(prim, origin, dir, FLT_MAX, candidate))
               return;
             for (ray_hit_result_t &existing : out_hits)
             {
               if (existing.id.type == candidate.id.type && existing.id.index == candidate.id.index)
               {
                 if (candidate.t < existing.t)
                   existing = candidate;
                 return;
               }
             }
             out_hits.push_back(candidate);
           });

  std::sort(out_hits.begin(), out_hits.end(),
            [](const ray_hit_result_t &a, const ray_hit_result_t &b) { return a.t < b.t; });
}

void bvh_intersect_aabb(const Bounding_Volume_Hierarchy &bvh, const aabb_bounds_t &aabb,
                        std::vector<const BVH_Primitive *> &out_primitives,
                        Span<const uint8_t> disabled_geometry)
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

        if (collision_is_disabled(disabled_geometry, prim.id))
          continue;

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

bool bvh_point_is_inside_solid(const Bounding_Volume_Hierarchy &bvh, const vec3f& point,
                              Span<const uint8_t> disabled_geometry)
{
  constexpr float ON_FACE_TOLERANCE = 1e-3f;

  std::vector<const BVH_Primitive *> candidates;
  bvh_intersect_aabb(bvh, {point, point}, candidates, disabled_geometry);

  for (const BVH_Primitive *primitive : candidates)
  {
    if (primitive->collision_planes.empty())
      return true;

    bool inside_every_plane = true;
    for (const Plane &plane : primitive->collision_planes)
    {
      if (dot(plane.normal, point - plane.point) > ON_FACE_TOLERANCE)
      {
        inside_every_plane = false;
        break;
      }
    }
    if (inside_every_plane) return true;
  }

  return false;
}

