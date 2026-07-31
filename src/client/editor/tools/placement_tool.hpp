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

  void select_placeable(int index);
  int get_index_of_placeable(const std::string &label);

  linalg::vec3 ghost_position;
  bool cursor_is_currently_over_surface = false;
  int selected_type_index = 0;

  // one or the other is populated.
  std::optional<shared::geometry_value_t> geometry_to_place;
  std::shared_ptr<::entities::Entity> entity_to_place;
};

} // namespace client
