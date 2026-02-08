#pragma once

#include "../editor_tool.hpp"

namespace network
{
class Entity;
}

namespace client
{

class Placement_Tool : public Editor_Tool
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
  linalg::vec3 ghost_pos;
  bool ghost_valid = false;

  std::shared_ptr<::network::Entity> current_entity;
};

} // namespace client
