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
#include <string>

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

class Transaction_System;

// Forward declaration of the editor state or game state if needed
struct editor_context_t
{
  // A REFERENCE, so a tool cannot be handed a context without one and no call
  // site has to ask. As a pointer this was checked four different ways across
  // the tools -- an assert, an if-guard, a silent early return and an unchecked
  // deref -- and the silent arm was the dangerous one: the edit had already
  // landed in the map by then, so a null pointer meant the map changed and the
  // undo entry was dropped without a word.
  Transaction_System &transaction_system;

  explicit editor_context_t(Transaction_System &transactions)
      : transaction_system(transactions)
  {
  }

  shared::map_t *map = nullptr;

  // Where `map` lives on disk, so a tool that writes a SIDECAR beside it does not
  // have to rediscover the maps directory. Empty until the map has a home, which
  // is what a bake button tests before it writes anything.
  std::string map_path;
  // Seconds since the editor opened, advanced by Tool_Editor_State::update. The
  // clock every animated overlay reads; initialised here because a tool that
  // pulses on garbage is a tool that flickers.
  float time = 0.0f;
  const Bounding_Volume_Hierarchy *bvh = nullptr;

  // The objects get_collision_pieces refused, filled by the same pass that built
  // `bvh`. A tool showing an object's properties says so; the viewport draws
  // their contour red. See build_editor_bvh.
  Span<const shared::entity_uid_t> objects_without_collision;

  // dirty Flag to signal that geometry has been modified and BVH needs rebuild
  bool *geometry_updated_so_bvh_rebuild_is_needed = nullptr;

  // The same shape, for the bake. It cannot be derived from lightmap_t's
  // geometry_id: that id covers the charts and deliberately not the pixels, so a
  // rebake at unchanged settings -- which is every iteration on the lighting --
  // leaves it identical while every texel has moved.
  bool *lightmap_updated_so_atlas_upload_is_needed = nullptr;

  editor::grid_settings_t *grid = nullptr;

  bool object_collides(shared::entity_uid_t uid) const
  {
    for (shared::entity_uid_t without : objects_without_collision)
      if (without == uid)
        return false;
    return true;
  }
};

// Where a placement gesture would put something: the surface point under the
// cursor, snapped to the grid. The picking BVH answers first and the Y=0 plane
// is the fallback -- only the fallback's X/Z are snapped, its Y being zero
// already. Empty means the ray missed both, which is a cursor with nowhere to
// place.
[[nodiscard]] inline std::optional<linalg::vec3>
try_pick_placement_point(const editor_context_t& ctx, const viewport_state_t &view)
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
