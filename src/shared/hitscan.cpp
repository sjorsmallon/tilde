#include "hitscan.hpp"

#include <cmath>

namespace shared
{

namespace
{

// Which face of the box the point sits on. The point comes off a ray-AABB
// intersection so it is on the surface by construction; the largest
// half-extent-normalized component names the face.
vec3f aabb_face_normal(const vec3f& point, const vec3f& box_min, const vec3f& box_max)
{
  const vec3f center       = (box_min + box_max) * 0.5f;
  const vec3f half_extents = (box_max - box_min) * 0.5f;
  const vec3f local        = point - center;

  const float x_ratio = std::abs(local.x) / half_extents.x;
  const float y_ratio = std::abs(local.y) / half_extents.y;
  const float z_ratio = std::abs(local.z) / half_extents.z;

  if (x_ratio >= y_ratio && x_ratio >= z_ratio)
    return {local.x < 0.f ? -1.f : 1.f, 0.f, 0.f};
  if (y_ratio >= z_ratio)
    return {0.f, local.y < 0.f ? -1.f : 1.f, 0.f};
  return {0.f, 0.f, local.z < 0.f ? -1.f : 1.f};
}

} // namespace

hitscan_result_t resolve_hitscan(vec3f origin, vec3f direction,
                                 float max_range,
                                 Span<const hitscan_target_t> targets)
{
  hitscan_result_t result{};

  // Seeding with max_range makes the range check part of the closest-wins
  // compare: anything further than the current best -- or than the wall the
  // caller already clamped to -- is skipped without a second test.
  float closest_distance = max_range;

  for (const hitscan_target_t& target : targets)
  {
    for (const player_hitbox_t& hitbox : player_hitboxes)
    {
      const vec3f center = target.position + hitbox.offset;

      float distance = 0.f;
      vec3f normal{};

      if (hitbox.shape == entities::Shape_Kind::Sphere)
      {
        if (!linalg::intersect_ray_sphere(origin, direction, center, hitbox.size.x,
                                          distance))
          continue;
        if (distance < 0.f || distance >= closest_distance)
          continue;

        normal = linalg::normalize((origin + direction * distance) - center);
      }
      else if (hitbox.shape == entities::Shape_Kind::Box)
      {
        const vec3f box_min = center - hitbox.size;
        const vec3f box_max = center + hitbox.size;

        if (!linalg::intersect_ray_aabb(origin, direction, box_min, box_max, distance))
          continue;
        // intersect_ray_aabb reports a hit whenever the ray EXITS in front of
        // the origin, so an origin inside the box yields a negative entry
        // distance. Rejecting it means a muzzle already overlapping a player
        // misses rather than landing an impact point behind the shooter.
        if (distance < 0.f || distance >= closest_distance)
          continue;

        normal = aabb_face_normal(origin + direction * distance, box_min, box_max);
      }

      closest_distance     = distance;
      result.hit_uid       = target.uid;
      result.region        = hitbox.region;
      result.impact_point  = origin + direction * distance;
      result.impact_normal = normal;
      result.distance      = distance;
    }
  }

  return result;
}

} // namespace shared
