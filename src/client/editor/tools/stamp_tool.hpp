#pragma once

#include "../../../shared/map/brush/brush.hpp"
#include "../editor_tool.hpp"

#include <vector>

namespace client
{

// Drop a primitive as plain brushes: a prism (one brush) or an arch (one wedge
// brush per segment). The panel picks the kind and its numbers, the ghost
// rests on the cursor hit with its low corner on the grid, and a click lands
// every brush in one transaction and one group. No link back, exactly as a
// placed prefab. geometry_def.md Track I.
class Stamp_Tool : public Editor_Tool
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
  enum class Kind
  {
    Prism,
    Arch
  };

  struct stamp_settings_t
  {
    Kind kind = Kind::Prism;
    int axis_index = 1;
    int prism_sides = 16;
    float prism_radius = 64.f;
    float prism_half_length = 64.f;
    int arch_segments = 8;
    float arch_inner_radius = 128.f;
    float arch_thickness = 32.f;
    float arch_half_depth = 32.f;

    bool operator==(const stamp_settings_t &) const = default;
  };

  stamp_settings_t settings;
  stamp_settings_t built_settings;

  std::vector<std::vector<linalg::vec3>> point_sets;
  std::vector<shared::brush_polyhedron_t> hulls;
  shared::aabb_bounds_t bounds{};

  bool cursor_is_over_surface = false;
  linalg::vec3 ghost_position{};

  void rebuild_if_changed();
  linalg::vec3 compute_translation(const editor_context_t &ctx) const;
  void place(editor_context_t &ctx);
};

} // namespace client
