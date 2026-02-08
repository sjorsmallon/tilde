#pragma once

#include "../shared/map.hpp"
#include "../shared/shapes.hpp"
#include "linalg.hpp"
#include <vulkan/vulkan.h> // For VkCommandBuffer

namespace client
{

struct reshape_gizmo_t
{
  shared::aabb_t aabb = {};

  // -1: None
  // 0: +X face
  // 1: -X face
  // 2: +Y face
  // 3: -Y face
  // 4: +Z face
  // 5: -Z face
  int hovered_handle_index = -1;
  int dragging_handle_index = -1;
};

struct transform_gizmo_t
{
  linalg::vec3 position = {{0, 0, 0}};
  linalg::vec3 rotation = {{0, 0, 0}}; // Euler angles in degrees
  float size = 1.0f;

  // -1: None
  // 0: X Arrow
  // 1: Y Arrow
  // 2: Z Arrow
  // -1: None
  // 0: X Arrow
  // 1: Y Arrow
  // 2: Z Arrow
  int hovered_axis_index = -1;
  int dragging_axis_index = -1;

  // -1: None
  // 0: X Ring (Pitch)
  // 1: Y Ring (Yaw)
  // 2: Z Ring (Roll)
  int hovered_ring_index = -1;
  int dragging_ring_index = -1;
};

void draw_reshape_gizmo(VkCommandBuffer cmd, const reshape_gizmo_t &gizmo);
void draw_transform_gizmo(VkCommandBuffer cmd, const transform_gizmo_t &gizmo);

// Hit Testing
bool hit_test_reshape_gizmo(const linalg::ray_t &ray, reshape_gizmo_t &gizmo);
bool hit_test_transform_gizmo(const linalg::ray_t &ray,
                              transform_gizmo_t &gizmo);

// Logic
// Returns true if modified
bool update_reshape_gizmo(reshape_gizmo_t &gizmo, const linalg::ray_t &ray,
                          bool is_dragging);

} // namespace client

// Forward validations
namespace shared
{
struct map_t;
struct aabb_bounds_t;
} // namespace shared

namespace client
{

class Transaction_System;

class Editor_Gizmo
{
public:
  Editor_Gizmo() = default;
  ~Editor_Gizmo(); // explicit destructor for unique_ptr PIMPL

  void start_interaction(Transaction_System *sys, shared::map_t *map,
                         int geo_index);
  void end_interaction();
  bool is_interacting() const;

  // Passthrough to underlying gizmo logic
  void update(const linalg::ray_t &ray, bool is_mouse_down);
  void draw(VkCommandBuffer cmd);

  // Manipulate the gizmo and the underlying object
  // output_modified: set to true if the object changed
  // valid_ray/ray: input ray
  void handle_input(const linalg::ray_t &ray, bool is_mouse_down,
                    const linalg::vec3 &cam_pos);

  // Access to internal state
  reshape_gizmo_t &get_state() { return state; }
  void set_geometry(const shared::aabb_bounds_t &bounds);

private:
  reshape_gizmo_t state;

  // Transaction State
  std::unique_ptr<class Editor_Transaction> active_transaction;

  shared::map_t *target_map = nullptr;
  int target_index = -1;

  // Dragging state helper
  linalg::vec3 drag_start_point;
  float drag_start_offset = 0.0f;
  shared::static_geometry_t original_geometry;
};

} // namespace client
