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

// Editing convex brushes: pick a face, pull it; or drop to the face grid, pick
// points, and pull those out as a new brush.
//
// TWO MODES, because one drag gesture cannot mean four things. In Face mode a
// drag moves the face under it; in Vertex mode the same drag rubber-bands a
// point selection. TrenchBroom splits these for the same reason. Tab switches.
//
// WHAT IS AND IS NOT CACHED. The hull of the selected brush and the grid lattice
// on its selected face are both recomputed EVERY FRAME in on_update, and neither
// is invalidated by anything. A brush is at most MAX_BRUSH_VERTICES points, so
// hulling one costs less than the draw call that follows it, and the whole class
// of "the tool is showing the shape from before the edit" simply does not exist.
//
// So nothing here is an index into a hull. The selected face is remembered by
// its NORMAL -- unique on a convex solid, since two faces sharing one would be
// coplanar and therefore one face -- and the selected lattice points by their
// POSITIONS. Both survive an edit that renumbers everything.
class Brush_Tool : public Editor_Tool
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
  void on_draw_ui(editor_context_t &ctx) override;

  bool capture_keyboard() const override { return true; }

private:
  enum class Mode
  {
    Face,  // pick a face, drag it along its normal
    Vertex // pick points on the face grid, drag or extrude them
  };

  Mode mode = Mode::Face;

  shared::entity_uid_t selected_uid = shared::invalid_entity_uid;
  shared::entity_uid_t hovered_uid  = shared::invalid_entity_uid;

  // Remembered by normal, not by index -- see the class comment.
  std::optional<linalg::vec3> selected_face_normal;
  std::optional<linalg::vec3> hovered_face_normal;

  // Rebuilt every frame from the selected brush. Empty when nothing is selected
  // or when the selection does not hull.
  std::optional<shared::brush_polyhedron_t> hull;
  int                                       selected_face = -1; // index into hull->faces
  int                                       hovered_face  = -1;

  // Grid points on the selected face, plus its real corners. Rebuilt every frame.
  std::vector<linalg::vec3> face_lattice;

  // The footprint: which lattice points are picked. Positions rather than
  // indices, so rebuilding the lattice cannot silently repoint them.
  std::vector<linalg::vec3> selected_points;

  // --- face drag (Face mode) ---
  bool                                     dragging_face = false;
  linalg::vec3                             drag_axis{0, 0, 0};
  linalg::vec3                             drag_anchor{0, 0, 0};
  float                                    drag_start_parameter = 0.0f;
  std::optional<shared::geometry_value_t>  drag_start_geometry;

  // --- rubber band (Vertex mode) ---
  //
  // ARMED is not BANDING. A press on empty face area only arms; the band starts
  // on the first drag past BAND_DRAG_THRESHOLD, and the point selection is
  // cleared THERE rather than on the press. Clearing on the press meant a click
  // that missed a handle by two pixels threw the whole selection away before the
  // user had done anything, which read as the tool dropping into box-select at
  // the slightest provocation.
  bool         band_armed     = false;
  bool         rubber_banding = false;
  linalg::vec2 band_start{0, 0};
  linalg::vec2 band_end{0, 0};
  bool         band_adds = false; // ctrl held when the band was armed

  // --- vertex drag (Vertex mode) ---
  bool                      dragging_vertices = false;
  std::vector<linalg::vec3> vertex_drag_start_points;

  // --- the pending extrusion (Vertex mode) ---
  //
  // Alive from the first shift-drag until Enter or Escape. Mouse-up does NOT
  // commit it: while it stands, clicking another lattice point adds that point
  // to the footprint and the preview re-hulls, which is the whole reason it is
  // a held state rather than the result of one gesture.
  bool  pending_extrusion   = false;
  bool  dragging_extrusion  = false;
  float pending_depth       = 0.0f;

  viewport_state_t cached_view;

  shared::brush_geometry_t *try_get_selected_brush(editor_context_t &ctx);

  // Drop every in-progress gesture without touching what is SELECTED. Anything
  // that changes the meaning of the next input calls this -- switching mode,
  // Escape, entering and leaving the tool. A half-finished band or drag that
  // outlives the thing that started it is a tool that has silently stopped
  // responding, and the user has no way to clear it.
  void cancel_in_progress_gestures();

  // One grid step along `direction`, applied to the whole brush.
  void nudge_selected_brush(editor_context_t &ctx, const linalg::vec3 &direction);

  void refresh_hull_and_lattice(editor_context_t &ctx);
  void clear_point_selection();
  void cancel_pending_extrusion();
  void commit_pending_extrusion(editor_context_t &ctx);

  // Replace the selected brush with `vertices` if they still form a solid,
  // pushing one value-swap transaction. Rejects and logs otherwise -- an edit
  // that would destroy the brush leaves it exactly as it was.
  bool try_apply_vertices(editor_context_t &ctx, std::vector<linalg::vec3> vertices,
                          bool push_transaction);

  // What the pending extrusion will actually create. Usually one brush; a
  // footprint that reads as filled grid cells becomes one rectangle per piece,
  // because a concave shape is not one convex brush. `from_grid_cells` says
  // which reading won, so the commit knows whether picks were swallowed.
  struct pending_extrusion_solids_t
  {
    std::vector<std::vector<linalg::vec3>> point_sets;
    bool                                   from_grid_cells = false;
  };

  pending_extrusion_solids_t build_pending_extrusion_solids(float grid_step) const;

  float grid_step_for(const editor_context_t &ctx, const input::modifiers_t &mods) const;

  int  try_pick_lattice_point(const linalg::vec2 &screen_position) const;
  bool point_is_selected(const linalg::vec3 &point) const;
  void toggle_point_selection(const linalg::vec3 &point, bool additive);
};

} // namespace client
