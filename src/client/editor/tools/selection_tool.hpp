#pragma once

#include "../../states/editor_gizmo.hpp"
#include "../editor_tool.hpp"
#include "../transaction_system.hpp"
#include "../../../shared/brush.hpp"
#include "../../../shared/map.hpp"
#include <optional>
#include <vector>

namespace client
{

struct object_snapshot_t
{
  entity_snapshot_t entity;
  std::optional<shared::geometry_value_t> geometry;
};

class Selection_Tool : public Editor_Tool
{
public:
  void on_enable(editor_context_t& ctx) override;
  void on_disable(editor_context_t& ctx) override;
  void on_update(editor_context_t& ctx, const viewport_state_t &view, float dt) override;

  void on_mouse_down(editor_context_t& ctx, const input::mouse_event_t &e) override;
  void on_mouse_drag(editor_context_t& ctx, const input::mouse_event_t &e) override;
  void on_mouse_up(editor_context_t& ctx, const input::mouse_event_t &e) override;
  void on_key_down(editor_context_t& ctx, const key_event_t &e) override;

  void on_draw_overlay(editor_context_t& ctx,
                       pass_builder_t &draws) override;

  void on_draw_ui(editor_context_t& ctx) override;

private:
  shared::entity_uid_t hovered_uid = 0;
  std::vector<shared::entity_uid_t> selected_uids;

  // Drag box selection
  bool is_dragging_box = false;
  linalg::vec2i drag_start_position;
  linalg::vec2i drag_current_position;

  // Cached viewport for projection in on_draw_ui / selection logic
  viewport_state_t cached_viewport;

  // Grid indication
  bool grid_hover_valid = false;
  linalg::vec3 grid_hover_position;

  // Gizmo. It owns no target of its own -- it is handed a box and reports a
  // transform, and everything below is what applies that transform.
  Editor_Gizmo editor_gizmo;

  // Direct object drag (Ctrl+LMB to move in camera view plane)
  bool         is_dragging_object = false;
  linalg::vec3 drag_plane_hit_start;
  linalg::vec3 drag_plane_normal;

  // The transform every selected object held when the drag opened. BOTH drag
  // styles -- the gizmo and Ctrl+LMB -- measure against this rather than
  // against the previous frame, so neither accumulates rounding and both are
  // idempotent if a frame produces no answer.
  struct drag_origin_t
  {
    shared::entity_uid_t uid = 0;
    linalg::vec3         position{0, 0, 0};
    linalg::quatf        orientation = linalg::quatf::identity();
  };
  std::vector<drag_origin_t>                        drag_origins;
  std::map<shared::entity_uid_t, object_snapshot_t> drag_start_snapshots;

  // Snapshot / commit for a multi-object drag, regime-agnostic at the call site.
  void capture_drag_snapshots(editor_context_t& ctx);
  void commit_drag_snapshots(editor_context_t& ctx);

  void apply_gizmo_drag(editor_context_t& ctx, const gizmo_drag_t &drag);

  // The panel's buttons go through apply_gizmo_drag too, wrapped in their own
  // snapshot/commit. Sharing the application path is what stops a typed offset
  // and a dragged one meaning different things.
  void apply_transform_as_one_edit(editor_context_t& ctx, const gizmo_drag_t &transform);
  void draw_multi_selection_panel(editor_context_t& ctx);

  // What the panel's offset fields hold. Not applied until Apply is pressed:
  // an edit-per-keystroke would push a transaction per digit typed.
  linalg::vec3 panel_offset{0, 0, 0};

  // --- Clipboard, and the pending paste ---------------------------------------
  //
  // Two keys, not one: Ctrl+C fills the clipboard, Ctrl+V opens a pending paste
  // that follows the cursor until LMB commits it or Escape drops it. Splitting
  // them is what makes a cancelled paste cost nothing and one copy stampable
  // repeatedly. The clipboard outlives the paste and the tool switch; the
  // pending paste does not.
  //
  // An entry holds one regime or the other, plus where it sat relative to the
  // clipboard's anchor. Only the ANCHOR meets the grid at paste time -- snapping
  // each member on its own would deform the arrangement that was copied, which
  // is usually the reason it was copied.
  struct clipboard_entry_t
  {
    std::optional<shared::geometry_value_t> geometry;
    entity_snapshot_t                       entity;
    linalg::vec3                            offset_from_anchor{0, 0, 0};

    // A brush's ghost is its hull, and building one is O(n^4) in the point
    // count. A clipboard entry never changes, so the hull is built once here at
    // copy time rather than per brush per frame for the whole life of a paste.
    std::optional<shared::brush_polyhedron_t> brush_hull;
  };

  std::vector<clipboard_entry_t> clipboard;

  // The copied group's low corner, relative to its anchor. Paste puts THAT
  // corner on a grid line, which is the rule compute_geometry_placement_center
  // already follows for a single object.
  linalg::vec3 clipboard_low_corner_offset{0, 0, 0};

  bool         paste_is_pending   = false;
  bool         paste_anchor_valid = false;
  // Bottom-centre of where the group would land, already grid-aligned. Written
  // once per frame in on_update; the overlay and the commit both read it, so
  // what you see and what gets stored cannot disagree.
  linalg::vec3 paste_anchor{0, 0, 0};

  void copy_selection_to_clipboard(editor_context_t& ctx);
  void begin_paste();
  void cancel_paste();
  void commit_paste(editor_context_t& ctx);

  [[nodiscard]] gizmo_view_t make_gizmo_view() const;

  // World bounds of the whole selection. Empty when nothing is selected -- the
  // union of no boxes is not a box at the origin, and three call sites would
  // otherwise each have to remember that.
  [[nodiscard]] std::optional<shared::aabb_bounds_t>
  try_compute_selection_bounds(editor_context_t& ctx) const;
};

} // namespace client
