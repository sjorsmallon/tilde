#include "pathfinding_test_tool.hpp"
#include "../../../shared/collision_detection.hpp"
#include "../../../shared/navmesh.hpp"
#include "../../../shared/pathfinding.hpp"
#include "../../renderer.hpp"
#include "imgui.h"
#include <cmath>
#include <limits>

namespace client
{

bool Pathfinding_Test_Tool::pick_navmesh_point(const navmesh_t &nav,
                                              linalg::vec3 &out_hit) const
{
  const linalg::vec3 &orig = m_viewport.mouse_ray.origin;
  const linalg::vec3 &dir  = m_viewport.mouse_ray.dir;
  float best_t = std::numeric_limits<float>::infinity();
  bool hit = false;

  for (const auto &poly : nav.polygons)
  {
    const int N = (int)poly.verts.size();
    // Decompose convex polygon into triangle fan from verts[0].
    const linalg::vec3 v0 = nav.vertices[poly.verts[0]].pos;
    for (int i = 1; i + 1 < N; ++i)
    {
      const linalg::vec3 v1 = nav.vertices[poly.verts[i]].pos;
      const linalg::vec3 v2 = nav.vertices[poly.verts[i + 1]].pos;
      float t;
      if (ray_triangle(orig, dir, v0, v1, v2, t) && t < best_t)
      {
        best_t  = t;
        out_hit = orig + dir * best_t;
        hit     = true;
      }
    }
  }
  return hit;
}

void Pathfinding_Test_Tool::recompute_path(const navmesh_t &nav)
{
  if (m_start && m_end)
    m_path = find_path(nav, *m_start, *m_end);
  else
    m_path.clear();
}

// ---------------------------------------------------------------------------
// Lifecycle

void Pathfinding_Test_Tool::on_enable(editor_context_t &ctx)
{
  m_start.reset();
  m_end.reset();
  m_path.clear();
}

void Pathfinding_Test_Tool::on_disable(editor_context_t &ctx) {}

// ---------------------------------------------------------------------------
// Input

void Pathfinding_Test_Tool::on_update(editor_context_t &ctx,
                                     const viewport_state_t &view, float /*dt*/)
{
  m_viewport = view;
}

void Pathfinding_Test_Tool::on_mouse_down(editor_context_t &ctx,
                                         const mouse_event_t &e)
{
  if (e.button != input::MouseButton::Left)
    return;
  if (!ctx.map->navmesh.valid())
    return;

  linalg::vec3 hit;
  if (!pick_navmesh_point(ctx.map->navmesh, hit))
    return;

  if (e.mods.shift)
    m_end = hit;
  else
    m_start = hit;

  recompute_path(ctx.map->navmesh);
}

void Pathfinding_Test_Tool::on_mouse_drag(editor_context_t &ctx,
                                         const mouse_event_t &e) {}

void Pathfinding_Test_Tool::on_mouse_up(editor_context_t &ctx,
                                       const mouse_event_t &e) {}

void Pathfinding_Test_Tool::on_key_down(editor_context_t &ctx,
                                       const key_event_t &e)
{
  if (e.key == input::Key::R)
  {
    m_start.reset();
    m_end.reset();
    m_path.clear();
  }
}

// ---------------------------------------------------------------------------
// Visuals

void Pathfinding_Test_Tool::on_draw_overlay(editor_context_t &ctx,
                                           overlay_renderer_t &renderer)
{
  const navmesh_t &nav = ctx.map->navmesh;
  if (!nav.valid())
    return;

  constexpr float y_lift = 2.f;

  // Navmesh wireframe, colored by island ID.
  static constexpr color_t island_colors[] = {
    colors::cyan,
    colors::yellow,
    colors::green,
    colors::magenta,
  };
  for (const auto &poly : nav.polygons)
  {
    color_t color = island_colors[poly.island % 4];
    const int N = (int)poly.verts.size();
    for (int e = 0; e < N; ++e)
    {
      linalg::vec3 a = nav.vertices[poly.verts[e          ]].pos;
      linalg::vec3 b = nav.vertices[poly.verts[(e + 1) % N]].pos;
      a.y += y_lift;
      b.y += y_lift;
      renderer.draw_line(a, b, color);
    }
  }

  // Start marker — green circle + vertical spike.
  if (m_start)
  {
    constexpr color_t start_color = colors::green;
    linalg::vec3 base = *m_start + linalg::vec3{0, y_lift, 0};
    renderer.draw_circle(base, 16.f, {0, 1, 0}, start_color);
    renderer.draw_line(base, base + linalg::vec3{0, 48.f, 0}, start_color);
  }

  // End marker — red circle + vertical spike.
  if (m_end)
  {
    constexpr color_t end_color = colors::red;
    linalg::vec3 base = *m_end + linalg::vec3{0, y_lift, 0};
    renderer.draw_circle(base, 16.f, {0, 1, 0}, end_color);
    renderer.draw_line(base, base + linalg::vec3{0, 48.f, 0}, end_color);
  }

  // Path: yellow lines connecting waypoints, white boxes at each waypoint.
  if (!m_path.empty())
  {
    constexpr color_t path_color = colors::yellow;
    constexpr color_t node_color = colors::white;
    constexpr linalg::vec3 half{4, 4, 4};
    for (int i = 0; i < (int)m_path.size(); ++i)
    {
      linalg::vec3 wp = m_path[i];
      wp.y += y_lift;
      renderer.draw_wire_box(wp, half, node_color);
      if (i + 1 < (int)m_path.size())
      {
        linalg::vec3 next = m_path[i + 1];
        next.y += y_lift;
        renderer.draw_line(wp, next, path_color);
      }
    }
  }
}

void Pathfinding_Test_Tool::on_draw_ui(editor_context_t &ctx)
{
  ImGui::SetNextWindowSize({300, 0}, ImGuiCond_Once);
  if (!ImGui::Begin("Pathfinding Test"))
  {
    ImGui::End();
    return;
  }

  ImGui::TextDisabled("LMB = start  |  Shift+LMB = end  |  R = reset");
  ImGui::Separator();

  if (m_start)
    ImGui::Text("Start: (%.1f, %.1f, %.1f)", m_start->x, m_start->y, m_start->z);
  else
    ImGui::TextDisabled("Start: not set");

  if (m_end)
    ImGui::Text("End:   (%.1f, %.1f, %.1f)", m_end->x, m_end->y, m_end->z);
  else
    ImGui::TextDisabled("End:   not set");

  ImGui::Separator();

  if (!m_start || !m_end)
    ImGui::TextDisabled("Set both points to find a path.");
  else if (m_path.empty())
    ImGui::TextColored({1.f, 0.3f, 0.3f, 1.f}, "No path found.");
  else
    ImGui::Text("Path: %d waypoints", (int)m_path.size());

  ImGui::End();
}

} // namespace client
