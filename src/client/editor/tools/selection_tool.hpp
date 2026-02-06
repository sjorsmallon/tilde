#pragma once

#include "../editor_tool.hpp"
#include <vector>

namespace client
{

class Selection_Tool : public Editor_Tool
{
public:
  void on_enable(editor_context_t &ctx) override;
  void on_disable(editor_context_t &ctx) override;
  void on_update(editor_context_t &ctx, const viewport_state_t &view) override;

  void on_mouse_down(editor_context_t &ctx, const mouse_event_t &e) override;
  void on_mouse_drag(editor_context_t &ctx, const mouse_event_t &e) override;
  void on_mouse_up(editor_context_t &ctx, const mouse_event_t &e) override;
  void on_key_down(editor_context_t &ctx, const key_event_t &e) override;

  void on_draw_overlay(editor_context_t &ctx,
                       overlay_renderer_t &renderer) override;

  void on_draw_ui(editor_context_t &ctx) override;

private:
  int hovered_geo_index = -1;
  std::vector<int> selected_geometry_indices;

  // Drag box selection
  bool is_dragging_box = false;
  linalg::vec2i drag_start_pos;
  linalg::vec2i drag_current_pos;

  // Cached viewport for projection in on_draw_ui / selection logic
  viewport_state_t cached_viewport;
};

} // namespace client
