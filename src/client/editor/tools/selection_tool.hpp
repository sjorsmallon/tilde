#pragma once

#include "../../states/editor_gizmo.hpp"
#include "../connection_panel.hpp"
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

  Span<const shared::entity_uid_t> selected_objects() const override { return selected_uids; }

private:
  void draw_light_bake_status(const editor_context_t& ctx, shared::entity_uid_t uid,
                              const entities::Entity& entity);
  void draw_reflection_volume_status(const editor_context_t& ctx,
                                     const entities::Entity& entity);

  // The last "explain reach" probe, kept so the lines stay up while the author
  // reads them; keyed by the light it was run for.
  shared::entity_uid_t light_reach_uid = 0;
  std::vector<std::string> light_reach_lines;
  shared::entity_uid_t hovered_uid = 0;
  std::vector<shared::entity_uid_t> selected_uids;

  // "Target by click": the Connections panel arms it, the next viewport click
  // resolves the hovered uid into the row and is SWALLOWED -- letting it through
  // would reselect, and the panel the author was editing would be gone before
  // the target landed in it.
  connection_pick_t connection_pick;

  // A press that MEANT something other than selecting, so its release must not
  // fall through to the selection branch. Two gestures set it: the connection
  // pick above, and the paste commit -- which hands the stamped copies to
  // selected_uids, and a release landing on nothing would clear them again.
  bool click_consumed_by_gesture = false;


  // While a pick is armed the RAY is not the answer. A point light has no
  // Render mesh, so compute_entity_bounds gives it a POINT, and a point is
  // sub-pixel at any distance -- which is what made clicking one finicky. This
  // is screen-space instead: the nearest entity ANCHOR within a radius of the
  // cursor, with a real BVH hit winning outright because that one is
  // unambiguous. Deliberately scoped to the pick and not to ordinary selection,
  // where a generous radius would mean grabbing a light you were not aiming at
  // while you were modelling something else.
  [[nodiscard]] std::optional<shared::entity_uid_t>
  try_pick_entity_near_cursor(const editor_context_t &ctx, linalg::vec2 cursor) const;

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

  // Drop the whole selection onto whatever is under it (End, or the inspector
  // button). One transform through apply_transform_as_one_edit, so a group
  // keeps its arrangement and one Ctrl+Z takes it back.
  void snap_selection_to_surface_below(editor_context_t& ctx);
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
  // THE CLIPBOARD IS A MAP -- a fragment whose anchor is its own origin, which
  // is exactly what a prefab file holds (prefab_def.md). Copy is
  // extract_map_subset and paste is stamp_map, so Ctrl+C carries the
  // CONNECTIONS between the copied objects, which the per-object clipboard this
  // replaced silently dropped. Only the ANCHOR meets the grid at paste time --
  // snapping each member on its own would deform the arrangement that was
  // copied, which is usually the reason it was copied.
  std::optional<shared::map_t> clipboard;

  // A brush's ghost is its hull, and building one is O(n^4) in the point count.
  // The clipboard never changes, so the hulls are built once at copy time
  // rather than per brush per frame for the whole life of a paste. Parallel to
  // clipboard->geometry; the entry is empty for anything that is not a brush.
  std::vector<std::optional<shared::brush_polyhedron_t>> clipboard_brush_hulls;

  // Rows the copy could not take, because one of their ends was not selected.
  // Reported once at Ctrl+C: the same loss "Save as prefab" warns about, and
  // the same walk decides both.
  size_t clipboard_crossing_count = 0;

  // --- Save as prefab ----------------------------------------------------------
  //
  // A name the author types and nothing else: a prefab's identity IS its
  // filename (prefab_def.md), so there is no name field inside the file to keep
  // in step with it.
  Array<char, 96> prefab_name;
  bool            prefab_overwrite = false;
  // What the last write said, kept so the answer survives the popup closing.
  std::string prefab_status;

  void draw_prefab_save_popup(editor_context_t& ctx);

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

  // The one way the clipboard is filled, whatever produced the fragment: a
  // copied selection or a prefab off disk. Builds the ghost hulls and the low
  // corner the paste snaps by, so those cannot be forgotten at a second site.
  void adopt_clipboard(shared::map_t fragment, size_t crossing_count);
  void copy_selection_to_clipboard(editor_context_t& ctx);
  void begin_paste();
  void cancel_paste();
  void commit_paste(editor_context_t& ctx);

  // The group the next paste of this clipboard becomes, named after the prefab
  // it was loaded from; empty for a copied selection, which pastes loose.
  std::string clipboard_group_name;

  // --- Groups ------------------------------------------------------------------
  //
  // A group is map data (map_group.hpp); what the tool adds is that a pick on a
  // member takes the members, and the three edits, each one transaction:
  // Ctrl+G groups the selection, Ctrl+Shift+G dissolves every group the
  // selection touches, and the outliner's Ungroup names one group.
  void group_selection(editor_context_t& ctx);
  void ungroup_selection(editor_context_t& ctx);
  void ungroup_by_uid(editor_context_t& ctx, shared::entity_uid_t group_uid);
  void select_group(editor_context_t& ctx, shared::entity_uid_t group_uid);

  // Arms the pick for the next group of the stamp's unbound rows, selecting
  // that group's sender so the panel being filled is the one on screen. Does
  // nothing when none are queued. Called after a stamp and after each resolve.
  void arm_next_unbound_pick(editor_context_t& ctx);

  // The nearest visible entity whose ICON is within `radius` pixels of the
  // cursor, if any. Pure proximity -- no ray, no BVH -- because an entity icon
  // is a few pixels wide and a ray that misses it is not evidence the author
  // meant something else.
  [[nodiscard]] std::optional<shared::entity_uid_t>
  try_pick_entity_within(const editor_context_t& ctx, linalg::vec2 cursor, float radius) const;

  [[nodiscard]] gizmo_view_t make_gizmo_view() const;

  // World bounds of the whole selection. Empty when nothing is selected -- the
  // union of no boxes is not a box at the origin, and three call sites would
  // otherwise each have to remember that.
  [[nodiscard]] std::optional<shared::aabb_bounds_t>
  try_compute_selection_bounds(editor_context_t& ctx) const;
};

} // namespace client
