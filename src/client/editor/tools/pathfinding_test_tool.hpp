#pragma once

#include "../editor_tool.hpp"
#include <optional>
#include <vector>

namespace client
{

class PathfindingTestTool : public Editor_Tool
{
public:
  void on_enable(editor_context_t &ctx) override;
  void on_disable(editor_context_t &ctx) override;
  void on_update(editor_context_t &ctx, const viewport_state_t &view) override;

  void on_mouse_down(editor_context_t &ctx, const mouse_event_t &e) override;
  void on_mouse_drag(editor_context_t &ctx, const mouse_event_t &e) override;
  void on_mouse_up(editor_context_t &ctx, const mouse_event_t &e) override;
  void on_key_down(editor_context_t &ctx, const key_event_t &e) override;

  void on_draw_overlay(editor_context_t &ctx, overlay_renderer_t &renderer) override;
  void on_draw_ui(editor_context_t &ctx) override;

private:
  std::optional<linalg::vec3> m_start;
  std::optional<linalg::vec3> m_end;
  std::vector<linalg::vec3>   m_path;
  viewport_state_t             m_viewport; // cached from on_update for mouse picking

  // Intersects the current mouse ray against navmesh polygon triangles.
  // Returns true and sets out_hit to the closest hit point.
  bool pick_navmesh_point(const navmesh_t &nav, linalg::vec3 &out_hit) const;

  void recompute_path(const navmesh_t &nav);
};

} // namespace client
