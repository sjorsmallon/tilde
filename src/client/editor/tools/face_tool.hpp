#pragma once

#include "../../../shared/map/map.hpp"
#include "../../../shared/map/map_geometry_mesh.hpp"
#include "../editor_tool.hpp"
#include "../transaction_system.hpp"
#include "brush_stroke.hpp"

#include <functional>
#include <optional>
#include <vector>

namespace client
{

// The face tools of geometry_def.md §11.8 increment 11: a face of a mesh,
// addressed by its INDEX. Surface mode edits what the face carries (material
// layers, UV channel, grid, smoothing group); Paint and Sculpt stroke its grid.
// Every edit is one whole-value swap of the mesh.
class Face_Tool : public Editor_Tool
{
public:
  void on_enable(editor_context_t &ctx) override;
  void on_disable(editor_context_t &ctx) override;
  void on_update(editor_context_t &ctx, const viewport_state_t &view, float dt) override;

  void on_mouse_down(editor_context_t &ctx, const input::mouse_event_t &e) override;
  void on_mouse_drag(editor_context_t &ctx, const input::mouse_event_t &e) override;
  void on_mouse_up(editor_context_t &ctx, const input::mouse_event_t &e) override;
  void on_key_down(editor_context_t &ctx, const key_event_t &e) override;

  void on_draw_overlay(editor_context_t &ctx, pass_builder_t &draws) override;
  void on_draw_inspector(editor_context_t &ctx) override;

  Span<const shared::entity_uid_t> selected_objects() const override;

private:
  enum class Mode
  {
    Surface,
    Paint,
    Sculpt
  };

  // A mesh and, when the cursor or a click resolved one, a face of it.
  struct face_ref_t
  {
    shared::entity_uid_t uid = shared::null_entity_uid;
    std::optional<uint32_t> face_index;
  };

  // Where the cursor meets the selected mesh's DRAWN surface. The stroke radius
  // is measured from there, not from the face plane: on a sculpted face the two
  // are far apart.
  struct grid_cursor_t
  {
    std::optional<linalg::vec3> position;
    linalg::vec3 surface_normal{0, 1, 0};
    // The PLANE's normal, the axis a sculpt pushes along; pushing along the
    // displaced normal curls a crater in over its own rim.
    linalg::vec3 face_normal{0, 1, 0};
  };

  struct stroke_t
  {
    bool active = false;
    bool inverted = false;
    std::vector<stroke_brush_before_t> geometry_at_the_start;
  };

  struct paint_settings_t
  {
    float radius = 48.0f;
    float strength = 2.0f; // weight per second at the centre
    int layer = 1;         // the TARGET layer; painting toward 0 is the eraser
  };

  struct sculpt_settings_t
  {
    float radius = 64.0f;
    float strength = 128.0f; // world units per second at the centre
  };

  Mode mode = Mode::Surface;
  face_ref_t selection;
  face_ref_t hover;
  std::vector<shared::entity_uid_t> selected_uids;
  paint_settings_t paint;
  sculpt_settings_t sculpt;
  grid_cursor_t grid_cursor;
  stroke_t stroke;
  std::optional<shared::face_surface_t> face_clipboard;
  char material_path_input[256] = {};
  viewport_state_t cached_view;

  shared::mesh_geometry_t *try_get_mesh(editor_context_t &ctx, shared::entity_uid_t uid);
  const shared::face_surface_t *try_get_face_surface(editor_context_t &ctx,
                                                     const face_ref_t &face);

  void select_face(const face_ref_t &face);

  // Hover first: texturing is a sweep, so a select click before every
  // assignment doubles the input for no decision. The panel reads the
  // SELECTION, never the hover.
  face_ref_t face_under_cursor_or_selected() const;

  void edit_face_surface(editor_context_t &ctx, const face_ref_t &target, const char *name,
                         const std::function<void(shared::face_surface_t &)> &edit);
  void edit_mesh(editor_context_t &ctx, shared::entity_uid_t uid, const char *name,
                 const std::function<void(shared::mesh_geometry_t &)> &edit);

  void update_grid_stroke(editor_context_t &ctx, float dt);
  void end_grid_stroke(editor_context_t &ctx);

  void draw_surface_ui(editor_context_t &ctx);
  void draw_paint_ui(editor_context_t &ctx);
  void draw_sculpt_ui(editor_context_t &ctx);
};

} // namespace client
