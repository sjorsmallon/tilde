#include "hitscan.hpp"

namespace shared
{

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
    for (const assets::posed_hitbox_t& hitbox : target.hitboxes)
    {
      // Every shape's geometry lives in hitbox_rig.cpp, including the rule that
      // a ray starting inside a volume misses. This loop only ranks.
      const std::optional<assets::hitbox_ray_hit_t> hit =
          assets::intersect_ray_hitbox(hitbox, origin, direction);
      if (!hit || hit->distance >= closest_distance)
        continue;

      closest_distance     = hit->distance;
      result.hit_uid       = target.uid;
      result.region        = hitbox.region;
      result.impact_point  = origin + direction * hit->distance;
      result.impact_normal = hit->normal;
      result.distance      = hit->distance;
    }
  }

  return result;
}

} // namespace shared
