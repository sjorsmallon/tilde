#pragma once

#include "../../states/editor_gizmo.hpp"
#include "../editor_tool.hpp"
#include "../path_editing.hpp"
#include "../transaction_system.hpp"

#include <optional>
#include <vector>

namespace client
{

class Path_Tool : public Editor_Tool
{
public:
  void on_enable(editor_context_t& ctx) override;
  void on_disable(editor_context_t& ctx) override;
  void on_update(editor_context_t& ctx, const viewport_state_t& view, float dt) override;

  void on_mouse_down(editor_context_t& ctx, const input::mouse_event_t& e) override;
  void on_mouse_drag(editor_context_t& ctx, const input::mouse_event_t& e) override;
  void on_mouse_up(editor_context_t& ctx, const input::mouse_event_t& e) override;
  void on_key_down(editor_context_t& ctx, const key_event_t& e) override;

  void on_draw_overlay(editor_context_t& ctx, pass_builder_t& draws) override;
  void on_draw_ui(editor_context_t& ctx) override;

  std::optional<view_focus_t> view_focus() const override;
  Span<const shared::entity_uid_t> selected_objects() const override;

private:
  struct drag_origin_t
  {
    shared::entity_uid_t          uid = shared::null_entity_uid;
    linalg::vec3                  position = {0, 0, 0};
    std::optional<linalg::quatf>  orientation;
    entity_snapshot_t             entity_before;
    std::optional<shared::geometry_value_t> geometry_before;
  };

  struct field_edit_t
  {
    shared::entity_uid_t uid = shared::null_entity_uid;
    entity_snapshot_t    before;
  };

  path_scratch_t scratch;

  shared::entity_uid_t active_node   = shared::null_entity_uid;
  shared::entity_uid_t subject_mover = shared::null_entity_uid;
  shared::entity_uid_t hovered_uid   = shared::null_entity_uid;
  std::optional<linalg::vec3> placement_point;

  std::vector<shared::entity_uid_t> chain;
  std::vector<shared::entity_uid_t> active_selection;

  viewport_state_t cached_viewport{};
  Editor_Gizmo     gizmo;
  std::vector<drag_origin_t> drag_origins;

  std::optional<field_edit_t> field_edit;

  bool  show_preview     = true;
  bool  playing          = false;
  float preview_seconds  = 0.0f;
  float playback_speed   = 1.0f;
  float cycle_seconds    = 0.0f;

  void refresh(editor_context_t& ctx);
  [[nodiscard]] gizmo_view_t make_gizmo_view() const;
  [[nodiscard]] shared::entity_uid_t pick_marker(const editor_context_t& ctx) const;
  [[nodiscard]] shared::entity_uid_t mover_under_ray(const editor_context_t& ctx) const;
  void adopt(editor_context_t& ctx, shared::entity_uid_t uid);

  void begin_drag(editor_context_t& ctx);
  void apply_drag(editor_context_t& ctx, const gizmo_drag_t& drag);
  void commit_drag(editor_context_t& ctx);

  void place_node(editor_context_t& ctx, const linalg::vec3& position);
  void delete_active_node(editor_context_t& ctx);
  void start_mover_at_active_node(editor_context_t& ctx, shared::entity_uid_t mover);

  void begin_field_edit(const editor_context_t& ctx, shared::entity_uid_t uid);
  void end_field_edit(editor_context_t& ctx);

  [[nodiscard]] float chain_cycle_seconds(const std::vector<shared::entity_uid_t>& walk,
                                          float tickrate) const;
  void draw_mover_preview(editor_context_t& ctx, pass_builder_t& draws);
};

} // namespace client
