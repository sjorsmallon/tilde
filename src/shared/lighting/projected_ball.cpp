#include "projected_ball.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace shared
{

namespace
{

struct overlap_t
{
  linalg::vec2f normal;
  float         depth;
};

float cross(const linalg::vec2f& a, const linalg::vec2f& b)
{
  return a.u * b.v - a.v * b.u;
}

// The least push that takes the disc out of the quad; a centre inside it leaves by the nearest edge. The
// quad's winding is read from its area, so either order round the corners works; a repeated corner is skipped.
[[nodiscard]] std::optional<overlap_t> try_find_overlap(const projector_mask_quad_t& quad, const linalg::vec2f& centre,
                                                        float radius)
{
  float twice_area = 0.f;
  for (uint32_t index = 0; index < 4; ++index)
    twice_area += cross(quad.corners[index], quad.corners[(index + 1) % 4]);
  if (std::fabs(twice_area) < 1e-12f)
    return std::nullopt;
  const float winding = twice_area > 0.f ? 1.f : -1.f;

  bool          inside          = true;
  float         least_inside    = 1e9f;
  linalg::vec2f inside_normal   = {0.f, -1.f};
  float         closest         = 1e9f;
  linalg::vec2f closest_point   = centre;
  linalg::vec2f closest_outward = {0.f, -1.f};
  for (uint32_t index = 0; index < 4; ++index)
  {
    const linalg::vec2f a      = quad.corners[index];
    const linalg::vec2f edge   = quad.corners[(index + 1) % 4] - a;
    const float         length_squared = linalg::dot(edge, edge);
    if (length_squared < 1e-12f)
      continue;
    const float         length  = std::sqrt(length_squared);
    const linalg::vec2f outward = linalg::vec2f{edge.v, -edge.u} * (winding / length);
    const float         side    = cross(edge, centre - a) * winding;
    if (side < 0.f)
      inside = false;
    else if (side / length < least_inside)
    {
      least_inside  = side / length;
      inside_normal = outward;
    }

    const float         along = std::clamp(linalg::dot(centre - a, edge) / length_squared, 0.f, 1.f);
    const linalg::vec2f point = a + edge * along;
    const float         away  = linalg::length(centre - point);
    if (away < closest)
    {
      closest         = away;
      closest_point   = point;
      closest_outward = outward;
    }
  }

  if (inside)
    return overlap_t{inside_normal, least_inside + radius};
  if (closest >= radius)
    return std::nullopt;
  if (closest > 0.f)
    return overlap_t{(centre - closest_point) * (1.f / closest), radius - closest};
  return overlap_t{closest_outward, radius};
}

[[nodiscard]] std::optional<overlap_t> try_find_deepest_overlap(Span<const projector_mask_quad_t> solids,
                                                                const linalg::vec2f& centre, float radius)
{
  std::optional<overlap_t> deepest;
  for (const projector_mask_quad_t& quad : solids)
  {
    const std::optional<overlap_t> overlap = try_find_overlap(quad, centre, radius);
    if (overlap && (!deepest || overlap->depth > deepest->depth))
      deepest = overlap;
  }
  return deepest;
}

} // namespace

projected_ball_t projected_ball_step(Span<const projector_mask_quad_t> solids, const projected_ball_t& ball,
                                     const projected_ball_settings_t& settings, float dt)
{
  if (dt <= 0.f)
    return ball;

  projected_ball_t out = ball;
  out.velocity.v += settings.gravity * dt;
  out.uv = out.uv + out.velocity * dt;

  const float settle_speed = settings.gravity * dt * PROJECTED_BALL_SETTLE_TICKS;
  const float keep_along   = 1.f - std::min(1.f, settings.friction * dt);

  for (int push = 0; push < PROJECTED_BALL_MAX_PUSHES_PER_STEP; ++push)
  {
    const std::optional<overlap_t> overlap = try_find_deepest_overlap(solids, out.uv, settings.radius);
    if (!overlap)
      break;

    out.uv = out.uv + overlap->normal * overlap->depth;

    const float into = linalg::dot(out.velocity, overlap->normal);
    if (into >= 0.f)
      continue;

    const linalg::vec2f tangential = out.velocity - overlap->normal * into;
    const float         outward    = -into <= settle_speed ? 0.f : -into * settings.restitution;
    out.velocity                   = tangential * keep_along + overlap->normal * outward;
  }

  return out;
}

bool projected_ball_is_outside_frame(const projected_ball_t& ball)
{
  return ball.uv.u <= 0.f || ball.uv.u >= 1.f || ball.uv.v <= 0.f || ball.uv.v >= 1.f;
}

std::optional<projector_mask_quad_t> try_project_hitbox_to_picture(const linalg::mat4f&          picture_matrix,
                                                                   const assets::posed_hitbox_t& hitbox)
{
  linalg::vec3f minimum;
  linalg::vec3f maximum;
  if (hitbox.shape == assets::hitbox_shape_t::Box)
  {
    const linalg::vec3f centre = hitbox.center();
    const linalg::vec3f reach  = {std::abs(hitbox.frame.right.x) * hitbox.half_extents.x +
                                      std::abs(hitbox.frame.up.x) * hitbox.half_extents.y +
                                      std::abs(hitbox.frame.forward.x) * hitbox.half_extents.z,
                                  std::abs(hitbox.frame.right.y) * hitbox.half_extents.x +
                                      std::abs(hitbox.frame.up.y) * hitbox.half_extents.y +
                                      std::abs(hitbox.frame.forward.y) * hitbox.half_extents.z,
                                  std::abs(hitbox.frame.right.z) * hitbox.half_extents.x +
                                      std::abs(hitbox.frame.up.z) * hitbox.half_extents.y +
                                      std::abs(hitbox.frame.forward.z) * hitbox.half_extents.z};
    minimum = centre - reach;
    maximum = centre + reach;
  }
  else
  {
    const linalg::vec3f radius = {hitbox.radius, hitbox.radius, hitbox.radius};
    minimum = linalg::vec3f{std::min(hitbox.start.x, hitbox.end.x), std::min(hitbox.start.y, hitbox.end.y),
                            std::min(hitbox.start.z, hitbox.end.z)} -
              radius;
    maximum = linalg::vec3f{std::max(hitbox.start.x, hitbox.end.x), std::max(hitbox.start.y, hitbox.end.y),
                            std::max(hitbox.start.z, hitbox.end.z)} +
              radius;
  }

  float u0 = 1e9f, v0 = 1e9f, u1 = -1e9f, v1 = -1e9f;
  for (int corner = 0; corner < 8; ++corner)
  {
    const linalg::vec3f point{(corner & 1) ? maximum.x : minimum.x, (corner & 2) ? maximum.y : minimum.y,
                              (corner & 4) ? maximum.z : minimum.z};
    const linalg::vec4  clip = picture_matrix * linalg::vec4{point.x, point.y, point.z, 1.f};
    if (clip.w <= 0.f)
      return std::nullopt;
    const float u = (clip.x / clip.w) * 0.5f + 0.5f;
    const float v = (clip.y / clip.w) * 0.5f + 0.5f;
    u0            = std::min(u0, u);
    v0            = std::min(v0, v);
    u1            = std::max(u1, u);
    v1            = std::max(v1, v);
  }
  if (u1 <= 0.f || u0 >= 1.f || v1 <= 0.f || v0 >= 1.f)
    return std::nullopt;
  return make_projector_mask_quad(u0, v0, u1, v1);
}

std::optional<projected_ball_room_point_t> try_find_projected_ball_room_point(const Bounding_Volume_Hierarchy& bvh,
                                                                            const scene_light_t&             light,
                                                                            linalg::vec2f uv, float uv_radius)
{
  const projector_axes_t axes            = projector_axes(light);
  const float            tangent         = std::tan(projector_half_angle(light));
  const float            aspect          = std::max(light.aspect, 1e-3f);
  const linalg::vec3f    direction_at_one = axes.forward + axes.right * ((2.f * uv.u - 1.f) * tangent * aspect) +
                                           axes.up * ((1.f - 2.f * uv.v) * tangent);
  const linalg::vec3f direction = linalg::normalize(direction_at_one);

  ray_hit_result_t hit{};
  if (!bvh_intersect_ray(bvh, light.position, direction, hit) || hit.t <= 0.f)
    return std::nullopt;

  const linalg::vec3f point = light.position + direction * hit.t;
  const float         depth = linalg::dot(point - light.position, axes.forward);
  return projected_ball_room_point_t{.point = point, .radius = uv_radius * 2.f * depth * tangent};
}

} // namespace shared
