#include "editor_gizmo.hpp"
#include "../editor/transaction_system.hpp"
#include "../renderer.hpp"
#include "../shared/map.hpp" // Full definition needed
#include <algorithm>         // for min/max
#include <cmath>

namespace client
{

using namespace linalg;

// Helper to draw a ring (circle) in 3D
static void draw_ring(VkCommandBuffer cmd, const vec3 &center, float radius,
                      int axis, uint32_t color)
{
  const int segments = 64;
  const float step = (2.0f * 3.1415926535f) / float(segments);

  for (int i = 0; i < segments; ++i)
  {
    float theta1 = float(i) * step;
    float theta2 = float(i + 1) * step;

    vec3 p1, p2;

    // Axis 0: X (Ring in YZ plane)
    // Axis 1: Y (Ring in XZ plane)
    // Axis 2: Z (Ring in XY plane)

    if (axis == 0) // X-axis ring
    {
      p1 = {center.x, center.y + std::cos(theta1) * radius,
            center.z + std::sin(theta1) * radius};
      p2 = {center.x, center.y + std::cos(theta2) * radius,
            center.z + std::sin(theta2) * radius};
    }
    else if (axis == 1) // Y-axis ring
    {
      p1 = {center.x + std::cos(theta1) * radius, center.y,
            center.z + std::sin(theta1) * radius};
      p2 = {center.x + std::cos(theta2) * radius, center.y,
            center.z + std::sin(theta2) * radius};
    }
    else // Z-axis ring
    {
      p1 = {center.x + std::cos(theta1) * radius,
            center.y + std::sin(theta1) * radius, center.z};
      p2 = {center.x + std::cos(theta2) * radius,
            center.y + std::sin(theta2) * radius, center.z};
    }

    renderer::DrawLine(cmd, p1, p2, color);
  }
}

void draw_reshape_gizmo(VkCommandBuffer cmd, const reshape_gizmo_t &gizmo)
{
  // Note: We do NOT draw the AABB here, as the editor draws the selection
  // separately (wireframe/filled). We only draw the handles.

  // Draw handles on faces using Arrows
  float handle_length = 1.0f; // Matching previous variable
  vec3 c = gizmo.aabb.center;
  vec3 e = gizmo.aabb.half_extents;

  struct handle_def_t
  {
    vec3 origin;
    vec3 dir;
    int index;
  };

  handle_def_t handles[6] = {
      {{c.x + e.x, c.y, c.z}, {1, 0, 0}, 0},  // +X
      {{c.x - e.x, c.y, c.z}, {-1, 0, 0}, 1}, // -X
      {{c.x, c.y + e.y, c.z}, {0, 1, 0}, 2},  // +Y
      {{c.x, c.y - e.y, c.z}, {0, -1, 0}, 3}, // -Y
      {{c.x, c.y, c.z + e.z}, {0, 0, 1}, 4},  // +Z
      {{c.x, c.y, c.z - e.z}, {0, 0, -1}, 5}  // -Z
  };

  for (const auto &h : handles)
  {
    uint32_t col = 0xFFFFFFFF; // White
    if (gizmo.hovered_handle_index == h.index ||
        gizmo.dragging_handle_index == h.index)
    {
      col = 0xFF00FF00; // Green
    }

    vec3 end = h.origin + h.dir * handle_length;
    renderer::draw_arrow(cmd, h.origin, end, col);
  }
}

void draw_transform_gizmo(VkCommandBuffer cmd, const transform_gizmo_t &gizmo)
{
  vec3 p = gizmo.position;
  float s = gizmo.size;

  // Colors (ABGR format often used in this project? Or RGBA?)
  // renderer.cpp uses uint32_t color. EditorStateDraw uses 0xFF0000FF for Red
  // (ABGR? A=FF B=00 G=00 R=FF -> Red?) Actually, usually it's 0xAABBGGRR.
  // 0xFF0000FF -> A=FF, B=00, G=00, R=FF. Yes.
  // 0xFF00A5FF -> Orange/Gold?

  // Standard Axis Colors: X=Red, Y=Green, Z=Blue
  uint32_t col_x = 0xFF0000FF;
  uint32_t col_y = 0xFF00FF00;
  uint32_t col_z = 0xFFFF0000;
  uint32_t col_sel = 0xFFFFFFFF; // White for selection

  // Draw Arrows
  // X Axis
  renderer::draw_arrow(cmd, p, p + vec3{s, 0, 0},
                       (gizmo.hovered_axis_index == 0) ? col_sel : col_x);
  // Y Axis
  renderer::draw_arrow(cmd, p, p + vec3{0, s, 0},
                       (gizmo.hovered_axis_index == 1) ? col_sel : col_y);
  // Z Axis
  renderer::draw_arrow(cmd, p, p + vec3{0, 0, s},
                       (gizmo.hovered_axis_index == 2) ? col_sel : col_z);

  // Draw Rings
  // Rings usually are at the center, radius proportional to size?
  // Let's make rings slightly larger than arrows or same size?
  // Usually rings are around the origin.

  float r_radius = s * 0.8f;

  draw_ring(cmd, p, r_radius, 0,
            (gizmo.hovered_ring_index == 0) ? col_sel : col_x);
  draw_ring(cmd, p, r_radius, 1,
            (gizmo.hovered_ring_index == 1) ? col_sel : col_y);
  draw_ring(cmd, p, r_radius, 2,
            (gizmo.hovered_ring_index == 2) ? col_sel : col_z);
}

// --- Logic ---

static float intersect_aabb(const vec3 &origin, const vec3 &dir,
                            const vec3 &min, const vec3 &max)
{
  float t = 0;
  if (linalg::intersect_ray_aabb(origin, dir, min, max, t))
    return t;
  return 1e9f;
}

bool hit_test_reshape_gizmo(const linalg::ray_t &ray, reshape_gizmo_t &gizmo)
{
  gizmo.hovered_handle_index = -1;
  float min_t = 1e9f;
  float handle_length = 1.0f;

  vec3 c = gizmo.aabb.center;
  vec3 e = gizmo.aabb.half_extents;

  // Normals for indices 0..5
  vec3 normals[6] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                     {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};

  // Position offsets for indices 0..5
  vec3 pos[6] = {{c.x + e.x, c.y, c.z}, {c.x - e.x, c.y, c.z},
                 {c.x, c.y + e.y, c.z}, {c.x, c.y - e.y, c.z},
                 {c.x, c.y, c.z + e.z}, {c.x, c.y, c.z - e.z}};

  for (int i = 0; i < 6; ++i)
  {
    vec3 p = pos[i];
    vec3 n = normals[i];
    vec3 end = p + n * handle_length;

    // Approximate arrow with AABB for picking
    // Standard logic from previous editor_state_update
    vec3 bmin = {.x = std::min(p.x, end.x),
                 .y = std::min(p.y, end.y),
                 .z = std::min(p.z, end.z)};
    vec3 bmax = {.x = std::max(p.x, end.x),
                 .y = std::max(p.y, end.y),
                 .z = std::max(p.z, end.z)};
    float pad = 0.2f;
    bmin = bmin - vec3{.x = pad, .y = pad, .z = pad};
    bmax = bmax + vec3{.x = pad, .y = pad, .z = pad};

    float t = intersect_aabb(ray.origin, ray.dir, bmin, bmax);
    if (t < min_t)
    {
      min_t = t;
      gizmo.hovered_handle_index = i;
    }
  }

  return gizmo.hovered_handle_index != -1;
}

bool hit_test_transform_gizmo(const linalg::ray_t &ray,
                              transform_gizmo_t &gizmo)
{
  gizmo.hovered_axis_index = -1;
  gizmo.hovered_ring_index = -1;
  float min_t = 1e9f;

  vec3 p = gizmo.position;
  float s = gizmo.size;

  // Axis Picking (Arrows)
  // X (0), Y (1), Z (2)
  vec3 axes[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

  for (int i = 0; i < 3; ++i)
  {
    vec3 end = p + axes[i] * s;
    // Approximating arrow as AABB
    vec3 bmin = {.x = std::min(p.x, end.x),
                 .y = std::min(p.y, end.y),
                 .z = std::min(p.z, end.z)};
    vec3 bmax = {.x = std::max(p.x, end.x),
                 .y = std::max(p.y, end.y),
                 .z = std::max(p.z, end.z)};
    float pad = s * 0.1f;
    bmin = bmin - vec3{pad, pad, pad};
    bmax = bmax + vec3{pad, pad, pad};

    float t = intersect_aabb(ray.origin, ray.dir, bmin, bmax);
    if (t < min_t)
    {
      min_t = t;
      gizmo.hovered_axis_index = i;
    }
  }

  // Ring Picking (Rotation)
  // Approximate ring as a torus or plane intersection?
  // User asked for translate/rotate.
  // We can do a plane intersection for the ring plane, check distance to
  // center. Radius = s * 0.8f. Thickness = something small.
  float r_radius = s * 0.8f;
  float thickness = s * 0.1f;

  // Check if we hit closer than current min_t
  for (int i = 0; i < 3; ++i)
  {
    vec3 normal = axes[i]; // Ring i lies in plane perpendicular to axis i?
    // Convention:
    // X Ring (Pitch) -> Plane YZ -> Normal X
    // Y Ring (Yaw) -> Plane XZ -> Normal Y
    // Z Ring (Roll) -> Plane XY -> Normal Z

    float t = 0;
    if (linalg::intersect_ray_plane(ray.origin, ray.dir, p, normal, t))
    {
      if (t > 0 && t < min_t)
      {
        vec3 hit = ray.origin + ray.dir * t;
        float d = linalg::length(hit - p);
        if (d >= r_radius - thickness && d <= r_radius + thickness)
        {
          min_t = t;
          gizmo.hovered_ring_index = i;
          gizmo.hovered_axis_index = -1; // Ring overrides Axis if closer
        }
      }
    }
  }

  return (gizmo.hovered_axis_index != -1 || gizmo.hovered_ring_index != -1);
}

bool update_reshape_gizmo(reshape_gizmo_t &gizmo, const linalg::ray_t &ray,
                          bool is_dragging)
{
  // Just update hovered state if not dragging
  if (!is_dragging)
  {
    hit_test_reshape_gizmo(ray, gizmo);
    gizmo.dragging_handle_index = -1;
    return false;
  }

  // If dragging...
  if (gizmo.dragging_handle_index == -1)
  {
    // Start Drag
    if (gizmo.hovered_handle_index != -1)
    {
      gizmo.dragging_handle_index = gizmo.hovered_handle_index;
    }
    else
    {
      return false; // Not dragging anything
    }
  }

  // Calculate drag delta
  // This requires state (start point, original aabb).
  // The function signature might be insufficient for full stateless update
  // without passing in the drag start/original context.
  // For now, let's assume the caller handles the dragging logic using the
  // indices, or we expand this function. The prompt asked for "refactoring to
  // gizmos", pushing logic here is good. But we need the context.

  // Let's rely on the caller for the actual modification for now, keeping this
  // simple or pass in the necessary context.

  // Actually, to fully refactor, we should move the math here.
  // But `update_reshape_gizmo` needs `drag_start_point` and `original_aabb`.
  // Maybe we keep `dragging_handle_index` in the gizmo struct, but the
  // interaction state (start point) needs to be managed.

  // The user wants "simplest solutions".
  // Keeping the math in `editor_state_update` but using the struct for
  // state/drawing is a good step. Or we can add a `drag_context` struct.

  return false;
}

// Editor_Gizmo Implementation

Editor_Gizmo::~Editor_Gizmo() = default;

void Editor_Gizmo::start_interaction(Transaction_System *sys,
                                     shared::map_t *map, int geo_index)
{
  if (!map || geo_index < 0 || geo_index >= (int)map->static_geometry.size())
    return;

  target_map = map;
  target_index = geo_index;

  // Start RAII Transaction
  // This snapshots the CURRENT state of the geometry as "Before"
  active_transaction =
      std::make_unique<Editor_Transaction>(sys, target_map, target_index);

  // Store original for drag calculations
  original_geometry = target_map->static_geometry[target_index];
}

void Editor_Gizmo::end_interaction()
{
  // Destroying the transaction object triggers the commit process:
  // 1. Calculate "After" snapshot (current state of map)
  // 2. Diff "Before" vs "After"
  // 3. Push transaction
  active_transaction.reset();

  target_map = nullptr;
  target_index = -1;
}

bool Editor_Gizmo::is_interacting() const
{
  return active_transaction != nullptr;
}

void Editor_Gizmo::update(const linalg::ray_t &ray, bool is_mouse_down)
{
  if (is_interacting())
    return; // Don't update hover if already dragging

  // Update hover state
  hit_test_reshape_gizmo(ray, state);
}

void Editor_Gizmo::draw(VkCommandBuffer cmd) { draw_reshape_gizmo(cmd, state); }

void Editor_Gizmo::handle_input(const linalg::ray_t &ray, bool is_mouse_down,
                                const linalg::vec3 &cam_pos)
{
  if (!target_map || target_index == -1)
    return;

  if (state.dragging_handle_index == -1)
  {
    if (is_mouse_down && state.hovered_handle_index != -1)
    {
      state.dragging_handle_index = state.hovered_handle_index;

      // Calculate Drag Start Offset
      int i = state.dragging_handle_index;
      int axis = i / 2;
      vec3 face_normals[6] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                              {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
      vec3 axis_dir = face_normals[i];

      shared::aabb_bounds_t bounds = shared::get_bounds(original_geometry);
      vec3 center = (bounds.min + bounds.max) * 0.5f;
      vec3 half = (bounds.max - bounds.min) * 0.5f;
      float half_vals[3] = {half.x, half.y, half.z};

      vec3 cam_to_obj = center - cam_pos;
      vec3 plane_normal = linalg::cross(axis_dir, cam_to_obj);
      plane_normal = linalg::cross(plane_normal, axis_dir);
      vec3 handle_pos = center + axis_dir * half_vals[axis];

      float t = 0;
      if (linalg::intersect_ray_plane(ray.origin, ray.dir, handle_pos,
                                      plane_normal, t))
      {
        vec3 hit = ray.origin + ray.dir * t;
        drag_start_offset = linalg::dot(hit, axis_dir);
      }
    }
  }
  else
  {
    if (!is_mouse_down)
    {
      // Stop Dragging
      state.dragging_handle_index = -1;
      // Interaction end should be handled by caller calling end_interaction?
      // Or we can call it here if we own the lifecycle?
      // Caller manages `Editor_Gizmo` lifecycle usually?
      // No, `Editor_Gizmo` is persistent, but `interaction` is transient.
      // Caller should call `end_interaction` when mouse up.
      // But `handle_input` is called every frame.
      // If !is_mouse_down, we are done.
      end_interaction();
    }
    else
    {
      // Continue Dragging
      int i = state.dragging_handle_index;
      int axis = i / 2;
      vec3 face_normals[6] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                              {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};

      vec3 axis_dir = face_normals[i];

      // Use original geometry for calculation
      shared::aabb_bounds_t orig_bounds = shared::get_bounds(original_geometry);
      vec3 orig_center = (orig_bounds.min + orig_bounds.max) * 0.5f;
      vec3 orig_half = (orig_bounds.max - orig_bounds.min) * 0.5f;
      float orig_half_vals[3] = {orig_half.x, orig_half.y, orig_half.z};
      vec3 handle_pos = orig_center + axis_dir * orig_half_vals[axis];

      vec3 cam_to_obj = orig_center - cam_pos;
      vec3 plane_normal = linalg::cross(axis_dir, cam_to_obj);
      plane_normal = linalg::cross(plane_normal, axis_dir);

      float t = 0;
      if (linalg::intersect_ray_plane(ray.origin, ray.dir, handle_pos,
                                      plane_normal, t))
      {
        vec3 hit = ray.origin + ray.dir * t;
        float current_proj = linalg::dot(hit, axis_dir);
        float delta = current_proj - drag_start_offset;

        // Calculate new min/max
        vec3 old_min = orig_bounds.min;
        vec3 old_max = orig_bounds.max;

        float new_min_val = old_min[axis];
        float new_max_val = old_max[axis];

        if (i % 2 == 0) // + Face
        {
          new_max_val += delta;
          new_max_val = std::round(new_max_val); // Snap?
        }
        else // - Face
        {
          new_min_val += (axis_dir[axis] * delta);
          new_min_val = std::round(new_min_val);
        }

        if (new_max_val < new_min_val + 0.1f)
        {
          if (i % 2 == 0)
            new_max_val = new_min_val + 0.1f;
          else
            new_min_val = new_max_val - 0.1f;
        }

        float new_center_val = (new_min_val + new_max_val) * 0.5f;
        float new_half_val = (new_max_val - new_min_val) * 0.5f;

        // Update Map Geometry
        auto &geo = target_map->static_geometry[target_index];
        std::visit(
            [&](auto &&arg)
            {
              using T = std::decay_t<decltype(arg)>;
              if constexpr (std::is_same_v<T, shared::aabb_t> ||
                            std::is_same_v<T, shared::wedge_t>)
              {
                if (axis == 0)
                {
                  arg.center.x = new_center_val;
                  arg.half_extents.x = new_half_val;
                }
                else if (axis == 1)
                {
                  arg.center.y = new_center_val;
                  arg.half_extents.y = new_half_val;
                }
                else if (axis == 2)
                {
                  arg.center.z = new_center_val;
                  arg.half_extents.z = new_half_val;
                }
              }
            },
            geo.data);

        // Update internal state visuals
        state.aabb.center =
            (shared::get_bounds(geo).min + shared::get_bounds(geo).max) * 0.5f;
        state.aabb.half_extents =
            (shared::get_bounds(geo).max - shared::get_bounds(geo).min) * 0.5f;
      }
    }
  }
}

void Editor_Gizmo::set_geometry(const shared::aabb_bounds_t &bounds)
{
  state.aabb.center = (bounds.min + bounds.max) * 0.5f;
  state.aabb.half_extents = (bounds.max - bounds.min) * 0.5f;
}

} // namespace client
