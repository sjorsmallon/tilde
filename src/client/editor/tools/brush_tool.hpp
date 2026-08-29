#pragma once

#include "../../../shared/brush.hpp"
#include "../../../shared/map.hpp"
#include "../../../shared/map_geometry.hpp"
#include "../editor_tool.hpp"
#include "../transaction_system.hpp"

#include <optional>
#include <vector>

namespace client
{

class Brush_Tool : public Editor_Tool
{
public:
  void on_enable(editor_context_t &ctx) override;
  void on_disable(editor_context_t &ctx) override;
  void on_update(editor_context_t &ctx, const viewport_state_t &view,
                 float dt) override;

  void on_mouse_down(editor_context_t &ctx,
                     const input::mouse_event_t &e) override;
  void on_mouse_drag(editor_context_t &ctx,
                     const input::mouse_event_t &e) override;
  void on_mouse_up(editor_context_t &ctx,
                   const input::mouse_event_t &e) override;
  void on_key_down(editor_context_t &ctx, const key_event_t &e) override;

  void on_draw_overlay(editor_context_t &ctx, pass_builder_t &draws) override;
  void on_draw_ui(editor_context_t &ctx) override;

  bool capture_keyboard() const override { return true; }

private:
  enum class Mode
  {
    Face,  // pick a face, drag it along its normal
    Vertex // pick points on the face grid, drag or extrude them
  };

  static constexpr int INVALID_FACE = -1;


  struct selection_t
  {
    shared::entity_uid_t uid = shared::invalid_entity_uid;
    std::optional<linalg::vec3> face_normal;
    std::vector<linalg::vec3> points;
  };

  // Derived from `selection`. Only select_brush_face and on_update write it.
  struct selection_geometry_t
  {
    std::optional<shared::brush_polyhedron_t> hull;
    int face_idx = INVALID_FACE;
    std::vector<linalg::vec3> vertex_handles;
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

  // Face, Vertices and Extrusion_Depth all drag along ONE axis: the cursor ray
  // projects onto it and the travel since the press is the whole answer.
  struct axis_drag_t
  {
    linalg::vec3 direction{0, 0, 0};
    linalg::vec3 anchor{0, 0, 0};
    float start_distance = 0.0f;
  };

  // Latched at the press, because a band outlives the modifiers and the hover
  // that started it.
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

    // Face, Vertices: the brush as it was at the press. The drag writes
    // straight into the map so it can be seen, so this is both what each frame
    // recomputes from and the transaction's before-image.
    std::optional<shared::geometry_value_t> geometry_at_the_start_of_drag;
    std::vector<linalg::vec3> vertex_start_points; // Vertices
  };

  // Outlives the drag that sized it: it stands until Enter or Escape.
  struct extrusion_t
  {
    bool pending = false;
    float depth = 0.0f;
  };

  Mode mode = Mode::Face;
  selection_t selection;
  selection_geometry_t selection_geometry;
  hover_t hover;
  drag_t drag;
  extrusion_t extrusion;

  // Band_Armed is not a drag yet -- until the cursor travels, the press is
  // still a click, so the hover keeps tracking under it.
  bool drag_is_live() const
  {
    return drag.kind != Drag::None && drag.kind != Drag::Band_Armed;
  }

  // This frame. Written at the top of on_update, read by handlers that run
  // later in it.
  viewport_state_t cached_view;

  shared::brush_geometry_t* try_get_selected_brush(editor_context_t &ctx);

  void end_drag();

  // one grid step along `direction`, applied to the whole brush.
  void nudge_selected_brush(editor_context_t &ctx,
                            const linalg::vec3 &direction);

  void delete_selected_brush(editor_context_t &ctx);

  // An armed band whose cursor never travelled was a click after all. This is
  // what it would have meant on press.
  void resolve_band_press_as_click(editor_context_t &ctx);

  // The ONE way the selection changes: writes it and rebuilds selection_geometry,
  // so the two cannot disagree.
  void select_brush_face(editor_context_t &ctx, shared::entity_uid_t uid,
                         std::optional<linalg::vec3> face_normal);

  void rebuild_hull_and_handles(editor_context_t &ctx);
  void clear_point_selection();
  void cancel_pending_extrusion();
  void commit_pending_extrusion(editor_context_t &ctx);

  bool try_rebuild_selected_brush(editor_context_t &ctx,
                                 std::vector<linalg::vec3> vertices);

  struct pending_extrusion_solids_t
  {
    std::vector<std::vector<linalg::vec3>> point_sets;
    bool every_selected_point_accounted_for = false;
  };

  pending_extrusion_solids_t build_pending_extrusion_solids(float grid_step) const;

  float grid_step_for(const editor_context_t &ctx,
                      const input::modifiers_t &mods) const;

  bool point_is_selected(const linalg::vec3 &point) const;
  void toggle_point_selection(const linalg::vec3 &point, bool additive);
};

} // namespace client
