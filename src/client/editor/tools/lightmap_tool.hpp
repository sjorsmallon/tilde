#pragma once

#include "../editor_tool.hpp"
#include "../../../shared/lightmap_bake.hpp"
#include "../../../shared/lightmap_probes.hpp"
#include "../../../shared/lightmap_sidecar.hpp"
#include "../../../shared/lightmap_solve.hpp"

#include <optional>
#include <vector>

namespace client
{

// The bake. Flattens every brush face into a chart, packs the charts into atlas
// pages, solves lighting into them, and -- on Apply -- writes the whole thing
// into map_t::lightmap and out to the .lightmap sidecar beside the .source.
// lightmap_def.md is the design; geometry_def.md ss10 is what the settings answer
// to.
class Lightmap_Tool : public Editor_Tool
{
public:
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

  // The bake in progress. One value rather than loose members, because the charts
  // and the pixels are no use without each other -- that is the same reason they
  // save and load together.
  shared::lightmap_t baked;

  // Per-light shadow-ray coverage from the same bake, held only to be looked at:
  // it goes into no sidecar and no atlas yet (lighting_def.md ss14 step 6).
  shared::lightmap_visibility_masks_t visibility_masks;
  bool emit_per_light_visibility = true;

  size_t lit_texel_count = 0;
  size_t indirect_texel_count = 0;

  // Gate 5's preview: the probe grid the bake WOULD trace, drawn before any
  // bake exists so the spacing can be tuned by eye. Built through the same two
  // functions the bake calls, so it cannot show a different grid.
  bool show_probe_preview = false;
  std::optional<shared::probe_grid_t> probe_preview_grid;
  std::vector<uint8_t> probe_preview_inside;
  size_t probe_preview_inside_count = 0;

  // Only the probes within this many spacings of the camera are drawn: a level's
  // whole grid is a hundred thousand crosses, which is more than the debug
  // vertex buffer holds, and the spacing is judged where you stand anyway.
  static constexpr int PROBE_PREVIEW_RADIUS_IN_SPACINGS = 10;
  size_t probe_preview_drawn_count = 0;

  void rebuild_probe_preview(editor_context_t& ctx);

  [[nodiscard]] bool has_packed() const { return baked.atlas.page_count > 0; }
};

} // namespace client
