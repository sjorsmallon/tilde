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
  const linalg::vec3& origin = viewport.mouse_ray.origin;
  const linalg::vec3& dir  = viewport.mouse_ray.direction;
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
      if (ray_triangle(origin, dir, v0, v1, v2, t) && t < best_t)
      {
        best_t  = t;
        out_hit = origin + dir * best_t;
        hit     = true;
      }
    }
  }
  return hit;
}

void Pathfinding_Test_Tool::recompute_path(const navmesh_t &nav)
{
  if (start && end)
    path = find_path(nav, *start, *end);
  else
    path.clear();
}

// ---------------------------------------------------------------------------
// Lifecycle

void Pathfinding_Test_Tool::on_enable(editor_context_t &ctx)
{
  start.reset();
  end.reset();
  path.clear();
}

void Pathfinding_Test_Tool::on_disable(editor_context_t &ctx) {}

// ---------------------------------------------------------------------------
// Input

void Pathfinding_Test_Tool::on_update(editor_context_t &ctx,
                                     const viewport_state_t &view, float /*dt*/)
{
  viewport = view;
}

void Pathfinding_Test_Tool::on_mouse_down(editor_context_t &ctx,
                                         const input::mouse_event_t &e)
{
  if (e.button != input::mouse_button_t::Left)
    return;
  if (!ctx.map->navmesh.valid())
    return;

  linalg::vec3 hit;
  if (!pick_navmesh_point(ctx.map->navmesh, hit))
    return;

  if (e.mods.shift)
    end = hit;
  else
    start = hit;

  recompute_path(ctx.map->navmesh);
}

void Pathfinding_Test_Tool::on_mouse_drag(editor_context_t &ctx,
                                         const input::mouse_event_t &e) {}

void Pathfinding_Test_Tool::on_mouse_up(editor_context_t &ctx,
                                       const input::mouse_event_t &e) {}

void Pathfinding_Test_Tool::on_key_down(editor_context_t &ctx,
                                       const key_event_t &e)
{
  if (e.key == input::key_t::R)
  {
    start.reset();
    end.reset();
    path.clear();
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
  if (start)
  {
    constexpr color_t start_color = colors::green;
    linalg::vec3 base = *start + linalg::vec3{0, y_lift, 0};
    renderer.draw_circle(base, 16.f, {0, 1, 0}, start_color);
    renderer.draw_line(base, base + linalg::vec3{0, 48.f, 0}, start_color);
  }

  // End marker — red circle + vertical spike.
  if (end)
  {
    constexpr color_t end_color = colors::red;
    linalg::vec3 base = *end + linalg::vec3{0, y_lift, 0};
    renderer.draw_circle(base, 16.f, {0, 1, 0}, end_color);
    renderer.draw_line(base, base + linalg::vec3{0, 48.f, 0}, end_color);
  }

  // Path: yellow lines connecting waypoints, white boxes at each waypoint.
  if (!path.empty())
  {
    constexpr color_t path_color = colors::yellow;
    constexpr color_t node_color = colors::white;
    constexpr linalg::vec3 half{4, 4, 4};
    for (int i = 0; i < (int)path.size(); ++i)
    {
      linalg::vec3 wp = path[i];
      wp.y += y_lift;
      renderer.draw_wire_box(wp, half, node_color);
      if (i + 1 < (int)path.size())
      {
        linalg::vec3 next = path[i + 1];
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

  if (start)
    ImGui::Text("Start: (%.1f, %.1f, %.1f)", start->x, start->y, start->z);
  else
    ImGui::TextDisabled("Start: not set");

  if (end)
    ImGui::Text("End:   (%.1f, %.1f, %.1f)", end->x, end->y, end->z);
  else
    ImGui::TextDisabled("End:   not set");

  ImGui::Separator();

  if (!start || !end)
    ImGui::TextDisabled("Set both points to find a path.");
  else if (path.empty())
    ImGui::TextColored({1.f, 0.3f, 0.3f, 1.f}, "No path found.");
  else
    ImGui::Text("Path: %d waypoints", (int)path.size());

  ImGui::End();
}

} // namespace client
