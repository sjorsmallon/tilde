#pragma once

#include "../editor_tool.hpp"

namespace client
{

class Sculpting_Tool : public Editor_Tool
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

private:
  int hovered_geo_index = -1;
  int hovered_face = -1; // 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z

  bool dragging = false;
  int dragging_geo_index = -1;
  int dragging_face = -1;
  viewport_state_t last_view;
  linalg::vec3 drag_origin_point;
  // We only support AABB sculpting for now, but we store the variant to be safe
  // or copy of AABB? Storing the full geometry is safer if we revert.
  shared::static_geometry_t original_geometry;
};

} // namespace client
