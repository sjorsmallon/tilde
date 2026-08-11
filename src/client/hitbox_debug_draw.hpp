#pragma once

// Wireframes for the posed hit volumes, in ONE place because two callers draw
// them: the Animation tool (over the preview model, through an
// overlay_renderer_t) and the in-game overlay (over a remote player, through
// renderer::draw_line). Two copies would be two chances for the tool to show a
// shape the game does not.
//
// Everything here is line segments, so the only thing a caller supplies is
// somewhere to put a line -- any `void(const vec3f& start, const vec3f& end,
// color_t)` callable. That is what lets one implementation serve two sinks that
// share no base class.
//
// The volumes must already be in the space being drawn in; see
// shared::compute_player_hitboxes for the world-space placement.

#include "../shared/color.hpp"
#include "../shared/hit_region.hpp"
#include "../shared/hitbox_rig.hpp"
#include "../shared/linalg.hpp"
#include "../shared/log.hpp"

#include <cmath>

namespace client
{

// Damage region, not volume: ten volumes share three colours, so a forearm that
// costs Torso damage reads as one at a glance.
inline color_t hit_region_color(shared::hit_region_t region)
{
  switch (region)
  {
    case shared::hit_region_t::Head:  return colors::red;
    case shared::hit_region_t::Torso: return colors::yellow;
    case shared::hit_region_t::Legs:  return colors::cyan;
    default:                          return colors::grey;
  }
}

template <typename Line_Sink>
void draw_wire_circle(const Line_Sink &line, const linalg::vec3f &center, float radius,
                      const linalg::vec3f &normal, color_t color)
{
  constexpr uint32_t SEGMENTS = 16;

  const linalg::vec3f axis = linalg::normalize(normal);
  const linalg::vec3f seed =
      std::fabs(axis.y) < 0.9f ? linalg::vec3f{0, 1, 0} : linalg::vec3f{1, 0, 0};
  const linalg::vec3f side = linalg::normalize(linalg::cross(axis, seed));
  const linalg::vec3f up   = linalg::cross(axis, side);

  linalg::vec3f previous = center + side * radius;
  for (uint32_t step = 1; step <= SEGMENTS; ++step)
  {
    const float         angle = (float)step / (float)SEGMENTS * 2.0f * linalg::PI;
    const linalg::vec3f point =
        center + side * (std::cos(angle) * radius) + up * (std::sin(angle) * radius);
    line(previous, point, color);
    previous = point;
  }
}

template <typename Line_Sink>
void draw_wire_sphere(const Line_Sink &line, const linalg::vec3f &center, float radius,
                      color_t color)
{
  // Three great circles -- enough to read as a sphere from any angle.
  draw_wire_circle(line, center, radius, {0, 1, 0}, color);
  draw_wire_circle(line, center, radius, {1, 0, 0}, color);
  draw_wire_circle(line, center, radius, {0, 0, 1}, color);
}

// A capsule or a cylinder: a ring at each end and four lines down the sides. The
// two differ only in the caps -- a capsule gets rings THROUGH the ends so they
// read as rounded, a cylinder does not, which is the whole visible difference
// and also the whole difference in the hit test.
template <typename Line_Sink>
void draw_wire_tube(const Line_Sink &line, const linalg::vec3f &start, const linalg::vec3f &end,
                    float radius, bool rounded_caps, color_t color)
{
  const linalg::vec3f along  = end - start;
  const float         length = linalg::length(along);
  if (length < 1e-4f)
  {
    draw_wire_sphere(line, start, radius, color);
    return;
  }

  const linalg::vec3f axis = along * (1.0f / length);

  // Any vector not parallel to the axis works; picking the world axis the tube
  // is least aligned with keeps the cross product well conditioned.
  const linalg::vec3f seed =
      std::fabs(axis.y) < 0.9f ? linalg::vec3f{0, 1, 0} : linalg::vec3f{1, 0, 0};
  const linalg::vec3f side = linalg::normalize(linalg::cross(axis, seed));
  const linalg::vec3f up   = linalg::cross(axis, side);

  draw_wire_circle(line, start, radius, axis, color);
  draw_wire_circle(line, end, radius, axis, color);

  for (const linalg::vec3f &offset : {side * radius, side * -radius, up * radius, up * -radius})
    line(start + offset, end + offset, color);

  if (rounded_caps)
  {
    draw_wire_circle(line, start, radius, side, color);
    draw_wire_circle(line, end, radius, up, color);
  }
}

// An ORIENTED box: twelve edges built from the volume's own axes.
template <typename Line_Sink>
void draw_wire_oriented_box(const Line_Sink &line, const linalg::vec3f &center,
                            const assets::hitbox_frame_t &frame,
                            const linalg::vec3f &half_extents, color_t color)
{
  linalg::vec3f corners[8];

  //@NOTE(SJM): in my life I have never seen this trick. why not just write it out?
  for (uint32_t index = 0; index < 8; ++index)
  {
    const float x  = (index & 1) ? half_extents.x : -half_extents.x;
    const float y  = (index & 2) ? half_extents.y : -half_extents.y;
    const float z  = (index & 4) ? half_extents.z : -half_extents.z;
    corners[index] = center + frame.right * x + frame.up * y + frame.forward * z;
  }

  for (uint32_t from = 0; from < 8; ++from)
    for (uint32_t bit = 1; bit <= 4; bit <<= 1)
      if ((from & bit) == 0)
        line(corners[from], corners[from | bit], color);
}

template <typename Line_Sink>
void draw_posed_hitbox(const Line_Sink &line, const assets::posed_hitbox_t &hitbox, color_t color)
{
  switch (hitbox.shape)
  {
    case assets::hitbox_shape_t::Sphere:
      draw_wire_sphere(line, hitbox.start, hitbox.radius, color);
      break;
    case assets::hitbox_shape_t::Capsule:
      draw_wire_tube(line, hitbox.start, hitbox.end, hitbox.radius, true, color);
      break;
    case assets::hitbox_shape_t::Cylinder:
      draw_wire_tube(line, hitbox.start, hitbox.end, hitbox.radius, false, color);
      break;
    case assets::hitbox_shape_t::Box:
      draw_wire_oriented_box(line, hitbox.center(), hitbox.frame, hitbox.half_extents, color);
      break;
    default:
      log_warning("[hitbox] unknown shape {}", (uint32_t)hitbox.shape);
      break;
  }
}

} // namespace client
