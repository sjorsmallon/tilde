#pragma once

#include "../editor_tool.hpp"
#include "../../../shared/lightmap_bake.hpp"
#include "../../../shared/lightmap_sidecar.hpp"
#include "../../../shared/lightmap_solve.hpp"

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

  size_t lit_texel_count = 0;

  [[nodiscard]] bool has_packed() const { return baked.atlas.page_count > 0; }
};

} // namespace client
