#pragma once

#include "../shared/color.hpp"
#include "../shared/hit_region.hpp"
#include "../shared/hitbox_rig.hpp"
#include "../shared/linalg.hpp"
#include "../shared/log.hpp"
#include "../shared/span.hpp"

#include <cmath>

namespace client
{

// Longitude segments in every ring, wire and solid alike. ONE constant on
// purpose: a face ring coarser than the wire ring sitting on it reads as the
// wireframe floating off its own surface.
constexpr uint32_t HITBOX_RING_SEGMENTS = 16;

// Latitude bands per hemispherical cap. Three is where a capsule end stops
// reading as a cone.
constexpr uint32_t HITBOX_CAP_BANDS = 3;

// Solid faces UNDER the edges, and deliberately tiny. Two "over" layers
// composited in the wrong order differ by alpha1*alpha2*(colour1 - colour2) --
// O(alpha^2) -- so at 12% a handful of volumes landing in append order rather
// than depth order are within ~1.4% of the sorted answer. That is why none of
// the overlays need a sort: transparency ordering only bites at high alpha.
constexpr uint8_t HITBOX_FACE_ALPHA = 30;

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

// An orthonormal pair spanning the plane perpendicular to `axis`, right-handed
// in the order (side, up, axis) -- which is what makes every winding rule below
// come out outward-facing with no per-shape sign.
struct hitbox_basis_t
{
  linalg::vec3f side;
  linalg::vec3f up;
};

inline hitbox_basis_t basis_around(const linalg::vec3f& axis)
{
  // Any vector not parallel to the axis works; picking the world axis the shape
  // is least aligned with keeps the cross product well conditioned.
  const linalg::vec3f seed =
      std::fabs(axis.y) < 0.9f ? linalg::vec3f{0, 1, 0} : linalg::vec3f{1, 0, 0};
  const linalg::vec3f side = linalg::normalize(linalg::cross(axis, seed));
  return {side, linalg::cross(axis, side)};
}

inline float hitbox_ring_angle(uint32_t step)
{
  return (float)step / (float)HITBOX_RING_SEGMENTS * 2.0f * linalg::PI;
}

inline linalg::vec3f hitbox_ring_point(const linalg::vec3f& center, const hitbox_basis_t &basis,
                                       float radius, float angle)
{
  return center + basis.side * (std::cos(angle) * radius) + basis.up * (std::sin(angle) * radius);
}

// --- Wireframe ---

template <typename Line_Bucket>
void draw_wire_circle(const Line_Bucket &line, const linalg::vec3f& center, float radius,
                      const linalg::vec3f& normal, color_t color)
{
  const hitbox_basis_t basis = basis_around(linalg::normalize(normal));

  linalg::vec3f previous = hitbox_ring_point(center, basis, radius, 0.0f);
  for (uint32_t step = 1; step <= HITBOX_RING_SEGMENTS; ++step)
  {
    const linalg::vec3f point = hitbox_ring_point(center, basis, radius, hitbox_ring_angle(step));
    line(previous, point, color);
    previous = point;
  }
}

template <typename Line_Bucket>
void draw_wire_sphere(const Line_Bucket &line, const linalg::vec3f& center, float radius,
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
template <typename Line_Bucket>
void draw_wire_tube(const Line_Bucket &line, const linalg::vec3f& start, const linalg::vec3f& end,
                    float radius, bool rounded_caps, color_t color)
{
  const linalg::vec3f along  = end - start;
  const float         length = linalg::length(along);
  if (length < 1e-4f)
  {
    draw_wire_sphere(line, start, radius, color);
    return;
  }

  const linalg::vec3f  axis  = along * (1.0f / length);
  const hitbox_basis_t basis = basis_around(axis);

  draw_wire_circle(line, start, radius, axis, color);
  draw_wire_circle(line, end, radius, axis, color);

  for (const linalg::vec3f& offset :
       {basis.side * radius, basis.side * -radius, basis.up * radius, basis.up * -radius})
    line(start + offset, end + offset, color);

  if (rounded_caps)
  {
    draw_wire_circle(line, start, radius, basis.side, color);
    draw_wire_circle(line, end, radius, basis.up, color);
  }
}

// An ORIENTED box: twelve edges built from the volume's own axes.
template <typename Line_Bucket>
void draw_wire_oriented_box(const Line_Bucket &line, const linalg::vec3f& center,
                            const assets::hitbox_frame_t &frame,
                            const linalg::vec3f& half_extents, color_t color)
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

template <typename Line_Bucket>
void draw_posed_hitbox(const Line_Bucket &line, const assets::posed_hitbox_t &hitbox,
                       color_t color)
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

// --- Solid faces ---

// A hemisphere around `axis`: latitude 0 is the equator ring, latitude pi/2 the
// pole at center + axis * radius. The polar band is a triangle rather than a
// quad with two coincident corners, so nothing downstream fans a zero-area
// triangle.
template <typename Face_Bucket>
void draw_solid_cap(const Face_Bucket &face, const linalg::vec3f& center, float radius,
                    const linalg::vec3f& axis, color_t color)
{
  const hitbox_basis_t basis = basis_around(axis);

  const auto point = [&](uint32_t band, uint32_t step) {
    const float latitude = (float)band / (float)HITBOX_CAP_BANDS * 0.5f * linalg::PI;
    const float angle    = hitbox_ring_angle(step);
    return center + (basis.side * (std::cos(angle) * std::cos(latitude)) +
                     basis.up * (std::sin(angle) * std::cos(latitude)) +
                     axis * std::sin(latitude)) *
                        radius;
  };

  const linalg::vec3f pole = center + axis * radius;

  for (uint32_t band = 0; band < HITBOX_CAP_BANDS; ++band)
    for (uint32_t step = 0; step < HITBOX_RING_SEGMENTS; ++step)
    {
      const linalg::vec3f lower_start = point(band, step);
      const linalg::vec3f lower_end   = point(band, step + 1);

      if (band + 1 == HITBOX_CAP_BANDS)
      {
        const linalg::vec3f triangle[3] = {lower_start, lower_end, pole};
        face(Span<const linalg::vec3f>(triangle, 3), color);
        continue;
      }

      const linalg::vec3f quad[4] = {lower_start, lower_end, point(band + 1, step + 1),
                                     point(band + 1, step)};
      face(Span<const linalg::vec3f>(quad, 4), color);
    }
}

template <typename Face_Bucket>
void draw_solid_sphere(const Face_Bucket &face, const linalg::vec3f& center, float radius,
                       color_t color)
{
  draw_solid_cap(face, center, radius, {0, 1, 0}, color);
  draw_solid_cap(face, center, radius, {0, -1, 0}, color);
}

template <typename Face_Bucket>
void draw_solid_tube(const Face_Bucket &face, const linalg::vec3f& start, const linalg::vec3f& end,
                     float radius, bool rounded_caps, color_t color)
{
  const linalg::vec3f along  = end - start;
  const float         length = linalg::length(along);
  if (length < 1e-4f)
  {
    draw_solid_sphere(face, start, radius, color);
    return;
  }

  const linalg::vec3f  axis  = along * (1.0f / length);
  const hitbox_basis_t basis = basis_around(axis);

  for (uint32_t step = 0; step < HITBOX_RING_SEGMENTS; ++step)
  {
    const float angle_start = hitbox_ring_angle(step);
    const float angle_end   = hitbox_ring_angle(step + 1);

    const linalg::vec3f quad[4] = {hitbox_ring_point(start, basis, radius, angle_start),
                                   hitbox_ring_point(start, basis, radius, angle_end),
                                   hitbox_ring_point(end, basis, radius, angle_end),
                                   hitbox_ring_point(end, basis, radius, angle_start)};
    face(Span<const linalg::vec3f>(quad, 4), color);
  }

  if (rounded_caps)
  {
    draw_solid_cap(face, end, radius, axis, color);
    draw_solid_cap(face, start, radius, axis * -1.0f, color);
    return;
  }

  // Flat discs. A ring is convex, so each is one polygon the bucket fans itself;
  // the start disc is wound backwards because its outward normal is -axis.
  linalg::vec3f disc[HITBOX_RING_SEGMENTS];
  for (uint32_t step = 0; step < HITBOX_RING_SEGMENTS; ++step)
    disc[step] = hitbox_ring_point(end, basis, radius, hitbox_ring_angle(step));
  face(Span<const linalg::vec3f>(disc, HITBOX_RING_SEGMENTS), color);

  for (uint32_t step = 0; step < HITBOX_RING_SEGMENTS; ++step)
    disc[step] =
        hitbox_ring_point(start, basis, radius, hitbox_ring_angle(HITBOX_RING_SEGMENTS - step));
  face(Span<const linalg::vec3f>(disc, HITBOX_RING_SEGMENTS), color);
}

// Bit 0 is +right, bit 1 is +up, bit 2 is +forward -- the same corner indexing
// draw_wire_oriented_box uses, so the edges and the faces cannot disagree about
// which corner is which.
template <typename Face_Bucket>
void draw_solid_oriented_box(const Face_Bucket &face, const linalg::vec3f& center,
                             const assets::hitbox_frame_t &frame,
                             const linalg::vec3f& half_extents, color_t color)
{
  linalg::vec3f corners[8];
  for (uint32_t index = 0; index < 8; ++index)
  {
    const float x  = (index & 1) ? half_extents.x : -half_extents.x;
    const float y  = (index & 2) ? half_extents.y : -half_extents.y;
    const float z  = (index & 4) ? half_extents.z : -half_extents.z;
    corners[index] = center + frame.right * x + frame.up * y + frame.forward * z;
  }

  // Each row is wound counter-clockwise seen from OUTSIDE, so
  // cross(b - a, c - b) is the outward normal.
  static constexpr uint32_t FACES[6][4] = {
      {1, 3, 7, 5}, {0, 4, 6, 2}, // +right,   -right
      {2, 6, 7, 3}, {0, 1, 5, 4}, // +up,      -up
      {4, 5, 7, 6}, {0, 2, 3, 1}, // +forward, -forward
  };

  for (const uint32_t *quad : FACES)
  {
    const linalg::vec3f face_corners[4] = {corners[quad[0]], corners[quad[1]], corners[quad[2]],
                                           corners[quad[3]]};
    face(Span<const linalg::vec3f>(face_corners, 4), color);
  }
}

template <typename Face_Bucket>
void draw_posed_hitbox_faces(const Face_Bucket &face, const assets::posed_hitbox_t &hitbox,
                             color_t color)
{
  switch (hitbox.shape)
  {
    case assets::hitbox_shape_t::Sphere:
      draw_solid_sphere(face, hitbox.start, hitbox.radius, color);
      break;
    case assets::hitbox_shape_t::Capsule:
      draw_solid_tube(face, hitbox.start, hitbox.end, hitbox.radius, true, color);
      break;
    case assets::hitbox_shape_t::Cylinder:
      draw_solid_tube(face, hitbox.start, hitbox.end, hitbox.radius, false, color);
      break;
    case assets::hitbox_shape_t::Box:
      draw_solid_oriented_box(face, hitbox.center(), hitbox.frame, hitbox.half_extents, color);
      break;
    default:
      log_warning("[hitbox] unknown shape {}", (uint32_t)hitbox.shape);
      break;
  }
}

} // namespace client
