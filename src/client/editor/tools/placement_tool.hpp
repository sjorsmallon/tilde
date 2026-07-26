#pragma once

#include "../../../shared/map_geometry.hpp"
#include "../editor_tool.hpp"
#include <optional>

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
  void on_update(editor_context_t &ctx, const viewport_state_t &view, float dt) override;

  void on_mouse_down(editor_context_t &ctx, const input::mouse_event_t &e) override;
  void on_mouse_drag(editor_context_t &ctx, const input::mouse_event_t &e) override;
  void on_mouse_up(editor_context_t &ctx, const input::mouse_event_t &e) override;
  void on_key_down(editor_context_t &ctx, const key_event_t &e) override;

  void on_draw_overlay(editor_context_t &ctx,
                       overlay_renderer_t &renderer) override;
  void on_draw_ui(editor_context_t &ctx) override;

private:
  // Select from the combined placeable table (geometry kinds first, then entity
  // types). Sets up exactly one of current_geometry / current_entity.
  void select_placeable(int index);

  linalg::vec3 ghost_position;
  bool ghost_valid = false;
  int selected_type_index = 0;

  // The prototype being placed. Exactly one is engaged at a time — which of the
  // two regimes the selected placeable belongs to.
  std::optional<shared::geometry_value_t> current_geometry;
  std::shared_ptr<::network::Entity> current_entity;
};

} // namespace client
