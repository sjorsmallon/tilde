#pragma once

#include "../frame_builder.hpp"
#include "../shared/aabb.hpp"
#include "../shared/editor_grid.hpp"
#include "linalg.hpp"

#include <cstdint>
#include <optional>

namespace client
{


struct gizmo_handle_t
{
  enum class kind_t : uint8_t
  {
    None,
    Translate, // index is the axis, 0..2 as x,y,z

    // The corner quad that drags in two axes at once. `index` is the axis it is
    // PERPENDICULAR to, not one of the two it moves in: a plane has one normal
    // and two tangents, so the normal is the name that fits in one number.
    // 0 = the yz plane, 1 = xz, 2 = xy.
    Translate_Plane,

    Rotate,  // index is the ring's axis, 0..2 as x,y,z
    Reshape, // index is the box face, 0..5 as +x,-x,+y,-y,+z,-z
  };

  kind_t  kind  = kind_t::None;
  uint8_t index = 0;

  bool             operator==(const gizmo_handle_t &) const = default;
  explicit         operator bool() const { return kind != kind_t::None; }
};

struct gizmo_capabilities_t
{
  bool rotate  = false;
  bool reshape = false;
};


struct gizmo_view_t
{
  linalg::vec3 camera_position = {0, 0, 0};
  bool         orthographic    = false;
  float        ortho_height    = 1024.f;
  float        fov_degrees     = 90.f;
};


struct gizmo_drag_t
{
  linalg::vec3 translation = {0, 0, 0}; // world units
  linalg::quatf rotation   = linalg::quatf::identity();
 
  // where the gizmo sat when it was pressed.
  linalg::vec3 pivot = {0, 0, 0};

  std::optional<shared::aabb_t> box;
};

class Editor_Gizmo
{
public:
  float snap_step = editor::MAJOR_GRID_STEP;

  // Where the gizmo sits, what it may do, and what translation snapping
  // measures against. Call every frame the gizmo is idle; it is ignored during a
  // drag, so the widget does not chase the object it is moving.
  //
  // `snap_origin` with a value means SNAP THE OBJECT: a drag lands that point
  // itself on the grid, so a caller passes whatever the map actually stores --
  // an entity's position, a box's centre. It deliberately is NOT derived from
  // `bounds`, because an origin need not sit at the centre of its own bounds: a
  // spawn's hull runs from its FEET up 72 units, so snapping the hull centre
  // left the feet 36 off every grid step coarser than 4, and a spawn could never
  // be stood on top of a brush.
  //
  // Absent means SNAP THE MOVEMENT: the translation is a multiple of the step.
  // That is what a multi-selection wants -- one absolute origin cannot align
  // every member, while a quantized delta keeps each member exactly as aligned
  // as it already was.
  void set_target(const shared::aabb_bounds_t &bounds, gizmo_capabilities_t capabilities,
                  const gizmo_view_t &view, const std::optional<linalg::vec3> &snap_origin);

  // Forget the target. A gizmo without one draws nothing and hit tests to
  // nothing, so a tool with an empty selection cannot report a stale hover from
  // wherever the gizmo last sat.
  void clear_target();

  void update_hover(const linalg::ray_t &ray);

  [[nodiscard]] bool has_target() const { return targeted; }
  [[nodiscard]] bool is_hovered() const { return (bool)hovered; }
  [[nodiscard]] bool is_dragging() const { return (bool)dragged; }

  // Begin dragging whatever update_hover last landed on.
  [[nodiscard]] bool try_begin_drag(const linalg::ray_t &ray, const gizmo_view_t &view);

  // Advance the live drag. Empty when no drag is live, or when this frame's ray
  // does not meet the drag plane -- a grazing angle is a frame with no new
  // answer, which is not the same as a zero one.
  [[nodiscard]] std::optional<gizmo_drag_t> try_update_drag(const linalg::ray_t &ray,
                                                            const gizmo_view_t &view);

  void end_drag();

  void draw(pass_builder_t &draws) const;

private:
  bool                        targeted = false;
  shared::aabb_t              box;
  gizmo_capabilities_t        capabilities;
  std::optional<linalg::vec3> snap_origin;

  // World length of a translate arm. Recomputed from the camera every frame so
  // the gizmo stays the same size on screen at any zoom; it was a hardcoded 64
  // world units, written in three places.
  float arm_length = editor::MAJOR_GRID_STEP * 0.5f;

  gizmo_handle_t hovered;
  gizmo_handle_t dragged;

  // Which plane handles are worth offering. A quad seen near edge-on is a
  // sliver: unreadable, awkward to hit, and it would steal picks from the
  // arrows behind it. Decided in set_target, where the view is already at hand,
  // so the hit test and the draw agree without either taking a camera.
  bool plane_handle_usable[3] = {true, true, true};

  // The frame the drag opened in. Every update measures against these, never
  // against the previous frame.
  shared::aabb_t              start_box;
  std::optional<linalg::vec3> start_snap_origin;

  // Where the ray met the drag plane at the press. A point rather than a
  // distance along one axis, because a plane drag has two axes and no single
  // scalar describes it; the axis drags project it back out.
  linalg::vec3 start_hit = {0, 0, 0};

  // Rotation is accumulated rather than differenced against a start angle: a
  // start angle comes from atan2, so a drag crossing the +/-pi seam reads as a
  // full turn the other way. Wrapping each frame's increment and summing makes
  // the seam a non-event and lets a drag exceed a half turn.
  float previous_angle = 0.f;
  float total_angle    = 0.f;
};

} // namespace client
