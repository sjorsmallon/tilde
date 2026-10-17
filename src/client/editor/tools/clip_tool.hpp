#pragma once

#include "../../../shared/map/brush/brush_split.hpp"
#include "../../../shared/map/map.hpp"
#include "../editor_tool.hpp"

#include <optional>
#include <vector>

namespace client
{

// Cut one brush by a plane. The first click picks the brush and the face the
// cap inherits; two points plus the view direction at the second click define
// the plane, a third point replaces that with the plane through all three.
// Both halves draw tinted, Tab cycles what is kept, Enter commits one
// transaction replacing the brush by one or two. geometry_def.md Track G.
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

  void on_draw_overlay(editor_context_t &ctx, pass_builder_t &draws) override;
  void on_draw_ui(editor_context_t &ctx) override;

private:
  enum class Keep
  {
    Both,
    Front,
    Back
  };

  shared::entity_uid_t target_uid = shared::null_entity_uid;
  Plane cap_plane{};
  std::vector<linalg::vec3> points;
  std::optional<Plane> cut_plane;
  Keep keep = Keep::Both;

  std::optional<shared::brush_split_t> preview;
  std::optional<shared::geometry_value_t> previewed_value;

  viewport_state_t cached_view{};

  void clear(editor_context_t &ctx);
  void add_point(editor_context_t &ctx, const linalg::vec3 &point);
  void rebuild_preview(editor_context_t &ctx);
  void commit(editor_context_t &ctx);
};

} // namespace client
