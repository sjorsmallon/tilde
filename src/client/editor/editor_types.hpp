#pragma once

#include "../../shared/collision_detection.hpp"
#include "../../shared/color.hpp"
#include "../../shared/editor_grid.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/map.hpp" // For map_t
#include "../camera.hpp"        // For camera_t
#include "../frame_builder.hpp"
#include "../input.hpp"

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
