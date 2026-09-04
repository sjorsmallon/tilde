#pragma once

#include "../../../shared/brush.hpp"
#include "../../../shared/map.hpp"
#include "../../../shared/map_geometry.hpp"
#include "../editor_tool.hpp"
#include "../transaction_system.hpp"

#include <functional>
#include <optional>
#include <vector>


// before it gets more confusing: there is a clear distinction between
// points and vertices. points are points that occupy 3d space. nothing else.
// vertices belong to a render mesh. they contain a normal, and whatever else.
// it can be that a single point has multiple vertices because it is part of 
// a set of faces, like the corner of a cube.

namespace client
{

class Brush_Tool : public Editor_Tool
{
public:
  void on_enable(editor_context_t& ctx) override;
  void on_disable(editor_context_t& ctx) override;
  void on_update(editor_context_t& ctx, const viewport_state_t &view,
                 float dt) override;

  void on_mouse_down(editor_context_t& ctx,
                     const input::mouse_event_t &e) override;
  void on_mouse_drag(editor_context_t& ctx,
                     const input::mouse_event_t &e) override;
  void on_mouse_up(editor_context_t& ctx,
                   const input::mouse_event_t &e) override;
  void on_key_down(editor_context_t& ctx, const key_event_t &e) override;

  void on_draw_overlay(editor_context_t& ctx, pass_builder_t &draws) override;
  void on_draw_ui(editor_context_t& ctx) override;

  bool capture_keyboard() const override { return true; }

private:
  static constexpr int INVALID_FACE = -1;

  enum class Mode
  {
    Face,   // move the whole face.
    Vertex, // pick points on the face grid, drag or extrude them
    Paint,  // paint between two textures at vertex positions.
    Sculpt  // brush the face grid itself in or out along the face normal
  };

  struct selection_t
  {
    shared::entity_uid_t uid = shared::invalid_entity_uid;
    std::optional<linalg::vec3> face_normal;
    std::vector<linalg::vec3> points;
  };

  struct selection_geometry_t
  {
    std::optional<shared::brush_polyhedron_t> hull;
    int face_idx = INVALID_FACE;
    std::vector<linalg::vec3> vertex_handles;

    // A SUBDIVIDED face's handles are its grid vertices rather than its corners
    // and grid-line points, and they move a different thing: an offset on the
    // face, not a point in the brush's set. One flag rather than a second handle
    // list, because a face is one or the other and never both.
    bool handles_are_grid_vertices = false;
  };

  struct hover_t
  {
    shared::entity_uid_t uid = shared::invalid_entity_uid;
    std::optional<linalg::vec3> face_normal;
  };

  // the mouse is, at most, only doing one of these things when dragging (or only not dragging)
  enum class Drag
  {
    None,
    Band_Armed,      // pressed on the face, not yet travelled: still a click
    Band_Sizing,     // travelled past BAND_DRAG_THRESHOLD, the rect is live
    Face,            // the picked face, along its normal
    Vertices,        // the selected points, along the face normal
    Extrusion_Depth, // the pending extrusion's height
  };

  // drag in a world direction, with a start anchor.
  struct axis_drag_t
  {
    linalg::vec3 direction{0, 0, 0};
    linalg::vec3 anchor{0, 0, 0};
    float start_distance = 0.0f;
  };

  // different name for box select.
  struct band_t
  {
    linalg::vec2 start{0, 0};
    linalg::vec2 end{0, 0};
    bool adds_to_point_selection = false; // ctrl at arm time; releasing it mid-drag still adds
    shared::entity_uid_t        press_uid = shared::invalid_entity_uid;
    std::optional<linalg::vec3> press_face_normal;
  };

  struct drag_t
  {
    Drag kind = Drag::None;
    axis_drag_t axis; // Face, Vertices, Extrusion_Depth
    band_t band;      // Band_Armed, Band_Sizing

    std::optional<shared::geometry_value_t> geometry_at_the_start_of_drag;
    std::vector<linalg::vec3> vertex_start_points; // Vertices
  };

  struct extrusion_t
  {
    bool pending = false;
    float depth = 0.0f;
  };

  // Where the cursor meets the brush's DISPLACED surface. Both stroke modes
  // resolve it the same way and neither can use the face plane: on a sculpted
  // face the two are far apart, and a radius measured from the plane reaches
  // through the hill it is standing on.
  struct grid_cursor_t
  {
    std::optional<linalg::vec3> position;
    linalg::vec3 surface_normal{0, 1, 0};

    // The PLANE's normal, which is the axis a sculpt pushes along -- pushing
    // along the displaced normal instead curls a crater in over its own rim.
    linalg::vec3 face_normal{0, 1, 0};
  };


  struct stroke_t
  {
    bool active = false;
    bool inverted = false;

    std::optional<shared::geometry_value_t> geometry_at_the_start;
  };

  //@FIXME(SJM): layer is the target layer where we are painting towards.
  struct paint_settings_t
  {
    float radius = 48.0f;
    float strength = 2.0f; // weight per second at the centre of the brush
    int layer = 1;
  };

  struct sculpt_settings_t
  {
    float radius = 64.0f;
    float strength = 128.0f; // world units per second at the centre
  };

  std::optional<shared::face_surface_t> face_clipboard;

  //@FIXME(SJM): we need to tear this out, I think.
  // The panel's "add a material" field. A path an author browses to, which is
  // why the table holds free-form paths rather than manifest ids.
  char material_path_input[256] = {};

  Mode mode = Mode::Face;
  paint_settings_t paint;
  sculpt_settings_t sculpt;
  grid_cursor_t grid_cursor;
  stroke_t stroke;
  selection_t selection;
  selection_geometry_t selection_geometry;
  hover_t hover;
  drag_t drag;
  extrusion_t extrusion;
  viewport_state_t cached_view;
  
  bool drag_is_live() const
  {
    return drag.kind != Drag::None && drag.kind != Drag::Band_Armed;
  }
 
  // started a band but never moved the cursor.
  void resolve_band_press_as_click(editor_context_t& ctx);

  void end_drag();

  shared::brush_geometry_t* try_get_selected_brush(editor_context_t& ctx);

  // one grid step along `direction`, applied to the whole brush.
  void nudge_selected_brush(editor_context_t& ctx,
                            const linalg::vec3 &direction);

  void delete_selected_brush(editor_context_t& ctx);
  
  void select_brush_face(editor_context_t& ctx, shared::entity_uid_t uid,
                         std::optional<linalg::vec3> face_normal);

  void rebuild_hull_and_handles(editor_context_t& ctx);
  void clear_point_selection();
  void cancel_pending_extrusion();
  void commit_pending_extrusion(editor_context_t& ctx);

  bool try_rebuild_selected_brush(editor_context_t& ctx,
                                 std::vector<linalg::vec3> vertices);

  struct pending_extrusion_solids_t
  {
    std::vector<std::vector<linalg::vec3>> point_sets;
    bool every_selected_point_accounted_for = false;
  };

  pending_extrusion_solids_t build_pending_extrusion_solids(float grid_step) const;

  float grid_step_for(const editor_context_t& ctx,
                      const input::modifiers_t &mods) const;

  struct face_target_t
  {
    shared::entity_uid_t uid = shared::invalid_entity_uid;
    shared::brush_geometry_t* brush = nullptr;
    Plane plane = {};
  };

  face_target_t resolve_face_target_under_cursor(editor_context_t& ctx);
  face_target_t resolve_selected_face_target(editor_context_t& ctx);
  face_target_t resolve_face_target(editor_context_t& ctx, shared::entity_uid_t uid,
                                    const std::optional<linalg::vec3>& face_normal);

  void edit_face_surface(editor_context_t& ctx, const face_target_t& target,
                         const std::function<void(shared::face_surface_t&)> &edit);

  void draw_material_ui(editor_context_t& ctx);
  void draw_paint_ui(editor_context_t& ctx);
  void draw_sculpt_ui(editor_context_t& ctx);

  bool mode_is_a_grid_stroke() const
  {
    return mode == Mode::Paint || mode == Mode::Sculpt;
  }

  // Where the cursor meets the selected brush's displaced surface, and the
  // stroke that writes there. Both no-op unless a stroke mode is live.
  void update_grid_stroke(editor_context_t& ctx, float dt);
  void end_grid_stroke(editor_context_t& ctx);

  bool point_is_selected(const linalg::vec3& point) const;
  void toggle_point_selection(const linalg::vec3& point, bool additive);
};

} // namespace client
