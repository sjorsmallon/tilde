#include "editor_gizmo.hpp"
#include "../renderer.hpp"
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
  // Draw the AABB wireframe
  uint32_t aabb_color = 0xFFFFFFFF; // White
  vec3 min = gizmo.aabb.center - gizmo.aabb.half_extents;
  vec3 max = gizmo.aabb.center + gizmo.aabb.half_extents;
  renderer::DrawAABB(cmd, min, max, aabb_color);

  // Draw handles on faces
  // 6 faces: +X, -X, +Y, -Y, +Z, -Z
  // Handle size
  float hs = 0.2f; // handle size
  vec3 h_ext = {hs, hs, hs};

  struct handle_def_t
  {
    vec3 pos;
    int index;
  };

  vec3 c = gizmo.aabb.center;
  vec3 e = gizmo.aabb.half_extents;

  handle_def_t handles[6] = {
      {{c.x + e.x, c.y, c.z}, 0}, // +X
      {{c.x - e.x, c.y, c.z}, 1}, // -X
      {{c.x, c.y + e.y, c.z}, 2}, // +Y
      {{c.x, c.y - e.y, c.z}, 3}, // -Y
      {{c.x, c.y, c.z + e.z}, 4}, // +Z
      {{c.x, c.y, c.z - e.z}, 5}  // -Z
  };

  for (const auto &h : handles)
  {
    uint32_t col = 0xFFCCCCCC; // Light Grey
    if (gizmo.hovered_handle_index == h.index ||
        gizmo.dragging_handle_index == h.index)
    {
      col = 0xFF00FF00; // Green
    }

    renderer::DrawAABB(cmd, h.pos - h_ext, h.pos + h_ext, col);
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

} // namespace client
