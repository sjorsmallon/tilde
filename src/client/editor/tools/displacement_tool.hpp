#pragma once

#include "../../../shared/map.hpp"
#include "../../../shared/map_geometry.hpp"
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
  void on_update(editor_context_t &ctx, const viewport_state_t &view, float dt) override;

  void on_mouse_down(editor_context_t &ctx, const input::mouse_event_t &e) override;
  void on_mouse_drag(editor_context_t &ctx, const input::mouse_event_t &e) override;
  void on_mouse_up(editor_context_t &ctx, const input::mouse_event_t &e) override;
  void on_key_down(editor_context_t &ctx, const key_event_t &e) override;

  void on_draw_overlay(editor_context_t &ctx,
                       overlay_renderer_t &renderer) override;
  void on_draw_ui(editor_context_t &ctx) override;

  bool capture_keyboard() const override { return mode == Mode::Select; }

private:
  enum class Mode
  {
    Setup,  // Select displacement geometry, pick face, set subdivision
    Paint,  // Brush displacement painting
    Select  // Box-select vertices and step height
  };

  Mode mode = Mode::Setup;

  // Setup mode state
  shared::entity_uid_t selected_uid = shared::invalid_entity_uid;
  shared::entity_uid_t hovered_uid  = shared::invalid_entity_uid;
  shared::box_face_t   hovered_face = shared::box_face_t::Invalid;

  // Paint mode state
  bool currently_painting = false;
  linalg::vec3 cursor_position = {};
  linalg::vec3 cursor_normal = {};
  bool cursor_is_currently_over_a_displacement_face = false;

  struct brush_t
  {
    float radius = 32.0f;
    float strength = 2.0f;
  };

  brush_t brush{.radius = 32.f, .strength = 2.f};
  
  // Subdivision control
  int pending_subdivision = 4;

  // Face-drag resize (Setup mode, before displacement)
  bool currently_dragging_a_face_to_resize = false;
  bool selected_face_was_actually_resized = false;
  shared::box_face_t face_to_resize = shared::box_face_t::Invalid;
  viewport_state_t resize_last_view;

  // optional because..?
  std::optional<shared::geometry_value_t> resize_start_geometry;

  // Select mode state
  std::vector<bool> selected_vertices_bitmask;          // grid_size*grid_size selection bitmask
  bool box_selecting = false;
  linalg::vec2 box_start_screen;        // pixels, drag start
  linalg::vec2 box_end_screen;          // pixels, drag current/end

  float extent_snap = 128.0f;
  viewport_state_t cached_view;         // updated each on_update
  std::optional<shared::geometry_value_t> select_start_geometry;

  // Helpers
  shared::displacement_geometry_t *get_displacement_if_it_is_selected(editor_context_t &ctx);
  bool raycast_displacement_mesh(const shared::displacement_geometry_t &displacement,
                                 const linalg::vec3 &ray_origin,
                                 const linalg::vec3 &ray_dir, float &out_t,
                                 linalg::vec3 &out_normal);

  void apply_brush(shared::displacement_geometry_t &displacement, float dt, bool invert);

  linalg::vec2 project_to_screen(const linalg::vec3 &world_pos) const;

  // Push a value-swap transaction for a finished multi-frame edit, then drop the
  // start state. Both take the snapshot member by reference so they clear it.
  void commit_geometry_edit(editor_context_t &ctx,
                            std::optional<shared::geometry_value_t> &start_state);

  void clear_selection(int grid_size);
};

} // namespace client
