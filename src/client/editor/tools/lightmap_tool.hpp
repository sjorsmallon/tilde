#pragma once

#include "../editor_tool.hpp"
#include "../../../shared/lightmap_bake.hpp"
#include "../../../shared/lightmap_gpu.hpp"
#include "../../../shared/lightmap_probes.hpp"
#include "../../../shared/lightmap_sidecar.hpp"
#include "../../../shared/lightmap_solve.hpp"

#include <optional>
#include <string>
#include <vector>

namespace client
{

class Lightmap_Tool : public Editor_Tool
{
public:
  // What the PANEL opens with, as distinct from the structs' own defaults: a
  // full bake at a texel a unit is what a look wants, while the struct defaults
  // stay the cheap untraced bake 81 sites of lightmap_bake_test pin.
  Lightmap_Tool()
  {
    settings.texels_per_world_unit = 1.f;
    solve_settings.trace_indirect_light = true;
    solve_settings.bake_probes = true;
    solve_settings.bake_reflection_captures = true;
  }

  void on_enable(editor_context_t& ctx) override;
  void on_disable(editor_context_t& ctx) override;
  void on_update(editor_context_t& ctx, const viewport_state_t& view, float dt) override;

  void on_mouse_down(editor_context_t& ctx, const input::mouse_event_t& e) override;
  void on_mouse_drag(editor_context_t& ctx, const input::mouse_event_t& e) override;
  void on_mouse_up(editor_context_t& ctx, const input::mouse_event_t& e) override;
  void on_key_down(editor_context_t& ctx, const key_event_t& e) override;

  void on_draw_overlay(editor_context_t& ctx, pass_builder_t& draws) override;
  void on_draw_ui(editor_context_t& ctx) override;

private:
  shared::lightmap_bake_settings_t settings;
  shared::lightmap_solve_settings_t solve_settings;

  // Display only: the pages are HDR, so a PNG of one needs an exposure, and a
  // level lit for a dim interior and one lit for daylight are not readable at the
  // same one. Nothing baked depends on it.
  float preview_exposure = 4.f;

  // the bake in progress
  shared::lightmap_t baked;

  // Per-light shadow-ray coverage from the same bake, held only to be looked at:
  // it goes into no sidecar and no atlas yet (lighting_def.md ss14 step 6).
  shared::lightmap_visibility_masks_t visibility_masks;
  bool emit_per_light_visibility = true;

  size_t lit_texel_count = 0;
  size_t indirect_texel_count = 0;

  // Which path the last bake took and why (lightmap_gpu_plan.md step 7b).
  std::string last_bake_line;

  bool show_probe_preview = false;
  std::optional<shared::probe_grid_t> probe_preview_grid;
  std::vector<uint8_t> probe_preview_inside;
  size_t probe_preview_inside_count = 0;
  static constexpr int PROBE_PREVIEW_RADIUS_IN_SPACINGS = 10;
  size_t probe_preview_drawn_count = 0;
  void rebuild_probe_preview(editor_context_t& ctx);

  // Gate 6 step 6: the capture lattice, derived from the same preview grid.
  // Rebuilt by rebuild_probe_preview when shown.
  bool show_capture_preview = false;
  bool show_all_capture_boxes = false;
  shared::reflection_capture_set_t capture_preview;
  shared::reflection_lattice_t capture_preview_lattice;
  shared::reflection_lattice_t baked_capture_lattice;
  size_t capture_preview_open_count = 0;
  size_t capture_preview_overridden_count = 0;
  uint32_t capture_preview_boxes_drawn = 0;
  void draw_capture_preview(pass_builder_t& draws);


  std::optional<shared::probe_ray_report_t> probe_ray_report;
  std::string probe_ray_worst_line;
  double probe_ray_cpu_milliseconds = 0.0;
  double probe_ray_gpu_milliseconds = 0.0;
  uint32_t probe_ray_triangle_count = 0;
  void probe_gpu_rays(editor_context_t& ctx);

  std::optional<shared::record_comparison_report_t> indirect_comparison;
  double indirect_compare_cpu_milliseconds = 0.0;
  double indirect_compare_gpu_milliseconds = 0.0;
  void compare_gpu_indirect(editor_context_t& ctx);

  std::optional<shared::record_comparison_report_t> direct_comparison;
  size_t direct_compare_light_count = 0;
  size_t direct_compare_cpu_rays = 0;
  size_t direct_compare_gpu_rays = 0;
  double direct_compare_cpu_milliseconds = 0.0;
  double direct_compare_gpu_milliseconds = 0.0;
  void compare_gpu_direct(editor_context_t& ctx);

  // lightmap_gpu_plan.md step 7: the probe half against trace_probe_light. A
  // "chart" of this report is one z slice of the grid.
  std::optional<shared::record_comparison_report_t> probe_comparison;
  linalg::vec3i probe_compare_grid_count{0, 0, 0};
  size_t probe_compare_open_count = 0;
  size_t probe_compare_light_count = 0;
  double probe_compare_cpu_milliseconds = 0.0;
  double probe_compare_gpu_milliseconds = 0.0;
  void compare_gpu_probes(editor_context_t& ctx);

  // Gate 6 step 2: the capture term against trace_capture_direction, grouped
  // by capture.
  std::optional<shared::record_comparison_report_t> capture_comparison;
  size_t capture_compare_capture_count = 0;
  size_t capture_compare_record_count = 0;
  size_t capture_compare_light_count = 0;
  double capture_compare_cpu_milliseconds = 0.0;
  double capture_compare_gpu_milliseconds = 0.0;
  void compare_gpu_captures(editor_context_t& ctx);

  [[nodiscard]] bool has_packed() const { return baked.atlas.page_count > 0; }
};

} // namespace client
