#include "components.hpp"
#include "../linalg.hpp"
#include <algorithm>

namespace shared
{

// --- Hitbox collision implementations ---

bool test_sphere_sphere(const vec3f& sphere_a_pos, float radius_a,
                        const vec3f& sphere_b_pos, float radius_b)
{
  vec3f delta = sphere_b_pos - sphere_a_pos;
  float dist_squared = linalg::dot(delta, delta);
  float radius_sum = radius_a + radius_b;
  return dist_squared <= (radius_sum * radius_sum);
}

bool test_sphere_capsule(const vec3f& sphere_pos, float sphere_radius,
                         const vec3f& capsule_p0, const vec3f& capsule_p1,
                         float capsule_radius)
{
  // Find closest point on line segment (capsule axis) to sphere center
  vec3f segment = capsule_p1 - capsule_p0;
  vec3f to_sphere = sphere_pos - capsule_p0;

  float segment_length_sq = linalg::dot(segment, segment);

  // Handle degenerate case (capsule is a sphere)
  if (segment_length_sq < 0.0001f)
  {
    return test_sphere_sphere(sphere_pos, sphere_radius, capsule_p0, capsule_radius);
  }

  // Project sphere center onto line segment
  float t = linalg::dot(to_sphere, segment) / segment_length_sq;
  t = std::clamp(t, 0.0f, 1.0f);

  vec3f closest_point = capsule_p0 + segment * t;

  // Test sphere vs sphere at closest point
  return test_sphere_sphere(sphere_pos, sphere_radius, closest_point, capsule_radius);
}

bool test_sphere_aabb(const vec3f& sphere_pos, float sphere_radius,
                      const vec3f& aabb_min, const vec3f& aabb_max)
{
  // Find closest point on AABB to sphere center
  vec3f closest = {
    std::clamp(sphere_pos.x, aabb_min.x, aabb_max.x),
    std::clamp(sphere_pos.y, aabb_min.y, aabb_max.y),
    std::clamp(sphere_pos.z, aabb_min.z, aabb_max.z)
  };

  vec3f delta = sphere_pos - closest;
  float dist_squared = linalg::dot(delta, delta);
  return dist_squared <= (sphere_radius * sphere_radius);
}

namespace
{

// A capsule's central segment, in world space. `size.x` is the radius and
// `size.y` the half-height of the cylinder portion (the caps add radius on top).
void capsule_segment(const vec3f& center, const entities::Hitbox& hitbox, vec3f& out_p0,
                     vec3f& out_p1)
{
  const float half_height = hitbox.size.y;
  out_p0 = {center.x, center.y - half_height, center.z};
  out_p1 = {center.x, center.y + half_height, center.z};
}

// The axis-aligned bound of a hitbox, whatever its shape. Used for the pairings
// that have no exact test: box/box, and capsule/capsule (which would want GJK).
void hitbox_bounds(const vec3f& center, const entities::Hitbox& hitbox, vec3f& out_min,
                   vec3f& out_max)
{
  switch (hitbox.shape)
  {
    case entities::Shape_Kind::Sphere:
    {
      const vec3f extent{hitbox.size.x, hitbox.size.x, hitbox.size.x};
      out_min = center - extent;
      out_max = center + extent;
      return;
    }

    case entities::Shape_Kind::Capsule:
    {
      const float radius     = hitbox.size.x;
      const float half_total = hitbox.size.y + radius;
      out_min = {center.x - radius, center.y - half_total, center.z - radius};
      out_max = {center.x + radius, center.y + half_total, center.z + radius};
      return;
    }

    case entities::Shape_Kind::Box:
      out_min = center - hitbox.size;
      out_max = center + hitbox.size;
      return;
  }
}

bool bounds_overlap(const vec3f& a_min, const vec3f& a_max, const vec3f& b_min,
                    const vec3f& b_max)
{
  return (a_min.x <= b_max.x && a_max.x >= b_min.x) &&
         (a_min.y <= b_max.y && a_max.y >= b_min.y) &&
         (a_min.z <= b_max.z && a_max.z >= b_min.z);
}

} // namespace

bool test_hitbox_collision(const vec3f& entity_a_pos, const entities::Hitbox& hitbox_a,
                           const vec3f& entity_b_pos, const entities::Hitbox& hitbox_b)
{
  // Apply offsets to get world-space hitbox centers
  const vec3f pos_a = entity_a_pos + hitbox_a.offset;
  const vec3f pos_b = entity_b_pos + hitbox_b.offset;

  // Sphere vs sphere
  if (hitbox_a.shape == entities::Shape_Kind::Sphere &&
      hitbox_b.shape == entities::Shape_Kind::Sphere)
    return test_sphere_sphere(pos_a, hitbox_a.size.x, pos_b, hitbox_b.size.x);

  // Sphere vs capsule, either way round
  if (hitbox_a.shape == entities::Shape_Kind::Sphere &&
      hitbox_b.shape == entities::Shape_Kind::Capsule)
  {
    vec3f p0, p1;
    capsule_segment(pos_b, hitbox_b, p0, p1);
    return test_sphere_capsule(pos_a, hitbox_a.size.x, p0, p1, hitbox_b.size.x);
  }

  if (hitbox_a.shape == entities::Shape_Kind::Capsule &&
      hitbox_b.shape == entities::Shape_Kind::Sphere)
  {
    vec3f p0, p1;
    capsule_segment(pos_a, hitbox_a, p0, p1);
    return test_sphere_capsule(pos_b, hitbox_b.size.x, p0, p1, hitbox_a.size.x);
  }

  // Sphere vs box, either way round
  if (hitbox_a.shape == entities::Shape_Kind::Sphere &&
      hitbox_b.shape == entities::Shape_Kind::Box)
    return test_sphere_aabb(pos_a, hitbox_a.size.x, pos_b - hitbox_b.size,
                            pos_b + hitbox_b.size);

  if (hitbox_a.shape == entities::Shape_Kind::Box &&
      hitbox_b.shape == entities::Shape_Kind::Sphere)
    return test_sphere_aabb(pos_b, hitbox_b.size.x, pos_a - hitbox_a.size,
                            pos_a + hitbox_a.size);

  // Everything left (box/box, capsule/capsule, capsule/box) falls back to the
  // axis-aligned bounds of both shapes. Approximate, but it is an OVERLAP
  // approximation rather than the old "no combination matched, so no hit".
  vec3f a_min, a_max, b_min, b_max;
  hitbox_bounds(pos_a, hitbox_a, a_min, a_max);
  hitbox_bounds(pos_b, hitbox_b, b_min, b_max);
  return bounds_overlap(a_min, a_max, b_min, b_max);
}

} // namespace shared
