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
  entity_snapshot_t entity{};
  std::optional<shared::geometry_value_t> geometry{};
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
  void on_draw_inspector(editor_context_t& ctx) override;
  void draw_selection_fields(editor_context_t& ctx);

  Span<const shared::entity_uid_t> selected_objects() const override { return selected_uids; }

private:

shared::entity_uid_t hovered_uid = 0;
  std::vector<shared::entity_uid_t> selected_uids{};

  // Cached viewport for projection in on_draw_ui / selection logic
  viewport_state_t cached_viewport{};

  Editor_Gizmo editor_gizmo{};

  // position on the work plane if no entity is there.
  std::optional<linalg::vec3> grid_hover{};

  // where LMB went down.
  linalg::vec2i press_position{};

  // Selection happens on the release, so the release of a spent press must not select.
  bool press_was_spent_on_pick_or_paste = false;

  // if multiple entities occupy the same space or bounds, allows click through.
  struct click_cycle_entry_t
  {
    shared::entity_uid_t uid = 0;
    std::vector<shared::entity_uid_t> members{};
  };
  linalg::vec2i last_plain_click_position = {-1000000, -1000000};

  // allows through select of a group member. armed by ctrl+W.
  bool ignoring_groups = false;

  // --- Armed picks: the next click answers a question --------------------------

  // this is armed by the connections panel.
  uid_pick_t uid_pick{};

  // Ctrl+P arms it: the next viewport click names an entity and every selected ENTITY takes its
  // position (Ctrl+Shift+P: its orientation too).
  struct snap_pick_t
  {
    bool armed = false;
    bool with_orientation = false;
  };
  snap_pick_t snap_pick{};

  // --- Drags -------------------------------------------------------------------

  struct box_drag_t
  {
    bool active = false;
    linalg::vec2i current_position{};
  };
  box_drag_t box_drag{};

  // Direct object drag (Ctrl+LMB to move in camera view plane)
  struct object_drag_t
  {
    bool active = false;
    bool left_click_radius = false;
    linalg::vec3 plane_hit_start{};
    linalg::vec3 plane_normal{};
  };
  object_drag_t object_drag{};

  // The transform every selected object held when the drag opened. BOTH drag
  // styles -- the gizmo and Ctrl+LMB -- measure against this rather than
  // against the previous frame, so neither accumulates rounding and both are
  // idempotent if a frame produces no answer.
  struct drag_origin_t
  {
    shared::entity_uid_t uid = 0;
    linalg::vec3 position{0, 0, 0};
    linalg::quatf orientation = linalg::quatf::identity();
  };
  struct drag_baseline_t
  {
    std::vector<drag_origin_t> origins{};
    std::map<shared::entity_uid_t, object_snapshot_t> snapshots{};

    void clear()
    {
      origins.clear();
      snapshots.clear();
    }
  };
  drag_baseline_t drag_baseline{};

  // clipboard.
  struct clipboard_t
  {
    // the clipboard is a map because a subset of a map is still a map.
    // there was a lot of back-and-forth about what the simplest thing to do is,
    // and it's this.
    std::optional<shared::map_t> piece{};

    // A brush's ghost is its hull, and building one is O(n^4) in the point count.
    // The clipboard never changes, so the hulls are built once at copy time
    // rather than per brush per frame for the whole life of a paste. Parallel to
    // piece->geometry; the entry is empty for anything that is not a brush.
    std::vector<std::optional<shared::brush_polyhedron_t>> brush_hulls{};

    // this mentions the connections that have an endpoint outside the selection.
    size_t outside_end_count = 0;

    // The copied group's low corner, relative to its anchor. Paste puts THAT
    // corner on a grid line, which is the rule compute_geometry_placement_center
    // already follows for a single object.
    linalg::vec3 low_corner_offset{0, 0, 0};

    // The group the next paste of this clipboard becomes, named after the prefab
    // it was loaded from; empty for a copied selection, which pastes loose.
    std::string group_name{};
  };
  clipboard_t clipboard{};

  struct paste_t
  {
    bool pending = false;
    // Bottom-centre of where the group would land, already grid-aligned. Written
    // once per frame in on_update; the overlay and the commit both read it, so
    // what you see and what gets stored cannot disagree. Empty while the cursor
    // is on nothing placeable.
    std::optional<linalg::vec3> anchor{};
  };
  paste_t paste{};

  // --- Panels ------------------------------------------------------------------

  // The inspector's open edit. `before` is re-seeded every frame nothing is
  // pending, so an undo or a gizmo drag is never mistaken for one; a widget
  // held across frames commits as ONE transaction when ImGui lets go of it.
  struct inspector_edit_t
  {
    std::vector<shared::entity_uid_t> uids{};
    std::vector<std::shared_ptr<entities::Entity>> before;
    std::string field{};
    bool pending = false;
  };
  inspector_edit_t inspector_edit{};

  // The inspector's group name field, re-seeded from the map while it is not
  // being typed in.
  struct group_name_edit_t
  {
    shared::entity_uid_t group_uid = shared::null_entity_uid;
    Array<char, 96>      name;
    bool                 active = false;
  };
  group_name_edit_t group_name_edit;

  // What the panel's offset fields hold. Not applied until Apply is pressed:
  // an edit-per-keystroke would push a transaction per digit typed.
  linalg::vec3 panel_offset{0, 0, 0};

  // The last "explain reach" probe, kept so the lines stay up while the author
  // reads them; keyed by the light it was run for.
  struct light_reach_t
  {
    shared::entity_uid_t uid = 0;
    std::vector<std::string> lines{};
  };
  light_reach_t light_reach{};

  // A name the author types and nothing else: a prefab's identity IS its
  // filename (prefab_def.md), so there is no name field inside the file to keep
  // in step with it.
  struct prefab_save_popup_t
  {
    Array<char, 96> name{};
    bool overwrite = false;
    // What the last write said, kept so the answer survives the popup closing.
    std::string status;
  };
  prefab_save_popup_t prefab_save{};

  // =============================================================================
  // Functions
  // =============================================================================

  // --- Picking -----------------------------------------------------------------

  [[nodiscard]] std::vector<click_cycle_entry_t> collect_click_cycle(const editor_context_t& ctx) const;

  [[nodiscard]] std::optional<shared::entity_uid_t>
  try_pick_entity_near_cursor(const editor_context_t &ctx, linalg::vec2 cursor) const;

  // The nearest visible entity whose ICON is within `radius` pixels of the
  // cursor, if any. Pure proximity -- no ray, no BVH -- because an entity icon
  // is a few pixels wide and a ray that misses it is not evidence the author
  // meant something else.
  [[nodiscard]] std::optional<shared::entity_uid_t>
  try_pick_entity_within(const editor_context_t& ctx, linalg::vec2 cursor, float radius) const;

  void append_pick(const editor_context_t& ctx, shared::entity_uid_t uid,
                   std::vector<shared::entity_uid_t>& out) const;

  // The ctrl/shift click: all of `picked` selected takes them out, else the
  // rest come in. The viewport and the outliner both.
  void toggle_in_selection(Span<const shared::entity_uid_t> picked);

  void snap_selected_entities_onto(editor_context_t& ctx, shared::entity_uid_t target_uid,
                                   bool with_orientation);

  void commit_picked_field_uid(editor_context_t& ctx, const field_pick_target_t& target,
                               shared::entity_uid_t picked);

  // Arms the pick for the next group of the stamp's unbound rows, selecting
  // that group's sender so the panel being filled is the one on screen. Does
  // nothing when none are queued. Called after a paste and after each resolve.
  void arm_next_unbound_pick(editor_context_t& ctx);

  // --- Transforms --------------------------------------------------------------

  [[nodiscard]] gizmo_view_t make_gizmo_view() const;

  // World bounds of the whole selection. Empty when nothing is selected -- the
  // union of no boxes is not a box at the origin, and three call sites would
  // otherwise each have to remember that.
  [[nodiscard]] std::optional<shared::aabb_bounds_t>
  try_compute_selection_bounds(editor_context_t& ctx) const;

  void capture_drag_snapshots(editor_context_t& ctx);
  void commit_drag_snapshots(editor_context_t& ctx, std::string name);

  void apply_gizmo_drag(editor_context_t& ctx, const gizmo_drag_t &drag);

  // the panel buttons go through apply_gizmo_drag too, wrapped in their own
  // snapshot/commit.
  void apply_transform_as_one_edit(editor_context_t& ctx, const gizmo_drag_t &transform,
                                   std::string name);

  // Drop the whole selection onto whatever is under it (End, or the inspector
  // button). One transform through apply_transform_as_one_edit, so a group
  // keeps its arrangement and one Ctrl+Z takes it back.
  void snap_selection_to_surface_below(editor_context_t& ctx);

  // --- Clipboard and paste -----------------------------------------------------

  // The one way the clipboard is filled, whatever produced the piece: a
  // copied selection or a prefab off disk. Builds the ghost hulls and the low
  // corner the paste snaps by, so those cannot be forgotten at a second site.
  void adopt_clipboard(shared::map_t piece, size_t outside_end_count);
  void copy_selection_to_clipboard(editor_context_t& ctx);
  void begin_paste();
  void cancel_paste();
  void commit_paste(editor_context_t& ctx);

  // --- Panels ------------------------------------------------------------------

  void draw_light_bake_status(const editor_context_t& ctx, shared::entity_uid_t uid,
                              const entities::Entity& entity);
  void draw_reflection_volume_status(const editor_context_t& ctx,
                                     const entities::Entity& entity);
  void draw_multi_selection_panel(editor_context_t& ctx);
  void draw_prefab_save_popup(editor_context_t& ctx);

  [[nodiscard]] std::vector<entities::Entity*> collect_inspected_entities(const editor_context_t& ctx) const;
  void seed_inspector_edit(Span<entities::Entity* const> inspected);
  void settle_inspector_edit(editor_context_t& ctx);
  void commit_inspector_edit(editor_context_t& ctx);

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
  void rename_group(editor_context_t& ctx, shared::entity_uid_t group_uid, std::string name);

  // --- The tie ------------------------------------------------------------------
  //
  // Source's "tie to entity" with the storage direction turned around: the
  // brush names its owner and nothing names the brush (prediction_def.md ss4.2).
  // Tying writes owner_uid on every selected object: to the ONE geometry owner
  // in the selection, else to a new owner of a GEOMETRY_OWNER_TYPES type spawned
  // at the selection centroid. Untying clears it, whether the selection is the
  // objects or the entity. One transaction per gesture.
  void tie_selection_to_existing_entity(editor_context_t& ctx);
  void tie_selection_to_new_entity(editor_context_t& ctx, entities::entity_type type);
  void tie_selection_to_owner(editor_context_t& ctx, shared::entity_uid_t owner_uid,
                              transaction_t transaction);
  void collect_selected_geometry_owners(const editor_context_t&            ctx,
                                        std::vector<shared::entity_uid_t>& out) const;
  void untie_selection(editor_context_t& ctx);

  // Every geometry uid `owner` switches, walked on demand. No cached list on
  // the entity: the file stores one direction, so a second one held here is an
  // answer that can disagree with it.
  void collect_owned_geometry(const editor_context_t& ctx, shared::entity_uid_t owner,
                              std::vector<shared::entity_uid_t>& out) const;
};

} // namespace client
