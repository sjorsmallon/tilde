#include "editor_gizmo.hpp"
#include "../editor/transaction_system.hpp"
#include "../renderer.hpp"
#include "../shared/entities/player_entity.hpp"
#include "../shared/entities/static_entities.hpp"
#include "../shared/entities/weapon_entity.hpp"
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

    // Debug: Draw AABBs along the ring
    if (i % 8 == 0)
    {
      float s = radius * 0.05f;
      renderer::DrawAABB(cmd, p1 - vec3{s, s, s}, p1 + vec3{s, s, s}, color);
    }
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

void Editor_Gizmo::start_interaction(Transaction_System *sys,
                                     shared::map_t *map,
                                     shared::entity_uid_t uid)
{
  if (!map || uid == 0)
    return;

  auto *entry = map->find_by_uid(uid);
  if (!entry || !entry->entity)
    return;

  target_map = map;
  target_uid = uid;
  transaction_system = sys;

  // Start Edit_Recorder — snapshot entity before modification
  active_edit.emplace(*target_map);
  active_edit->track(target_uid);

  // Store original for drag calculations
  auto &ent = entry->entity;

  if (auto *aabb = dynamic_cast<::network::AABB_Entity *>(ent.get()))
  {
    original_transform.position = aabb->position;
    original_transform.scale =
        aabb->half_extents; // Store half-extents as scale
    original_transform.orientation = {0, 0, 0,
                                      1}; // Identity (AABB has no rotation)
  }
  else if (auto *wedge = dynamic_cast<::network::Wedge_Entity *>(ent.get()))
  {
    original_transform.position = wedge->position;
    original_transform.scale = wedge->half_extents;
    original_transform.orientation = {0, 0, 0, 1}; // TODO: Wedge rotation
  }
  else if (auto *mesh =
               dynamic_cast<::network::Static_Mesh_Entity *>(ent.get()))
  {
    original_transform.position = mesh->position;
    original_transform.scale = mesh->render.scale;
  }
  else if (auto *player = dynamic_cast<::network::Player_Entity *>(ent.get()))
  {
    original_transform.position = player->position;
    original_transform.scale = {1, 1, 1};
    original_transform.orientation = {0, 0, 0, 1};
  }

  // Initialize Transform Gizmo State
  transform_state.position = original_transform.position;
  transform_state.rotation = ent->orientation;
  original_transform.orientation = {ent->orientation.x, ent->orientation.y,
                                    ent->orientation.z, 0};
  transform_state.size = 2.0f;
}

void Editor_Gizmo::end_interaction()
{
  if (active_edit)
  {
    active_edit->finish(target_uid);
    if (transaction_system)
    {
      if (auto txn = active_edit->take())
        transaction_system->push(*txn);
    }
    active_edit.reset();
  }

  target_map = nullptr;
  target_uid = 0;
  transaction_system = nullptr;
}

bool Editor_Gizmo::is_interacting() const
{
  return active_edit.has_value();
}

bool Editor_Gizmo::is_hovered() const
{
  return reshape_state.hovered_handle_index != -1 ||
         transform_state.hovered_axis_index != -1 ||
         transform_state.hovered_ring_index != -1;
}

void Editor_Gizmo::update(const linalg::ray_t &ray, bool is_mouse_down)
{
  if (is_interacting())
    return; // Don't update hover if already dragging

  // Update hover state
  if (current_mode == Gizmo_Mode::Reshape)
  {
    hit_test_reshape_gizmo(ray, reshape_state);
  }
  // For Unified, we might want both, but let's prioritize Transform for now or
  // separate tools?
  // The user asked for "rings to be drawn".
  // Let's Always hit test Transform if not reshaping?
  // Or if mode is Unified/Translate/Rotate.
  // Let's assume we want to show Transform gizmo by default if Reshape isn't
  // specifically active? Or maybe we just check both?
  // If we check both, who wins?
  // Usually Transform gizmo handles (arrows) are outside the object, Reshape
  // handles are on faces. Let's check Transform first.

  bool hit_transform = hit_test_transform_gizmo(ray, transform_state);
  if (!hit_transform &&
      (current_mode == Gizmo_Mode::Reshape || current_mode == Gizmo_Mode::Unified))
  {
    hit_test_reshape_gizmo(ray, reshape_state);
  }
  else
  {
    reshape_state.hovered_handle_index = -1;
  }
}

void Editor_Gizmo::draw(VkCommandBuffer cmd)
{
  if (current_mode == Gizmo_Mode::Reshape ||
      current_mode == Gizmo_Mode::Unified)
    draw_reshape_gizmo(cmd, reshape_state);

  // Always draw transform gizmo if it exists/initialized?
  // Or only if mode allows.
  // User wants to see rings.
  draw_transform_gizmo(cmd, transform_state);
}

void Editor_Gizmo::handle_input(const linalg::ray_t &ray, bool is_mouse_down,
                                const linalg::vec3 &cam_pos)
{
  if (!target_map || target_uid == 0)
    return;

  if (reshape_state.dragging_handle_index != -1)
  {
    if (!is_mouse_down)
    {
      reshape_state.dragging_handle_index = -1;
      end_interaction();
      return;
    }
    // Continue Dragging Reshape
    // ... (Existing Reshape Logic) ...
    // Refactoring: The existing logic was modifying map directly.
    // I will keep it mostly as is but fix variable names.

    int i = reshape_state.dragging_handle_index;
    int axis = i / 2;
    vec3 face_normals[6] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                            {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};

    vec3 axis_dir = face_normals[i];
    vec3 orig_center = original_transform.position;
    vec3 orig_half = original_transform.scale;
    vec3 handle_pos = orig_center + axis_dir * orig_half[axis];

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

      vec3 old_min = orig_center - orig_half;
      vec3 old_max = orig_center + orig_half;
      float new_min_val = old_min[axis];
      float new_max_val = old_max[axis];

      if (i % 2 == 0) // + Face
        new_max_val += delta;
      else // - Face
        new_min_val += (axis_dir[axis] * delta);

      // Grid Snap (optional, but good for editor)
      new_max_val = std::round(new_max_val * 2.0f) * 0.5f;
      new_min_val = std::round(new_min_val * 2.0f) * 0.5f;

      if (new_max_val < new_min_val + 0.1f)
      {
        if (i % 2 == 0)
          new_max_val = new_min_val + 0.1f;
        else
          new_min_val = new_max_val - 0.1f;
      }

      float new_center_val = (new_min_val + new_max_val) * 0.5f;
      float new_half_val = (new_max_val - new_min_val) * 0.5f;

      auto *_entry = target_map->find_by_uid(target_uid);
      if (!_entry) return;
      auto &ent = _entry->entity;
      if (auto *aabb = dynamic_cast<::network::AABB_Entity *>(ent.get()))
      {
        if (axis == 0)
        {
          aabb->position.x = new_center_val;
          aabb->half_extents.x = new_half_val;
        }
        else if (axis == 1)
        {
          aabb->position.y = new_center_val;
          aabb->half_extents.y = new_half_val;
        }
        else
        {
          aabb->position.z = new_center_val;
          aabb->half_extents.z = new_half_val;
        }

        reshape_state.aabb.center = aabb->position;
        reshape_state.aabb.half_extents = aabb->half_extents;

        // Also update transform gizmo
        transform_state.position = aabb->position;
      }
    }
  }
  else if (transform_state.dragging_axis_index != -1 ||
           transform_state.dragging_ring_index != -1)
  {
    if (!is_mouse_down)
    {
      transform_state.dragging_axis_index = -1;
      transform_state.dragging_ring_index = -1;
      end_interaction();
      return;
    }

    // Handle Transform Drag
    if (transform_state.dragging_axis_index != -1)
    {
      // Axis Drag (Translation)
      int axis = transform_state.dragging_axis_index;
      vec3 axis_dir = {0, 0, 0};
      if (axis == 0)
        axis_dir = {1, 0, 0};
      else if (axis == 1)
        axis_dir = {0, 1, 0};
      else
        axis_dir = {0, 0, 1};

      vec3 orig_pos = original_transform.position;
      vec3 cam_to_obj = orig_pos - cam_pos;
      vec3 plane_normal = linalg::cross(axis_dir, cam_to_obj);
      plane_normal = linalg::cross(plane_normal, axis_dir);

      float t = 0;
      if (linalg::intersect_ray_plane(ray.origin, ray.dir, orig_pos,
                                      plane_normal, t))
      {
        vec3 hit = ray.origin + ray.dir * t;
        float current_proj = linalg::dot(hit, axis_dir);
        float delta = current_proj - drag_start_offset;

        // Apply Delta
        // Snap?
        // float delta_snapped = std::round(delta * 2.0f) * 0.5f;

        vec3 new_pos = orig_pos + axis_dir * delta;
        new_pos.x = std::round(new_pos.x * 2.0f) * 0.5f;
        new_pos.y = std::round(new_pos.y * 2.0f) * 0.5f;
        new_pos.z = std::round(new_pos.z * 2.0f) * 0.5f;

        auto *te = target_map->find_by_uid(target_uid);
        if (!te) return;
        te->entity->position = new_pos;
        reshape_state.aabb.center = new_pos;
        transform_state.position = new_pos;
      }
    }
    else if (transform_state.dragging_ring_index != -1)
    {
      // Ring Drag (Rotation)
      // For now, simple rotation visual or basic entity rotation (if supported)
      // AABB doesn't support rotation.
      // Wedge supports 90 degree steps (0-3).

      int axis = transform_state.dragging_ring_index;
      vec3 axis_dir = {0, 0, 0};
      if (axis == 0)
        axis_dir = {1, 0, 0};
      else if (axis == 1)
        axis_dir = {0, 1, 0};
      else
        axis_dir = {0, 0, 1};

      // Project ray onto plane perpendicular to axis
      float t = 0;
      if (linalg::intersect_ray_plane(ray.origin, ray.dir,
                                      transform_state.position, axis_dir, t))
      {
        vec3 hit = ray.origin + ray.dir * t;
        vec3 local = hit - transform_state.position;
        // Calculate angle?
        // We need start angle.
        // Let's use `drag_start_offset` as "start angle" (radians).

        // But `drag_start_offset` was float.
        // Re-calculate current angle
        // We need a basis on the plane.
        vec3 u, v;
        if (axis == 0)
        {
          u = {0, 1, 0};
          v = {0, 0, 1};
        }
        else if (axis == 1)
        {
          u = {0, 0, 1};
          v = {1, 0, 0};
        } // Z, X
        else
        {
          u = {1, 0, 0};
          v = {0, 1, 0};
        }

        float x = linalg::dot(local, u);
        float y = linalg::dot(local, v);
        float angle = std::atan2(y, x);

        float delta_angle = angle - drag_start_offset;

        // For Wedge: snap to 90 degrees
        // original orientation + delta?
        // Wedge orientation is 0,1,2,3 -> 0, 90, 180, 270 around Y axis.
        // So primarily axis 1 (Y).

        // Apply to any entity's base orientation (euler degrees)
        auto *re = target_map->find_by_uid(target_uid);
        if (!re) return;
        auto &ent = re->entity;
        float delta_degrees = delta_angle * (180.0f / 3.14159f);
        // Snap to 15-degree increments
        delta_degrees = std::round(delta_degrees / 15.0f) * 15.0f;

        vec3 new_orient = {original_transform.orientation.x,
                           original_transform.orientation.y,
                           original_transform.orientation.z};
        new_orient[axis] = new_orient[axis] + delta_degrees;
        ent->orientation = new_orient;
        transform_state.rotation = new_orient;
      }
    }
  }
  else
  {
    // New Drag Start?
    if (is_mouse_down)
    {
      if (transform_state.hovered_axis_index != -1)
      {
        transform_state.dragging_axis_index =
            transform_state.hovered_axis_index;
        // Init Drag
        int axis = transform_state.dragging_axis_index;
        vec3 axis_dir = {0, 0, 0};
        if (axis == 0)
          axis_dir = {1, 0, 0};
        else if (axis == 1)
          axis_dir = {0, 1, 0};
        else
          axis_dir = {0, 0, 1};

        vec3 center = transform_state.position;
        vec3 cam_to_obj = center - cam_pos;
        vec3 plane_normal = linalg::cross(axis_dir, cam_to_obj);
        plane_normal = linalg::cross(plane_normal, axis_dir);

        float t = 0;
        if (linalg::intersect_ray_plane(ray.origin, ray.dir, center,
                                        plane_normal, t))
        {
          vec3 hit = ray.origin + ray.dir * t;
          drag_start_offset = linalg::dot(hit, axis_dir);
        }
      }
      else if (transform_state.hovered_ring_index != -1)
      {
        transform_state.dragging_ring_index =
            transform_state.hovered_ring_index;

        // Init Rotate Drag
        int axis = transform_state.dragging_ring_index;
        vec3 axis_dir = {0, 0, 0};
        if (axis == 0)
          axis_dir = {1, 0, 0};
        else if (axis == 1)
          axis_dir = {0, 1, 0};
        else
          axis_dir = {0, 0, 1};

        float t = 0;
        if (linalg::intersect_ray_plane(ray.origin, ray.dir,
                                        transform_state.position, axis_dir, t))
        {
          vec3 hit = ray.origin + ray.dir * t;
          vec3 local = hit - transform_state.position;
          vec3 u, v;
          if (axis == 0)
          {
            u = {0, 1, 0};
            v = {0, 0, 1};
          }
          else if (axis == 1)
          {
            u = {0, 0, 1};
            v = {1, 0, 0};
          }
          else
          {
            u = {1, 0, 0};
            v = {0, 1, 0};
          }

          float x = linalg::dot(local, u);
          float y = linalg::dot(local, v);
          drag_start_offset = std::atan2(y, x); // Store start angle
        }
      }
      else if (reshape_state.hovered_handle_index != -1)
      {
        reshape_state.dragging_handle_index =
            reshape_state.hovered_handle_index;
        // ... duplicate init logic from before or call function?
        // Since we inline here, copy paste logic:

        int i = reshape_state.dragging_handle_index;
        int axis = i / 2;
        vec3 face_normals[6] = {{1, 0, 0},  {-1, 0, 0}, {0, 1, 0},
                                {0, -1, 0}, {0, 0, 1},  {0, 0, -1}};
        vec3 axis_dir = face_normals[i];
        vec3 center = original_transform.position;
        vec3 half = original_transform.scale;
        vec3 handles_pos = center + axis_dir * half[axis];
        vec3 cam_to_obj = center - cam_pos;
        vec3 plane_normal = linalg::cross(axis_dir, cam_to_obj);
        plane_normal = linalg::cross(plane_normal, axis_dir);
        float t = 0;
        if (linalg::intersect_ray_plane(ray.origin, ray.dir, handles_pos,
                                        plane_normal, t))
        {
          vec3 hit = ray.origin + ray.dir * t;
          drag_start_offset = linalg::dot(hit, axis_dir);
        }
      }
    }
  }
}

void Editor_Gizmo::set_geometry(const shared::aabb_bounds_t &bounds)
{
  reshape_state.aabb.center = (bounds.min + bounds.max) * 0.5f;
  reshape_state.aabb.half_extents = (bounds.max - bounds.min) * 0.5f;
  // Sync transform
  transform_state.position = reshape_state.aabb.center;
  // Size? default
  transform_state.size = 2.0f; // Reset size on new geometry? Or keep?
  // It's fine.
}

} // namespace client
