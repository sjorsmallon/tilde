#include "hull_sweep.hpp"
#include "movement_kernel.hpp"

#include <algorithm>
#include <vector>

namespace shared
{

namespace
{
// A move that brings the hull closer to a face by less than this is running alongside it, not
// entering: the clipped travel along a face is parallel to it within float noise.
constexpr float MINIMUM_APPROACH = 1e-4f;
} // namespace

std::optional<hull_sweep_hit_t> sweep_hull(const Bounding_Volume_Hierarchy& bvh,
                                           const predicted_world_t& world, const vec3& from,
                                           const vec3& to, float half_width, float half_height)
{
  const vec3 move = to - from;
  if (dot(move, move) == 0.f)
    return std::nullopt;

  const aabb_bounds_t traversal =
      union_aabb(hull_aabb(from, half_width, half_height), hull_aabb(to, half_width, half_height));

  std::vector<collision_candidate_t> candidates;
  collect_collision_candidates(bvh, world, traversal, candidates);

  std::optional<hull_sweep_hit_t> nearest;
  for (const collision_candidate_t& candidate : candidates)
  {
    float t_enter;
    float t_exit;
    vec3  normal;
    if (!intersect_box_sweep_convex_hull(*candidate.collision_planes, from, move, half_width,
                                         half_height, t_enter, t_exit, normal))
      continue;

    const float approach = -dot(move, normal);
    if (t_enter > 1.f || approach < MINIMUM_APPROACH)
      continue;

    const float t = std::clamp(t_enter - hull_sweep_skin / approach, 0.f, 1.f);
    if (nearest && t >= nearest->t)
      continue;

    nearest = hull_sweep_hit_t{
        .t = t, .normal = normal, .id = candidate.id, .mover_uid = candidate.mover_uid};
  }

  return nearest;
}

} // namespace shared
