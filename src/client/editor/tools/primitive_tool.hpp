#pragma once

#include "../../../shared/map/map_geometry.hpp"
#include "../../../shared/map/mesh/primitive_recipe.hpp"
#include "../editor_tool.hpp"

#include <optional>
#include <vector>

namespace client
{

// Place a primitive as ONE recipe mesh: a prism, an arch, a straight flight,
// a spiral or a curve wall (geometry_def.md §11.8 increment 10). The panel
// edits a primitive_recipe_t, R turns it a quarter turn about world up, the
// ghost rests on the cursor hit with its low corner on the grid, and a click
// lands the mesh in one transaction with the recipe as its source.
//
// A curve wall is DRAWN instead of dropped: every click adds a control point
// at the hit, right click takes the last one back, Enter places the wall and
// Escape cancels it. The ghost is the wall through the clicked points and the
// point under the cursor, standing where it will land; at placement the
// points are rebased to the wall's lowest corner and the pose takes the
// corner. While a curve is in progress a click on a placed primitive adds a
// point too.
//
// A click on a recipe mesh EDITS it instead: the panel shows its recipe,
// every change regenerates the mesh in place (the pose stays, so does each
// face's surface by index), and the edit lands as one whole-value swap when
// the widget is released. Escape, or a click away, returns to placing. The
// map is the truth: an undo that changes the mesh's recipe is adopted by the
// panel. An edited curve wall shows its control points as handles: a drag
// moves one on the floor plane through it, with Shift straight up and down,
// snapped to the grid; the other points stay where they are because the
// points are the mesh's local frame, and on release they are rebased to the
// new corner with the pose moved to match, which moves nothing.
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
  shared::mesh_geometry_t built_mesh;
  bool built = false;

  // The edited mesh and its value when the current edit began; the
  // transaction is the difference to what the map holds when the widget is
  // released.
  std::optional<shared::entity_uid_t> edited_uid;
  std::vector<shared::entity_uid_t> edited_as_selection;
  std::optional<shared::entity_uid_t> hovered_recipe_uid;
  shared::primitive_recipe_t applied_settings;
  std::optional<shared::geometry_value_t> value_before_edit;

  // The curve wall being drawn: its control points as clicked, in the world.
  std::vector<linalg::vec3> curve_points_in_progress;

  bool cursor_is_over_surface = false;
  linalg::vec3 ghost_position{};

  viewport_state_t cached_view{};
  std::optional<size_t> hovered_curve_point;
  std::optional<size_t> dragged_curve_point;

  bool is_drawing_curve() const;
  bool is_editing_curve() const;
  void add_curve_point(const linalg::vec3 &point);
  void remove_last_curve_point();
  void cancel_curve();
  const shared::mesh_geometry_t *try_edited_mesh(const editor_context_t &ctx) const;
  std::vector<linalg::vec3> edited_curve_points_in_world(const editor_context_t &ctx) const;
  std::optional<size_t> try_pick_curve_handle(const editor_context_t &ctx,
                                              const linalg::vec2 &screen_position) const;
  void drag_curve_point(const editor_context_t &ctx, const input::mouse_event_t &e);
  void end_curve_drag(editor_context_t &ctx);
  void rebuild_if_changed();
  linalg::vec3 compute_placement_position(const editor_context_t &ctx) const;
  void place(editor_context_t &ctx);

  std::optional<shared::entity_uid_t>
  try_pick_recipe_mesh(const editor_context_t &ctx, const viewport_state_t &view) const;
  void begin_edit(editor_context_t &ctx, shared::entity_uid_t uid);
  void end_edit(editor_context_t &ctx);
  void regenerate(editor_context_t &ctx);
  void commit_edit(editor_context_t &ctx);
};

} // namespace client
