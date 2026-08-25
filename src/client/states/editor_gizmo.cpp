#include "editor_gizmo.hpp"

#include "../renderer.hpp"
#include "../shared/color.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace client
{

using namespace linalg;

namespace
{

// The arm is this fraction of the viewport's half-height, so the gizmo covers
// the same screen area whether you are nose-to-nose with a crate or looking at
// the whole level.
constexpr float ARM_SCREEN_FRACTION = 0.16f;

// Handle geometry, all as multiples of the arm length so one number scales the
// whole widget.
constexpr float ARM_INNER_FRACTION  = 0.18f; // arrows start out from the centre
constexpr float RING_RADIUS_FACTOR  = 0.85f;
constexpr float RING_THICKNESS      = 0.08f;
constexpr float FACE_ARM_FACTOR     = 0.45f;
constexpr float PICK_RADIUS_FACTOR  = 0.13f;

// The two-axis quad, as a span along each of its axes. It sits clear of the
// arrows' pick radius on the inside and of the rotation band on the outside --
// its far corner is at 0.5*sqrt(2) = 0.71 arms, where the ring band starts
// at 0.77.
constexpr float PLANE_QUAD_INNER = 0.20f;
constexpr float PLANE_QUAD_OUTER = 0.50f;

// How square-on a plane handle has to be before it is offered at all, as
// |dot(plane normal, view direction)|. Below this it is a sliver.
constexpr float PLANE_QUAD_MIN_FACING = 0.25f;

constexpr int RING_SEGMENTS = 48;

vec3 axis_direction(int axis)
{
  vec3 direction = {0, 0, 0};
  direction[axis] = 1.f;
  return direction;
}

// Face index 0..5 -> +x,-x,+y,-y,+z,-z.
int  face_axis(int face) { return face / 2; }
float face_sign(int face) { return (face % 2 == 0) ? 1.f : -1.f; }

vec3 face_normal(int face) { return axis_direction(face_axis(face)) * face_sign(face); }

// The plane containing `axis` that most faces the camera. Dragging against it
// keeps the pointer's motion mapped to the axis at every viewing angle except
// straight down it, where the intersection test itself rejects.
vec3 camera_facing_plane_normal(const vec3 &axis, const vec3 &point, const vec3 &camera_position)
{
  const vec3 to_camera = point - camera_position;
  return cross(cross(axis, to_camera), axis);
}

// The two in-plane basis vectors for a ring about `axis`, ordered so a positive
// atan2 angle is a positive rotation about it.
void ring_basis(int axis, vec3 &out_u, vec3 &out_v)
{
  out_u = axis_direction((axis + 1) % 3);
  out_v = axis_direction((axis + 2) % 3);
}

// The four corners of a plane handle's quad, in winding order, for the plane
// whose NORMAL is `normal_axis`. It sits in the ++ quadrant of its two axes so
// it reads as belonging to the two arrows it sits between.
void plane_quad_corners(const vec3 &center, int normal_axis, float arm_length,
                        vec3 (&out_corners)[4])
{
  vec3 u, v;
  ring_basis(normal_axis, u, v);

  const vec3 inner_u = u * (arm_length * PLANE_QUAD_INNER);
  const vec3 outer_u = u * (arm_length * PLANE_QUAD_OUTER);
  const vec3 inner_v = v * (arm_length * PLANE_QUAD_INNER);
  const vec3 outer_v = v * (arm_length * PLANE_QUAD_OUTER);

  out_corners[0] = center + inner_u + inner_v;
  out_corners[1] = center + outer_u + inner_v;
  out_corners[2] = center + outer_u + outer_v;
  out_corners[3] = center + inner_u + outer_v;
}

float wrap_radians(float angle)
{
  while (angle > PI)
    angle -= 2.f * PI;
  while (angle < -PI)
    angle += 2.f * PI;
  return angle;
}

// Handle picking is a DISTANCE to the handle's line, via
// linalg::distance_from_ray_to_segment, rather than a ray test against a padded
// box around it: the three axis boxes all contained the gizmo centre, so a click
// anywhere near the middle picked whichever happened to be nearest, which is to
// say an arbitrary axis.

// One candidate handle, ranked by how far the ray passed from it.
struct pick_t
{
  gizmo_handle_t handle;
  float          separation = std::numeric_limits<float>::max();
};

void consider(pick_t &best, gizmo_handle_t handle, float separation, float threshold)
{
  if (separation <= threshold && separation < best.separation)
    best = {handle, separation};
}

// One axis of a snapped translation. With an origin the OBJECT lands on the
// grid; without one the MOVEMENT is a multiple of the step. See set_target.
float snapped_translation(const std::optional<vec3> &origin, int axis, float travelled,
                          float snap_step)
{
  if (!origin)
    return editor::snap(travelled, snap_step);

  return editor::snap((*origin)[axis] + travelled, snap_step) - (*origin)[axis];
}

} // namespace

void Editor_Gizmo::set_target(const shared::aabb_bounds_t &bounds,
                              gizmo_capabilities_t target_capabilities,
                              const gizmo_view_t &view,
                              const std::optional<linalg::vec3> &target_snap_origin)
{
  if (is_dragging())
    return;

  targeted     = true;
  box          = shared::to_aabb(bounds);
  capabilities = target_capabilities;
  snap_origin  = target_snap_origin;

  // Perspective: the arm subtends a constant angle, so its world length grows
  // with distance. Orthographic: there is no divide, so it tracks the zoom.
  if (view.orthographic)
  {
    arm_length = view.ortho_height * ARM_SCREEN_FRACTION;
  }
  else
  {
    const float distance = length(box.center - view.camera_position);
    arm_length = std::max(distance, 1.f) *
                 std::tan(to_radians(view.fov_degrees) * 0.5f) * ARM_SCREEN_FRACTION;
  }

  arm_length = std::max(arm_length, 1e-2f);

  // Derived from the eye rather than from the camera's forward vector: for a
  // perspective view that is the direction the gizmo is actually seen along,
  // which is what decides whether a quad reads as a sliver. An ortho editor
  // camera looks down an axis from far back, so the two agree there anyway.
  const vec3 to_gizmo = box.center - view.camera_position;
  const vec3 view_direction =
      length(to_gizmo) > 1e-4f ? normalize(to_gizmo) : vec3{0, 0, 1};
  for (int normal_axis = 0; normal_axis < 3; ++normal_axis)
    plane_handle_usable[normal_axis] =
        std::abs(view_direction[normal_axis]) >= PLANE_QUAD_MIN_FACING;
}

void Editor_Gizmo::clear_target()
{
  targeted = false;
  hovered  = {};
  dragged  = {};
}

void Editor_Gizmo::update_hover(const linalg::ray_t &ray)
{
  hovered = {};
  if (!targeted || is_dragging())
    return;

  // Plane handles first, and a hit on one WINS OUTRIGHT rather than competing
  // on distance: containment in a solid region is a stronger statement than
  // passing near a line, and the quad is placed so no arrow crosses it. Two
  // quads can be pierced by one ray at a shallow angle, so those tie-break on
  // depth.
  {
    float nearest_quad = std::numeric_limits<float>::max();
    for (int normal_axis = 0; normal_axis < 3; ++normal_axis)
    {
      if (!plane_handle_usable[normal_axis])
        continue;

      float distance_along_ray = 0.f;
      if (!intersect_ray_plane(ray.origin, ray.direction, box.center,
                               axis_direction(normal_axis), distance_along_ray) ||
          distance_along_ray <= 0.f || distance_along_ray >= nearest_quad)
        continue;

      vec3 u, v;
      ring_basis(normal_axis, u, v);
      const vec3  local     = (ray.origin + ray.direction * distance_along_ray) - box.center;
      const float along_u   = dot(local, u) / arm_length;
      const float along_v   = dot(local, v) / arm_length;

      if (along_u < PLANE_QUAD_INNER || along_u > PLANE_QUAD_OUTER ||
          along_v < PLANE_QUAD_INNER || along_v > PLANE_QUAD_OUTER)
        continue;

      nearest_quad = distance_along_ray;
      hovered      = {gizmo_handle_t::kind_t::Translate_Plane, (uint8_t)normal_axis};
    }

    if (hovered)
      return;
  }

  const float threshold = arm_length * PICK_RADIUS_FACTOR;
  pick_t      best;

  // Translate arrows. They start out from the centre rather than at it, which
  // removes the region where all three overlap instead of tie-breaking it.
  for (int axis = 0; axis < 3; ++axis)
  {
    const vec3  direction = axis_direction(axis);
    const float separation =
        distance_from_ray_to_segment(ray.origin, ray.direction,
                                     box.center + direction * (arm_length * ARM_INNER_FRACTION),
                                     box.center + direction * arm_length);
    consider(best, {gizmo_handle_t::kind_t::Translate, (uint8_t)axis}, separation, threshold);
  }

  if (capabilities.reshape)
  {
    for (int face = 0; face < 6; ++face)
    {
      const vec3  normal = face_normal(face);
      const vec3  root   = box.center + normal * box.half_extents[face_axis(face)];
      const float separation = distance_from_ray_to_segment(
          ray.origin, ray.direction, root, root + normal * (arm_length * FACE_ARM_FACTOR));
      consider(best, {gizmo_handle_t::kind_t::Reshape, (uint8_t)face}, separation, threshold);
    }
  }

  if (capabilities.rotate)
  {
    const float radius = arm_length * RING_RADIUS_FACTOR;
    for (int axis = 0; axis < 3; ++axis)
    {
      float distance_along_ray = 0.f;
      if (!intersect_ray_plane(ray.origin, ray.direction, box.center, axis_direction(axis),
                               distance_along_ray) ||
          distance_along_ray <= 0.f)
        continue;

      const vec3  hit          = ray.origin + ray.direction * distance_along_ray;
      const float from_centre  = length(hit - box.center);
      consider(best, {gizmo_handle_t::kind_t::Rotate, (uint8_t)axis},
               std::abs(from_centre - radius), arm_length * RING_THICKNESS);
    }
  }

  hovered = best.handle;
}

bool Editor_Gizmo::try_begin_drag(const linalg::ray_t &ray, const gizmo_view_t &view)
{
  if (!targeted || !hovered)
    return false;

  dragged           = hovered;
  start_box         = box;
  start_snap_origin = snap_origin;
  previous_angle    = 0.f;
  total_angle       = 0.f;

  const vec3 &camera_position = view.camera_position;

  switch (dragged.kind)
  {
  case gizmo_handle_t::kind_t::Translate:
  case gizmo_handle_t::kind_t::Reshape:
  {
    const bool is_reshape = dragged.kind == gizmo_handle_t::kind_t::Reshape;
    const vec3 axis       = is_reshape ? face_normal(dragged.index) : axis_direction(dragged.index);
    const vec3 anchor =
        is_reshape ? start_box.center + axis * start_box.half_extents[face_axis(dragged.index)]
                   : start_box.center;

    float distance_along_ray = 0.f;
    if (!intersect_ray_plane(ray.origin, ray.direction, anchor,
                             camera_facing_plane_normal(axis, anchor, camera_position),
                             distance_along_ray))
    {
      dragged = {};
      return false;
    }

    start_hit = ray.origin + ray.direction * distance_along_ray;
    return true;
  }

  case gizmo_handle_t::kind_t::Translate_Plane:
  {
    float distance_along_ray = 0.f;
    if (!intersect_ray_plane(ray.origin, ray.direction, start_box.center,
                             axis_direction(dragged.index), distance_along_ray))
    {
      dragged = {};
      return false;
    }

    start_hit = ray.origin + ray.direction * distance_along_ray;
    return true;
  }

  case gizmo_handle_t::kind_t::Rotate:
  {
    const vec3 axis = axis_direction(dragged.index);

    float distance_along_ray = 0.f;
    if (!intersect_ray_plane(ray.origin, ray.direction, start_box.center, axis,
                             distance_along_ray))
    {
      dragged = {};
      return false;
    }

    vec3 u, v;
    ring_basis(dragged.index, u, v);
    const vec3 local = (ray.origin + ray.direction * distance_along_ray) - start_box.center;
    previous_angle   = std::atan2(dot(local, v), dot(local, u));
    return true;
  }

  case gizmo_handle_t::kind_t::None:
    break;
  }

  dragged = {};
  return false;
}

std::optional<gizmo_drag_t> Editor_Gizmo::try_update_drag(const linalg::ray_t &ray,
                                                          const gizmo_view_t &view)
{
  if (!dragged)
    return std::nullopt;

  const vec3 &camera_position = view.camera_position;

  switch (dragged.kind)
  {
  case gizmo_handle_t::kind_t::Translate:
  {
    const int  axis      = dragged.index;
    const vec3 direction = axis_direction(axis);

    float distance_along_ray = 0.f;
    if (!intersect_ray_plane(
            ray.origin, ray.direction, start_box.center,
            camera_facing_plane_normal(direction, start_box.center, camera_position),
            distance_along_ray))
      return std::nullopt;

    const float travelled =
        dot(ray.origin + ray.direction * distance_along_ray - start_hit, direction);

    // Snap the DRAGGED component only. Snapping the whole vector re-gridded the
    // two axes the drag never touched, which silently moved any object that was
    // not already on the grid.
    gizmo_drag_t result;
    result.pivot = start_box.center;
    result.translation[axis] =
        snapped_translation(start_snap_origin, axis, travelled, snap_step);

    box.center = start_box.center + result.translation;
    return result;
  }

  case gizmo_handle_t::kind_t::Translate_Plane:
  {
    const int normal_axis = dragged.index;

    float distance_along_ray = 0.f;
    if (!intersect_ray_plane(ray.origin, ray.direction, start_box.center,
                             axis_direction(normal_axis), distance_along_ray))
      return std::nullopt;

    const vec3 travelled = (ray.origin + ray.direction * distance_along_ray) - start_hit;

    // The two in-plane components are snapped independently and the normal one
    // is left at exactly zero, so a plane drag can never nudge the third axis.
    gizmo_drag_t result;
    result.pivot = start_box.center;
    for (const int axis : {(normal_axis + 1) % 3, (normal_axis + 2) % 3})
      result.translation[axis] =
          snapped_translation(start_snap_origin, axis, travelled[axis], snap_step);

    box.center = start_box.center + result.translation;
    return result;
  }

  case gizmo_handle_t::kind_t::Rotate:
  {
    const int  axis      = dragged.index;
    const vec3 direction = axis_direction(axis);

    float distance_along_ray = 0.f;
    if (!intersect_ray_plane(ray.origin, ray.direction, start_box.center, direction,
                             distance_along_ray))
      return std::nullopt;

    vec3 u, v;
    ring_basis(axis, u, v);
    const vec3  local = (ray.origin + ray.direction * distance_along_ray) - start_box.center;
    const float angle = std::atan2(dot(local, v), dot(local, u));

    total_angle += wrap_radians(angle - previous_angle);
    previous_angle = angle;

    gizmo_drag_t result;
    result.pivot = start_box.center;
    result.rotation[axis] = editor::snap(to_degrees(total_angle), editor::ROTATION_SNAP);
    return result;
  }

  case gizmo_handle_t::kind_t::Reshape:
  {
    const int   axis   = face_axis(dragged.index);
    const float sign   = face_sign(dragged.index);
    const vec3  normal = face_normal(dragged.index);
    const vec3  anchor = start_box.center + normal * start_box.half_extents[axis];

    float distance_along_ray = 0.f;
    if (!intersect_ray_plane(ray.origin, ray.direction, anchor,
                             camera_facing_plane_normal(normal, anchor, camera_position),
                             distance_along_ray))
      return std::nullopt;

    // Outward-positive along the face normal, which is why the minus faces need
    // no sign correction of their own.
    const float travelled =
        dot(ray.origin + ray.direction * distance_along_ray - start_hit, normal);

    // Only the face being dragged moves, and only it is snapped. Snapping both
    // ends moved the opposite face of any box that started off-grid.
    const shared::aabb_bounds_t start_bounds = shared::get_bounds(start_box);
    float                       minimum      = start_bounds.min[axis];
    float                       maximum      = start_bounds.max[axis];

    if (sign > 0.f)
      maximum = std::max(editor::snap(maximum + travelled, snap_step),
                         minimum + editor::MIN_EXTENT);
    else
      minimum = std::min(editor::snap(minimum - travelled, snap_step),
                         maximum - editor::MIN_EXTENT);

    gizmo_drag_t result;
    result.pivot = start_box.center;
    shared::aabb_t reshaped        = start_box;
    reshaped.center[axis]          = (minimum + maximum) * 0.5f;
    reshaped.half_extents[axis]    = (maximum - minimum) * 0.5f;
    result.box                     = reshaped;
    result.translation             = reshaped.center - start_box.center;

    box = reshaped;
    return result;
  }

  case gizmo_handle_t::kind_t::None:
    break;
  }

  return std::nullopt;
}

void Editor_Gizmo::end_drag()
{
  dragged        = {};
  previous_angle = 0.f;
  total_angle    = 0.f;
}

void Editor_Gizmo::draw(pass_builder_t &draws) const
{
  if (!targeted)
    return;

  // Everything here is drawn when occluded: the gizmo sits at the object's
  // centre, which for a solid brush is inside it, so depth-tested it is buried
  // exactly when it is needed. The rings used to be the one part that was not,
  // and they vanished into anything they were placed in.
  const auto handle_color = [&](gizmo_handle_t handle, color_t base)
  { return (hovered == handle || dragged == handle) ? colors::white : base; };

  constexpr color_t axis_colors[3] = {colors::red, colors::green, colors::blue};

  for (int axis = 0; axis < 3; ++axis)
  {
    const vec3 direction = axis_direction(axis);
    draws.debug.arrow(box.center + direction * (arm_length * ARM_INNER_FRACTION),
                      box.center + direction * arm_length,
                      handle_color({gizmo_handle_t::kind_t::Translate, (uint8_t)axis},
                                   axis_colors[axis]),
                      0.f, true);
  }

  for (int normal_axis = 0; normal_axis < 3; ++normal_axis)
  {
    if (!plane_handle_usable[normal_axis])
      continue;

    const gizmo_handle_t handle = {gizmo_handle_t::kind_t::Translate_Plane,
                                   (uint8_t)normal_axis};
    const color_t        color  = handle_color(handle, axis_colors[normal_axis]);

    vec3 corners[4];
    plane_quad_corners(box.center, normal_axis, arm_length, corners);

    // Translucent fill plus an opaque border: the fill is what you aim at, the
    // border is what makes it legible against whatever is behind it. `rim` is
    // off because this IS a flat quad looked at face-on, which is the case the
    // rim term is documented as wrong for.
    draws.debug.filled_polygon(Span<const linalg::vec3f>(corners, 4),
                               with_alpha(color, hovered == handle ? 0xAA : 0x55), 0.f,
                               {.shaded = false, .draw_when_occluded = true, .rim = false});

    for (int corner = 0; corner < 4; ++corner)
      draws.debug.line(corners[corner], corners[(corner + 1) % 4], color, 0.f, 0.f, true);
  }

  if (capabilities.reshape)
  {
    for (int face = 0; face < 6; ++face)
    {
      const vec3 normal = face_normal(face);
      const vec3 root   = box.center + normal * box.half_extents[face_axis(face)];
      draws.debug.arrow(root, root + normal * (arm_length * FACE_ARM_FACTOR),
                        handle_color({gizmo_handle_t::kind_t::Reshape, (uint8_t)face},
                                     colors::gold),
                        0.f, true);
    }
  }

  if (capabilities.rotate)
  {
    const float radius = arm_length * RING_RADIUS_FACTOR;
    for (int axis = 0; axis < 3; ++axis)
    {
      const color_t color =
          handle_color({gizmo_handle_t::kind_t::Rotate, (uint8_t)axis}, axis_colors[axis]);

      vec3 u, v;
      ring_basis(axis, u, v);

      vec3 previous = box.center + u * radius;
      for (int segment = 1; segment <= RING_SEGMENTS; ++segment)
      {
        const float angle = (float)segment / (float)RING_SEGMENTS * 2.f * PI;
        const vec3  point =
            box.center + u * (std::cos(angle) * radius) + v * (std::sin(angle) * radius);
        draws.debug.line(previous, point, color, 0.f, 0.f, true);
        previous = point;
      }
    }
  }
}

} // namespace client
