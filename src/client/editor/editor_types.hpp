#pragma once

#include "../../shared/collision_detection.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/map.hpp" // For map_t
#include "../camera.hpp"        // For camera_t

namespace client
{

struct mouse_event_t
{
  // SDL Button constants or custom enum
  // 1: Left, 2: Middle, 3: Right
  int button;
  linalg::vec2i pos;
  linalg::vec2i delta;
  bool shift_down;
  bool ctrl_down;
  bool alt_down;
};

struct key_event_t
{
  int scancode; // SDL_Scancode
  bool shift_down;
  bool ctrl_down;
  bool alt_down;
  bool repeat;
};

struct viewport_state_t
{
  linalg::ray_t mouse_ray;

  client::camera_t camera;
  // New fields for projection
  linalg::vec2 display_size;
  float aspect_ratio;
  float fov;
};

// Forward declaration of the editor state or game state if needed
struct editor_context_t
{
  shared::map_t *map;
  // We might need to spawn ghosts/visuals, so maybe a scene root or similar?
  // For now, let's keep it simple.

  // Helper to get global time if needed
  float time;

  // BVH for picking
  const Bounding_Volume_Hierarchy *bvh = nullptr;

  // Flag to signal that geometry has been modified and BVH needs rebuild
  bool *geometry_updated = nullptr;
};

// Interface for drawing editor overlays
struct overlay_renderer_t
{
  virtual ~overlay_renderer_t() = default;

  virtual void draw_line(const linalg::vec3 &start, const linalg::vec3 &end,
                         uint32_t color) = 0;
  virtual void draw_wire_box(const linalg::vec3 &center,
                             const linalg::vec3 &half_extents,
                             uint32_t color) = 0;
  virtual void draw_solid_box(const linalg::vec3 &center,
                              const linalg::vec3 &half_extents,
                              uint32_t color) = 0;
  virtual void draw_circle(const linalg::vec3 &center, float radius,
                           const linalg::vec3 &normal, uint32_t color) = 0;
  virtual void draw_text(const linalg::vec3 &pos, const char *text,
                         uint32_t color) = 0;
};

} // namespace client
