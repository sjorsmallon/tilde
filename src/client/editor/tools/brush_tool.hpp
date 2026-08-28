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


  struct potential_selection_t
  {
    shared::entity_uid_t uid = shared::invalid_entity_uid;
    std::optional<linalg::vec3> face_normal;
    std::vector<linalg::vec3> points;
  };
  
  struct selection_t
  {
    std::optional<shared::brush_polyhedron_t> hull;
    int face = INVALID_FACE;
    std::vector<linalg::vec3> vertex_handles;
  };

  struct hover_t
  {
    shared::entity_uid_t uid = shared::invalid_entity_uid;
    std::optional<linalg::vec3> face_normal;
    int face = INVALID_FACE;
  };

  struct axis_drag_t
  {
    linalg::vec3 axis{0, 0, 0};
    linalg::vec3 anchor{0, 0, 0};
    float start_distance = 0.0f;
  };

  struct band_t
  {
    bool armed = false;
    bool active = false;
    linalg::vec2 start{0, 0};
    linalg::vec2 end{0, 0};
    bool adds = false; // ctrl held when the band was armed

    // What the press was over. A band arms from ANYWHERE, so which face the
    // press landed on is only acted on if the cursor never travels -- and by
    // then `hover` has moved on, which is why the press records its own.
    shared::entity_uid_t        press_uid = shared::invalid_entity_uid;
    std::optional<linalg::vec3> press_face_normal;
  };

  struct gestures_t
  {
    bool dragging_face = false;
    bool dragging_vertices = false;
    std::optional<shared::geometry_value_t> start_geometry;
    std::vector<linalg::vec3> vertex_start_points;
    band_t band;
  };

  struct extrusion_t
  {
    bool pending = false;
    bool dragging = false;
    float depth = 0.0f;
  };

  Mode mode = Mode::Face;
  potential_selection_t potential_selection;
  selection_t selection;
  hover_t hover;
  axis_drag_t axis_drag;
  gestures_t gestures;
  extrusion_t extrusion;

  // This frame. Written at the top of on_update, read by handlers that run
  // later in it.
  viewport_state_t cached_view;

  shared::brush_geometry_t* try_get_selected_brush(editor_context_t &ctx);

  void cancel_in_progress_gestures();

  // one grid step along `direction`, applied to the whole brush.
  void nudge_selected_brush(editor_context_t &ctx,
                            const linalg::vec3 &direction);

  void delete_selected_brush(editor_context_t &ctx);

  // An armed band whose cursor never travelled was a click after all. This is
  // what it would have meant on press.
  void resolve_band_press_as_click();

  void rebuild_hull_and_handles(editor_context_t &ctx);
  void clear_point_selection();
  void cancel_pending_extrusion();
  void commit_pending_extrusion(editor_context_t &ctx);

  bool try_rebuild_selected_brush(editor_context_t &ctx,
                                 std::vector<linalg::vec3> vertices);

  struct pending_extrusion_solids_t
  {
    std::vector<std::vector<linalg::vec3>> point_sets;
    bool every_pick_accounted_for = false;
  };

  pending_extrusion_solids_t build_pending_extrusion_solids(float grid_step) const;

  float grid_step_for(const editor_context_t &ctx,
                      const input::modifiers_t &mods) const;

  int try_pick_vertex_handle(const linalg::vec2 &screen_position) const;
  bool point_is_selected(const linalg::vec3 &point) const;
  void toggle_point_selection(const linalg::vec3 &point, bool additive);
};

} // namespace client
