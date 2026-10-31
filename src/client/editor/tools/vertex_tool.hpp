#pragma once

#include "../../../shared/map/map.hpp"
#include "../../../shared/map/map_geometry_mesh.hpp"
#include "../editor_tool.hpp"
#include "../transaction_system.hpp"

#include <optional>
#include <vector>

namespace client
{

// Vertex and edge editing of a mesh (geometry_def.md §11.8 increment 12). The
// selection is a set of the mesh's STORED vertices; Vertex, Edge and Face mode
// only decide what a click adds to it (one vertex, an edge's two, a face's
// ring). A drag moves the set live, in the axis-aligned plane facing the
// camera (Vertex and Edge mode), along the world up with shift, or along the
// face normal (Face mode), snapped to the grid; the release keeps the move only
// when the mesh is still closed and planar, and refuses it by name otherwise.
// A moved recipe mesh freezes to inline.
class Vertex_Tool : public Editor_Tool
{
public:
  void on_enable(editor_context_t &ctx) override;
  void on_disable(editor_context_t &ctx) override;
  void on_update(editor_context_t &ctx, const viewport_state_t &view, float dt) override;

  void on_mouse_down(editor_context_t &ctx, const input::mouse_event_t &e) override;
  void on_mouse_drag(editor_context_t &ctx, const input::mouse_event_t &e) override;
  void on_mouse_up(editor_context_t &ctx, const input::mouse_event_t &e) override;
  void on_key_down(editor_context_t &ctx, const key_event_t &e) override;

  void on_draw_overlay(editor_context_t &ctx, draw_lists_t &draws) override;
  void on_draw_ui(editor_context_t &ctx) override;
  void on_draw_inspector(editor_context_t &ctx) override;

  Span<const shared::entity_uid_t> selected_objects() const override;

private:
  enum class Mode
  {
    Vertex,
    Edge,
    Face
  };

  struct edge_ref_t
  {
    uint32_t a = 0;
    uint32_t b = 0;
  };

  // What the cursor rests on: a mesh of the map, and on the SELECTED mesh the
  // element the mode picks.
  struct hover_t
  {
    shared::entity_uid_t uid = shared::null_entity_uid;
    std::optional<uint32_t> vertex;
    std::optional<edge_ref_t> edge;
    std::optional<uint32_t> face;
    linalg::vec3 face_point{0, 0, 0};
  };

  enum class Drag
  {
    None,
    Band_Armed,
    Band_Sizing,
    Move
  };

  struct band_t
  {
    linalg::vec2 start{0, 0};
    linalg::vec2 end{0, 0};
    bool adds = false;
    shared::entity_uid_t press_uid = shared::null_entity_uid;
  };

  struct move_t
  {
    linalg::vec3 anchor{0, 0, 0};
    bool along_axis = false;
    linalg::vec3 axis{0, 1, 0};
    float start_distance = 0.f;
    linalg::vec3 plane_normal{0, 1, 0};
    linalg::vec3 start_plane_point{0, 0, 0};
    linalg::vec3 world_delta{0, 0, 0};
    std::optional<shared::mesh_geometry_t> geometry_at_start;
    std::vector<linalg::vec3> vertices_at_start;
  };

  struct drag_t
  {
    Drag kind = Drag::None;
    band_t band;
    move_t move;
  };

  Mode mode = Mode::Vertex;
  shared::entity_uid_t selected_uid = shared::null_entity_uid;
  std::vector<uint32_t> selected_vertices;
  std::vector<shared::entity_uid_t> selected_uids;
  hover_t hover;
  drag_t drag;
  std::optional<shared::mesh_defect_t> live_defect;
  viewport_state_t cached_view;

  shared::mesh_geometry_t *try_get_mesh(editor_context_t &ctx, shared::entity_uid_t uid);
  shared::mesh_geometry_t *try_get_selected_mesh(editor_context_t &ctx);

  void select_mesh(shared::entity_uid_t uid);
  bool vertex_is_selected(uint32_t vertex) const;
  void add_vertex_to_selection(uint32_t vertex);
  void toggle_vertices(Span<const uint32_t> vertices);
  std::vector<uint32_t> hovered_element_vertices(const shared::mesh_geometry_t &mesh) const;

  void update_hover(editor_context_t &ctx);
  void begin_move(shared::mesh_geometry_t &mesh, const linalg::vec3 &anchor,
                  const input::modifiers_t &mods);
  void update_move(editor_context_t &ctx, float grid_step);
  void end_move(editor_context_t &ctx);
  void cancel_move(editor_context_t &ctx);
  void resolve_band_press_as_click();

  // Write `local_positions` to the selected vertices as one undoable edit, or
  // refuse it by name when the mesh would no longer be a solid.
  void edit_selected_vertices(editor_context_t &ctx, const char *name,
                              Span<const linalg::vec3> local_positions);

  void draw_handles_ui();
};

} // namespace client
