#pragma once

#include "../../../shared/map/brush/brush.hpp"
#include "../../../shared/map/brush/primitive_recipe.hpp"
#include "../../../shared/map/map_geometry.hpp"
#include "../../../shared/map/map_group.hpp"
#include "../editor_tool.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace client
{

// Drop a primitive as plain brushes: a prism (one brush), an arch (one wedge
// brush per segment), a straight flight (one box per step down to the base) or
// a spiral (one slab per step). The panel edits a primitive_recipe_t, R turns
// it a quarter turn about world up, the ghost rests on the cursor hit with its
// low corner on the grid, and a click lands every brush, a lone prism included,
// in one transaction and one group carrying the recipe.
//
// A curve wall is DRAWN instead of dropped: every click adds a control point
// at the hit, right click takes the last one back, Enter places the wall and
// Escape cancels it. The ghost is the wall through the clicked points and the
// point under the cursor, standing where it will land; the recipe takes the
// points relative to the wall's lowest corner. A piece that does not hull (a
// leg climbing straight up) is dropped from the wall and counted in the panel.
// While a curve is in progress a click on a placed primitive adds a point too.
//
// A click on a brush of a recipe group EDITS that group instead: the panel
// shows its recipe, every change regenerates the members in place (the
// group's low corner stays, uids are kept where the count allows), and the
// edit lands as one transaction when the widget is released. Escape, or a
// click away, returns to placing. The map is the truth: an undo that changes
// the group's recipe is adopted by the panel. An edited curve wall shows its
// control points as handles: a drag moves one on the floor plane through it,
// with Shift straight up and down, snapped to the grid, and the wall follows
// the drag until the button is released. geometry_def.md Track I.
class Primitive_Tool : public Editor_Tool
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
  shared::primitive_recipe_t settings;
  shared::primitive_recipe_t built_settings;

  // The members' values and the group list as they were when the current
  // edit began; the transaction is the difference to what the map holds when
  // the widget is released.
  struct edit_session_t
  {
    std::vector<std::pair<shared::entity_uid_t, shared::geometry_value_t>> members_before;
    std::vector<shared::map_group_t> groups_before;
  };

  std::optional<shared::entity_uid_t> edited_group;
  std::optional<shared::entity_uid_t> hovered_recipe_group;
  shared::primitive_recipe_t applied_settings;
  std::optional<edit_session_t> edit_session;
  std::vector<shared::entity_uid_t> edited_members;
  linalg::vec3 edit_translation{};

  std::vector<std::vector<linalg::vec3>> point_sets;
  std::vector<shared::brush_polyhedron_t> hulls;
  shared::aabb_bounds_t bounds{};
  bool built = false;
  int flat_curve_pieces = 0;

  // The curve wall being drawn: its control points as clicked, in the world.
  std::vector<linalg::vec3> curve_points_in_progress;

  bool cursor_is_over_surface = false;
  linalg::vec3 ghost_position{};

  // The edited curve's control points as drag handles: `edit_translation` and
  // the recipe's turns take a stored point into the world and back. A drag
  // pins the wall's translation, so the other points stay where they are and
  // the dragged one lands under the cursor; the corner rule resumes after.
  viewport_state_t cached_view{};
  std::optional<size_t> hovered_curve_point;
  std::optional<size_t> dragged_curve_point;
  std::optional<linalg::vec3> pinned_translation;

  bool is_drawing_curve() const;
  bool is_editing_curve() const;
  void add_curve_point(const linalg::vec3 &point);
  void remove_last_curve_point();
  void cancel_curve();
  std::vector<linalg::vec3> edited_curve_points_in_world() const;
  std::optional<size_t> try_pick_curve_handle(const linalg::vec2 &screen_position) const;
  void drag_curve_point(const editor_context_t &ctx, const input::mouse_event_t &e);
  void end_curve_drag();
  void rebuild_if_changed();
  linalg::vec3 compute_translation(const editor_context_t &ctx) const;
  void place(editor_context_t &ctx);

  std::optional<shared::entity_uid_t>
  try_pick_recipe_group(const editor_context_t &ctx, const viewport_state_t &view) const;
  void begin_edit(editor_context_t &ctx, shared::entity_uid_t group_uid);
  void end_edit(editor_context_t &ctx);
  void regenerate(editor_context_t &ctx);
  void commit_edit(editor_context_t &ctx);
};

} // namespace client
