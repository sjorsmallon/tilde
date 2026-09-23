#include "bounce_body.hpp"

#include "projectile_sweep.hpp"

#include <algorithm>
#include <cmath>

namespace shared
{

bounce_body_t bounce_step(const Bounding_Volume_Hierarchy& bvh, const predicted_world_t& world,
                          const bounce_body_t& body, float radius, float gravity, float dt)
{
  if (body.bounce.at_rest || dt <= 0.f)
    return body;

  bounce_body_t out = body;
  linalg::vec3f velocity = out.bounce.velocity;
  velocity.y -= gravity * dt;

  float remaining = dt;
  for (int contact = 0; contact < BOUNCE_MAX_CONTACTS_PER_STEP && remaining > 0.f; ++contact)
  {
    const linalg::vec3f to = out.position + velocity * remaining;
    const std::optional<projectile_hit_t> hit =
        sweep_projectile(bvh, world, out.position, to, radius);
    if (!hit)
    {
      out.position = to;
      break;
    }

    // A hair off the face, so the next sweep starts strictly outside it rather
    // than on it, where float rounding decides which side it is on.
    const linalg::vec3f normal = hit->normal;
    out.position = hit->position + normal * BOUNCE_CONTACT_EPSILON;

    const float into = linalg::dot(velocity, normal);
    if (into < 0.f)
      velocity = velocity - normal * ((1.f + out.bounce.restitution) * into);

    const bool floor_like = normal.y > BOUNCE_REST_NORMAL_Y;
    if (floor_like)
    {
      const linalg::vec3f tangential = velocity - normal * linalg::dot(velocity, normal);
      velocity = velocity - tangential * std::min(1.f, out.bounce.friction * dt);
    }

    // A start inside a solid answers t = 0: step out along the face so the next
    // sweep has somewhere to go rather than answering zero forever.
    if (hit->t == 0.f)
      out.position = out.position + normal * 0.5f;

    remaining *= (1.f - hit->t);

    if (floor_like && linalg::length(velocity) < BOUNCE_REST_SPEED)
    {
      out.bounce.velocity         = {0.f, 0.f, 0.f};
      out.bounce.angular_velocity = {0.f, 0.f, 0.f};
      out.bounce.at_rest          = true;
      return out;
    }
  }

  out.bounce.velocity = velocity;

  const float spin_rate = linalg::length(out.bounce.angular_velocity);
  if (spin_rate > 0.f)
    out.orientation = linalg::normalize(
        linalg::from_axis_angle(out.bounce.angular_velocity, spin_rate * dt) * out.orientation);

  return out;
}

linalg::vec3f throw_spin_for(uint32_t tick, uint32_t uid, float degrees_per_second)
{
  uint32_t hash = tick * 2654435761u ^ (uid + 0x9e3779b9u) * 2246822519u;
  hash ^= hash >> 15;
  hash *= 2246822519u;
  hash ^= hash >> 13;

  const linalg::vec3f axis{static_cast<float>(hash & 0xffu) / 127.5f - 1.f,
                           static_cast<float>((hash >> 8) & 0xffu) / 127.5f - 1.f,
                           static_cast<float>((hash >> 16) & 0xffu) / 127.5f - 1.f};
  const float axis_length = linalg::length(axis);
  if (axis_length < 1e-3f)
    return {0.f, degrees_per_second, 0.f};
  return axis * (degrees_per_second / axis_length);
}

} // namespace shared
