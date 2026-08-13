#pragma once

#include "../editor_tool.hpp"
#include "../../../shared/box_face.hpp"
#include "../../../shared/map.hpp"
#include "../transaction_system.hpp"
#include <optional>

namespace client
{

class Sculpting_Tool : public Editor_Tool
{
public:
  void on_enable(editor_context_t &ctx) override;
  void on_disable(editor_context_t &ctx) override;
  void on_update(editor_context_t &ctx, const viewport_state_t &view, float dt) override;

  void on_mouse_down(editor_context_t &ctx, const input::mouse_event_t &e) override;
  void on_mouse_drag(editor_context_t &ctx, const input::mouse_event_t &e) override;
  void on_mouse_up(editor_context_t &ctx, const input::mouse_event_t &e) override;
  void on_key_down(editor_context_t &ctx, const key_event_t &e) override;

  void on_draw_overlay(editor_context_t &ctx,
                       pass_builder_t &draws) override;

private:
  shared::entity_uid_t hovered_uid = shared::invalid_entity_uid;
  shared::box_face_t hovered_face = shared::box_face_t::Invalid;

  bool dragging = false;
  shared::entity_uid_t dragging_uid = shared::invalid_entity_uid;
  shared::box_face_t dragging_face = shared::box_face_t::Invalid;
  
  viewport_state_t last_view;
  linalg::vec3 drag_origin_point;
  shared::aabb_t original_aabb;

  // Pre-drag state, one flavor per regime — exactly one is engaged. See
  // commit_sculpt.
  entity_snapshot_t sculpt_start_entity;
  std::optional<shared::geometry_value_t> sculpt_start_geometry;

  void commit_sculpt(editor_context_t &ctx);
};

} // namespace client
