#pragma once

#include "../../../shared/map/map.hpp"
#include "../../../shared/map/mesh/mesh_split.hpp"
#include "../editor_tool.hpp"

#include <optional>
#include <vector>

namespace client
{

// Cut one mesh by a plane: the split of geometry_def.md §11.8 increment 2,
// driven by hand. The first click picks the mesh and the face the caps
// inherit; two points plus the view direction at the second click define the
// plane, a third point replaces that with the plane through all three. Both
// halves draw tinted, Tab cycles what is kept, Enter commits one transaction
// replacing the mesh by the shells of the kept halves.
class Clip_Tool : public Editor_Tool
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
  void on_draw_inspector(editor_context_t &ctx) override;

private:
  enum class Keep
  {
    Both,
    Front,
    Back
  };

  shared::entity_uid_t target_uid = shared::null_entity_uid;
  std::optional<uint32_t> cap_face_index;
  std::vector<linalg::vec3> points;
  std::optional<Plane> cut_plane;
  Keep keep = Keep::Both;

  std::optional<shared::mesh_split_t> preview;
  std::optional<shared::mesh_geometry_t> previewed_value;

  viewport_state_t cached_view{};

  void clear();
  void add_point(editor_context_t &ctx, const linalg::vec3 &point);
  void rebuild_cut_plane();
  void rebuild_preview(editor_context_t &ctx);
  void commit(editor_context_t &ctx);
};

} // namespace client
