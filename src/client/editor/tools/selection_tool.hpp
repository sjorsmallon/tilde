#pragma once

#include "../../states/editor_gizmo.hpp"
#include "../editor_tool.hpp"
#include "../transaction_system.hpp"
#include "../../../shared/map.hpp"
#include <optional>
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
  shared::entity_uid_t hovered_uid = 0; // 0 = none
  std::vector<shared::entity_uid_t> selected_uids;

  // Drag box selection
  bool is_dragging_box = false;
  linalg::vec2i drag_start_pos;
  linalg::vec2i drag_current_pos;

  // Cached viewport for projection in on_draw_ui / selection logic
  viewport_state_t cached_viewport;

  // Grid indication
  bool grid_hover_valid = false;
  linalg::vec3 grid_hover_pos;

  // Gizmo
  Editor_Gizmo editor_gizmo;

  // Direct object drag (Ctrl+LMB to move in camera view plane)
  bool is_dragging_object = false;
  std::vector<std::pair<shared::entity_uid_t, linalg::vec3>> drag_start_positions;
  linalg::vec3 drag_plane_hit_start;    // initial plane hit point
  linalg::vec3 drag_plane_normal;       // normal of the drag plane
  std::optional<Edit_Recorder> drag_edit;
};

} // namespace client
