#pragma once

#include "../../../shared/entities/displacement_entity.hpp"
#include "../../../shared/map.hpp"
#include "../editor_tool.hpp"
#include "../transaction_system.hpp"
#include <optional>

namespace client
{

class Displacement_Tool : public Editor_Tool
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

  bool capture_keyboard() const override { return mode == Mode::Select; }

private:
  enum class Mode
  {
    Setup,  // Select entity, pick face, set subdivision
    Paint,  // Brush displacement painting
    Select  // Box-select vertices and step height
  };

  Mode mode = Mode::Setup;

  // Setup mode state
  shared::entity_uid_t selected_uid = 0;
  shared::entity_uid_t hovered_uid = 0;
  int hovered_face = -1;

  // Paint mode state
  bool painting = false;
  linalg::vec3 cursor_pos;
  linalg::vec3 cursor_normal;
  bool cursor_valid = false;

  // Brush parameters
  float brush_radius = 32.0f;
  float brush_strength = 2.0f;

  // Subdivision control
  int pending_subdivision = 4;

  // Face-drag resize (Setup mode, before displacement)
  bool resize_dragging = false;
  bool resize_moved = false;
  int resize_face = -1;
  viewport_state_t resize_last_view;
  std::optional<Edit_Recorder> resize_edit;

  // Select mode state
  std::vector<bool> sel_verts;          // gs*gs selection bitmask
  bool box_selecting = false;
  linalg::vec2 box_start_screen;        // pixels, drag start
  linalg::vec2 box_end_screen;          // pixels, drag current/end
  float height_snap = 128.0f;
  viewport_state_t cached_view;         // updated each on_update
  std::optional<Edit_Recorder> select_edit;

  // Helpers
  network::Displacement_Entity *get_selected(editor_context_t &ctx);
  bool raycast_displacement_mesh(const network::Displacement_Entity &ent,
                                 const linalg::vec3 &ray_origin,
                                 const linalg::vec3 &ray_dir, float &out_t,
                                 linalg::vec3 &out_normal);
  void apply_brush(network::Displacement_Entity &ent, float dt, bool invert);
  void regenerate_mesh(network::Displacement_Entity &ent,
                       shared::entity_uid_t uid);
  linalg::vec2 project_to_screen(const linalg::vec3 &world_pos) const;
  void commit_select_edit();
  void clear_selection(int gs);
};

} // namespace client
