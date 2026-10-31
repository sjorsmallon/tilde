#pragma once

#include "../editor_tool.hpp"
#include <optional>
#include <vector>

namespace client
{

class Pathfinding_Test_Tool : public Editor_Tool
{
public:
  void on_enable(editor_context_t& ctx) override;
  void on_disable(editor_context_t& ctx) override;
  void on_update(editor_context_t& ctx, const viewport_state_t &view, float dt) override;

  void on_mouse_down(editor_context_t& ctx, const input::mouse_event_t &e) override;
  void on_mouse_drag(editor_context_t& ctx, const input::mouse_event_t &e) override;
  void on_mouse_up(editor_context_t& ctx, const input::mouse_event_t &e) override;
  void on_key_down(editor_context_t& ctx, const key_event_t &e) override;

  void on_draw_overlay(editor_context_t& ctx, draw_lists_t &draws) override;
  void on_draw_inspector(editor_context_t& ctx) override;

private:

  std::optional<linalg::vec3> start;
  std::optional<linalg::vec3> end;
  std::vector<linalg::vec3> path;
  
  viewport_state_t viewport; 

  bool pick_navmesh_point(const shared::navmesh_t &nav, linalg::vec3 &out_hit) const;

  void recompute_path(const shared::navmesh_t &nav);
};

} // namespace client
