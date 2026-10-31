#pragma once

#include "../../../shared/map/map.hpp"
#include "../../../shared/map/mesh/mesh_boolean.hpp"
#include "../editor_tool.hpp"

#include <optional>

namespace client
{

// The boolean of geometry_def.md §11.8 increment 13, driven by hand: the first
// click names the WALL, the second the CUTTER, a convex mesh. The result is
// previewed over both, Tab swaps the roles, Enter commits one transaction that
// replaces the wall by the shells of the result and removes the cutter. Right
// click steps back one pick, Escape clears.
class Subtract_Tool : public Editor_Tool
{
public:
  void on_enable(editor_context_t &ctx) override;
  void on_disable(editor_context_t &ctx) override;
  void on_update(editor_context_t &ctx, const viewport_state_t &view, float dt) override;

  void on_mouse_down(editor_context_t &ctx, const input::mouse_event_t &e) override;
  void on_mouse_drag(editor_context_t &ctx, const input::mouse_event_t &e) override;
  void on_mouse_up(editor_context_t &ctx, const input::mouse_event_t &e) override;
  void on_key_down(editor_context_t &ctx, const key_event_t &e) override;

  void on_draw_overlay(editor_context_t &ctx, draw_lists_t &draws) override;
  void on_draw_inspector(editor_context_t &ctx) override;

private:
  shared::entity_uid_t wall_uid = shared::null_entity_uid;
  shared::entity_uid_t cutter_uid = shared::null_entity_uid;

  std::optional<shared::mesh_subtraction_t> preview;
  std::optional<shared::mesh_geometry_t> previewed_wall;
  std::optional<shared::mesh_geometry_t> previewed_cutter;

  viewport_state_t cached_view{};

  void clear();
  void rebuild_preview(editor_context_t &ctx);
  void commit(editor_context_t &ctx);
};

} // namespace client
