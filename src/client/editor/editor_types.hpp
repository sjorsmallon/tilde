#pragma once

#include "../../shared/collision_detection.hpp"
#include "../../shared/color.hpp"
#include "../../shared/editor_grid.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/map.hpp" // For map_t
#include "../camera.hpp"        // For camera_t
#include "../frame_builder.hpp"
#include "../input.hpp"

#include <optional>

namespace client
{

using key_event_t = input::key_event_t;

struct viewport_state_t
{
  linalg::ray_t mouse_ray;

  client::camera_t camera;
  // New fields for projection
  linalg::vec2 display_size;
  float aspect_ratio;
  // FOV is not duplicated here — it belongs to `camera`, and a second copy is a
  // second thing that can disagree with the projection the frame was drawn with.
};

// World position to framebuffer pixels, for tools that draw screen-space handles
// or hit-test against them.
//
// try_ because a point BEHIND the camera has no screen position: the
// perspective divide flips its sign and projects it, mirrored, into the visible
// half of the screen. Tools that ignored that drew handles for the geometry
// behind them.
[[nodiscard]] inline std::optional<linalg::vec2>
try_project_to_screen(const viewport_state_t &view, const linalg::vec3 &world_position)
{
  const linalg::vec3 view_position =
      linalg::world_to_view(world_position, view.camera.position, view.camera.yaw,
                            view.camera.pitch);

  // View space looks down -Z, so anything at or behind the eye has z >= 0.
  // Orthographic has no divide and no such half.
  if (!view.camera.orthographic && view_position.z > -0.01f)
    return std::nullopt;

  return linalg::view_to_screen(view_position, view.display_size,
                                view.camera.orthographic, view.camera.ortho_height,
                                view.camera.fov_degrees);
}

// Forward declaration of the editor state or game state if needed
struct editor_context_t
{
  shared::map_t *map = nullptr;
  // Seconds since the editor opened, advanced by Tool_Editor_State::update. The
  // clock every animated overlay reads; initialised here because a tool that
  // pulses on garbage is a tool that flickers.
  float time = 0.0f;
  const Bounding_Volume_Hierarchy *bvh = nullptr;

  // dirty Flag to signal that geometry has been modified and BVH needs rebuild
  bool *geometry_updated_so_bvh_rebuild_is_needed = nullptr;

  class Transaction_System *transaction_system = nullptr;

  editor::grid_settings_t *grid = nullptr;
};

// Where a placement gesture would put something: the surface point under the
// cursor, snapped to the grid. The picking BVH answers first and the Y=0 plane
// is the fallback -- only the fallback's X/Z are snapped, its Y being zero
// already. Empty means the ray missed both, which is a cursor with nowhere to
// place.
[[nodiscard]] inline std::optional<linalg::vec3>
try_pick_placement_point(const editor_context_t &ctx, const viewport_state_t &view)
{
  const float step = ctx.grid ? ctx.grid->step() : editor::MAJOR_GRID_STEP;

  if (ctx.bvh && !ctx.bvh->nodes.empty())
  {
    ray_hit_result_t hit{};
    if (bvh_intersect_ray(*ctx.bvh, view.mouse_ray.origin, view.mouse_ray.direction, hit))
    {
      const linalg::vec3 point =
          view.mouse_ray.origin + view.mouse_ray.direction * hit.t;
      return linalg::vec3{editor::snap(point.x, step), editor::snap(point.y, step),
                          editor::snap(point.z, step)};
    }
  }

  const linalg::vec3 plane_point{0, 0, 0};
  const linalg::vec3 plane_normal{0, 1.0f, 0};
  float              t = 0.0f;
  if (!linalg::intersect_ray_plane(view.mouse_ray.origin, view.mouse_ray.direction,
                                   plane_point, plane_normal, t))
    return std::nullopt;

  linalg::vec3 point = view.mouse_ray.origin + view.mouse_ray.direction * t;
  point.x = editor::snap(point.x, step);
  point.z = editor::snap(point.z, step);
  return point;
}

// What an axis view centres on and how much of it to fit. `radius` is the
// half-extent to frame, so a tool looking at a 72-unit-tall player asks for ~36
// and gets a viewport filled by the model rather than by empty map.
//
// A sphere rather than an aabb_t on purpose: the framing is the same from every
// axis, so a view snap cannot change how big the subject looks depending on
// which way you came at it.
struct view_focus_t
{
  linalg::vec3f center = {0, 0, 0};
  float         radius = 512.0f;
};

} // namespace client
