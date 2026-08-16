#include "hitscan.hpp"

#include <algorithm>
#include <cmath>

namespace shared
{

bounding_sphere_t compute_bounding_sphere(Span<const assets::posed_hitbox_t> hitboxes)
{
  // No volumes is a target that cannot be hit, and a zero-radius sphere at the
  // origin says exactly that. This is the ONE case where a finite default is
  // right, because it is the truth rather than a missing value.
  if (hitboxes.empty())
    return {{0.f, 0.f, 0.f}, 0.f};

  vec3f minimum = hitboxes[0].center();
  vec3f maximum = minimum;
  for (const assets::posed_hitbox_t& hitbox : hitboxes)
  {
    const vec3f center = hitbox.center();
    minimum = {std::min(minimum.x, center.x), std::min(minimum.y, center.y),
               std::min(minimum.z, center.z)};
    maximum = {std::max(maximum.x, center.x), std::max(maximum.y, center.y),
               std::max(maximum.z, center.z)};
  }

  bounding_sphere_t sphere{};
  sphere.center = (minimum + maximum) * 0.5f;
  sphere.radius = 0.f;

  for (const assets::posed_hitbox_t& hitbox : hitboxes)
  {
    // Every shape fits within half its span plus whichever thickness it uses --
    // `radius` for the round three, the half-extent diagonal for a Box. Taking
    // the max of the two covers all four without switching on the shape, and
    // over-estimating a Box (which is centred on its span rather than extended
    // by it) only ever makes the reject weaker, never wrong.
    const float half_span  = linalg::length(hitbox.end - hitbox.start) * 0.5f;
    const float thickness  = std::max(hitbox.radius, linalg::length(hitbox.half_extents));
    const float reach      = linalg::length(hitbox.center() - sphere.center);
    sphere.radius          = std::max(sphere.radius, reach + half_span + thickness);
  }

  return sphere;
}

namespace
{

// Does the segment [origin, origin + direction*max_distance] come within the
// target's bound? Conservative: every volume sits inside that sphere, so a
// segment that misses the sphere cannot touch a volume.
//
// THIS IS THE BROAD-PHASE SEAM. It is where a cheaper per-target reject belongs
// -- today geometry, later a PVS/visibility test once targets carry a cluster id
// and the shooter's own is available here. Note that anything added must stay
// CONSERVATIVE in the same direction: a filter that can reject a reachable
// target turns into missing hits with nothing logged, which is the one failure
// this whole file is arranged to avoid.
bool segment_reaches_bounds(vec3f origin, vec3f direction, float max_distance,
                            const bounding_sphere_t& bounds)
{
  if (std::isinf(bounds.radius))
    return true; // no bound computed; test the volumes

  // Clamping the projection to [0, max_distance] is what makes this a SEGMENT
  // test rather than an infinite-ray one: a target behind the shooter measures
  // from the muzzle, one past the range measures from the range's end, and a
  // shooter standing inside the sphere lands at 0 and is never rejected.
  const vec3f to_center = bounds.center - origin;
  const float along =
      std::clamp(linalg::dot(to_center, direction), 0.f, max_distance);
  const vec3f offset = to_center - direction * along;

  return linalg::dot(offset, offset) <= bounds.radius * bounds.radius;
}

} // namespace

hitscan_result_t resolve_hitscan(vec3f origin, vec3f direction,
                                 float max_range,
                                 Span<const hitscan_target_t> targets,
                                 entity_uid_t ignore_uid)
{
  hitscan_result_t result{};

  // Seeding with max_range makes the range check part of the closest-wins
  // compare: anything further than the current best -- or than the wall the
  // caller already clamped to -- is skipped without a second test.
  float closest_distance = max_range;

  for (const hitscan_target_t& target : targets)
  {
    // The shooter is in the shared list like everyone else; skipping it here is
    // what lets that list be built once per tick rather than once per shot.
    if (target.uid == ignore_uid)
      continue;

    // Measured against closest_distance rather than max_range on purpose: once
    // something has been hit, a target whose bound is only reachable further out
    // cannot beat it, so the reject strengthens as the loop narrows.
    if (!segment_reaches_bounds(origin, direction, closest_distance, target.bounds))
      continue;


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
