#pragma once

#include "../../shared/collision_detection.hpp"
#include "../../shared/color.hpp"
#include "../../shared/editor_grid.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/map.hpp" // For map_t
#include "../camera.hpp"        // For camera_t
#include "../input.hpp"
#include <vulkan/vulkan.h>

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
  float time;
  const Bounding_Volume_Hierarchy *bvh = nullptr;

  // dirty Flag to signal that geometry has been modified and BVH needs rebuild
  bool *geometry_updated_so_bvh_rebuild_is_needed = nullptr;

  class Transaction_System *transaction_system = nullptr;

  editor::grid_settings_t *grid = nullptr;
};

// Interface for drawing editor overlays
struct overlay_renderer_t
{
  virtual ~overlay_renderer_t() = default;

  virtual VkCommandBuffer get_command_buffer() = 0;

  virtual void draw_line(const linalg::vec3 &start, const linalg::vec3 &end,
                         color_t color) = 0;
  virtual void draw_wire_box(const linalg::vec3 &center,
                             const linalg::vec3 &half_extents,
                             color_t color) = 0;
  virtual void draw_solid_box(const linalg::vec3 &center,
                              const linalg::vec3 &half_extents,
                              color_t color) = 0;
  virtual void draw_circle(const linalg::vec3 &center, float radius,
                           const linalg::vec3 &normal, color_t color) = 0;
  virtual void draw_text(const linalg::vec3 &pos, const char *text,
                         color_t color) = 0;
};

} // namespace client
