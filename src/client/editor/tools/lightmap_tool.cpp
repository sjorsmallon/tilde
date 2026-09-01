#include "lightmap_tool.hpp"

#include "../../../shared/lightmap_debug_image.hpp"
#include "../../../shared/lightmap_solve.hpp"
#include "../../../shared/log.hpp"
#include "../../hud/announcement.hpp"
#include "imgui.h"

namespace client
{

namespace
{

constexpr const char *PACKING_IMAGE_PREFIX = "lightmap_packing";
constexpr const char *PAGES_IMAGE_PREFIX = "lightmap_pages";
constexpr const char *MASK_IMAGE_PREFIX = "lightmap_mask";
constexpr const char *VISIBILITY_IMAGE_PREFIX = "lightmap_visibility";

} // namespace

void Lightmap_Tool::on_enable(editor_context_t& ctx) {}
void Lightmap_Tool::on_disable(editor_context_t& ctx) {}

void Lightmap_Tool::on_update(editor_context_t& ctx, const viewport_state_t& view,
                              float dt)
{
}

void Lightmap_Tool::on_mouse_down(editor_context_t& ctx, const input::mouse_event_t& e) {}
void Lightmap_Tool::on_mouse_drag(editor_context_t& ctx, const input::mouse_event_t& e) {}
void Lightmap_Tool::on_mouse_up(editor_context_t& ctx, const input::mouse_event_t& e) {}
void Lightmap_Tool::on_key_down(editor_context_t& ctx, const key_event_t& e) {}

void Lightmap_Tool::on_draw_overlay(editor_context_t& ctx, pass_builder_t& draws) {}

void Lightmap_Tool::on_draw_ui(editor_context_t& ctx)
{
  ImGui::SetNextWindowSize({340, 0}, ImGuiCond_Once);
  if (!ImGui::Begin("Lightmap Bake"))
  {
    ImGui::End();
    return;
  }

  ImGui::Separator();

  ImGui::SliderFloat("Texels / unit", &settings.texels_per_world_unit, 0.03125f, 4.0f,
                     "%.4f", ImGuiSliderFlags_Logarithmic);
  ImGui::SliderInt("Gutter", &settings.gutter_in_texels, 0, 8);
  ImGui::SliderInt("Max chart", &settings.max_chart_extent_in_texels, 16, 2048);
  ImGui::SliderInt("Atlas size", &settings.atlas_size_in_texels, 64, 4096);

  ImGui::Separator();

  if (ImGui::Button("Build charts and pack", {-1, 0}))
  {
    baked = {};
    baked.settings = settings;
    baked.charts = shared::build_lightmap_charts(*ctx.map, settings);
    baked.atlas = shared::pack_lightmap_charts(baked.charts, settings);
    shared::set_lightmap_geometry_id(baked);
    lit_texel_count = 0;

    if (!has_packed())
      log_error("[lightmap] packing produced no pages for {} charts.",
                baked.charts.size());
  }

  if (!has_packed())
    ImGui::BeginDisabled();

  if (ImGui::Button("Write packing PNG", {-1, 0}))
  {
    (void)shared::try_write_lightmap_debug_png(baked.charts, baked.atlas, baked.settings,
                                               PACKING_IMAGE_PREFIX);
  }

  ImGui::SliderFloat("Shadow bias", &solve_settings.shadow_ray_bias, 0.01f, 8.0f,
                     "%.2f");
  ImGui::SliderInt("Samples / texel edge", &solve_settings.samples_per_texel_edge, 1, 8);
  ImGui::Checkbox("Dilate into the gutter", &solve_settings.dilate_into_the_gutter);

  // Two radios rather than two buttons: they run the same path and produce the
  // same format, so what an author is choosing is what the pixels MEAN, not which
  // bake to run.
  int mode = (int)solve_settings.mode;
  ImGui::RadioButton("Direct light", &mode, (int)shared::lightmap_solve_mode_t::Direct_Light);
  ImGui::SameLine();
  ImGui::RadioButton("Visibility", &mode, (int)shared::lightmap_solve_mode_t::Visibility);
  solve_settings.mode = (shared::lightmap_solve_mode_t)mode;

  ImGui::Checkbox("Per-light visibility masks", &emit_per_light_visibility);

  if (ImGui::Button("Bake", {-1, 0}))
  {
    visibility_masks = {};
    shared::bake_lightmap(*ctx.map, baked, solve_settings,
                          emit_per_light_visibility ? &visibility_masks : nullptr);

    lit_texel_count = 0;
    for (int page = 0; page < baked.irradiance_pages.page_count; ++page)
      for (int y = 0; y < baked.irradiance_pages.size_in_texels; ++y)
        for (int x = 0; x < baked.irradiance_pages.size_in_texels; ++x)
        {
          const linalg::vec3 texel = baked.irradiance_pages.load(page, x, y);
          if (texel.x > 0.f || texel.y > 0.f || texel.z > 0.f) ++lit_texel_count;
        }

    (void)shared::try_write_lightmap_pages_png(baked.irradiance_pages, PAGES_IMAGE_PREFIX,
                                               preview_exposure);
  }

  ImGui::BeginDisabled(baked.visibility_pages.empty());

  if (ImGui::Button("Write stored visibility PNGs", {-1, 0}))
    (void)shared::try_write_lightmap_visibility_pages_png(baked.visibility_pages,
                                                          VISIBILITY_IMAGE_PREFIX);

  ImGui::EndDisabled();

  ImGui::BeginDisabled(visibility_masks.empty());

  if (ImGui::Button("Write per-light mask PNGs", {-1, 0}))
    (void)shared::try_write_lightmap_visibility_png(visibility_masks, MASK_IMAGE_PREFIX);

  ImGui::EndDisabled();

  ImGui::SliderFloat("Preview exposure", &preview_exposure, 0.05f, 256.f, "%.2f",
                     ImGuiSliderFlags_Logarithmic);

  // Applying is what makes a bake outlive the tool. It is one step rather than
  // two because the map and the sidecar disagreeing is a state worth making
  // unrepresentable: the sidecar carries the hash of the map it was baked from,
  // and writing one without adopting the other leaves the editor drawing lighting
  // no file records.
  const bool can_apply = has_packed() && !ctx.map_path.empty();
  if (!can_apply)
    ImGui::BeginDisabled();

  if (ImGui::Button("Apply to map and save sidecar", {-1, 0}))
  {
    ctx.map->lightmap = baked;
    shared::save_lightmap_sidecar(ctx.map_path, baked,
                                  shared::compute_map_content_hash(*ctx.map));
    if (ctx.lightmap_updated_so_atlas_upload_is_needed)
      *ctx.lightmap_updated_so_atlas_upload_is_needed = true;
    hud::set_announcement("Lightmap applied");
  }

  if (!can_apply)
    ImGui::EndDisabled();

  if (has_packed() && ctx.map_path.empty())
    ImGui::TextDisabled("Save the map first -- a sidecar needs somewhere to go.");

  if (!has_packed())
    ImGui::EndDisabled();

  ImGui::Separator();

  if (has_packed())
  {
    ImGui::Text("%zu charts across %d page(s) of %d texels", baked.charts.size(),
                baked.atlas.page_count, baked.atlas.size_in_texels);

    size_t covered_texels = 0;
    for (const shared::lightmap_chart_t &chart : baked.charts)
      covered_texels += (size_t)chart.atlas_rect.width * (size_t)chart.atlas_rect.height;

    const size_t page_texels =
        (size_t)baked.atlas.size_in_texels * (size_t)baked.atlas.size_in_texels *
        (size_t)baked.atlas.page_count;
    ImGui::Text("%.1f%% of the atlas used",
                100.0 * (double)covered_texels / (double)page_texels);

    if (baked.irradiance_pages.page_count > 0)
      ImGui::Text("%zu texels see a light (%d bytes/texel)", lit_texel_count,
                  shared::bytes_per_texel(baked.irradiance_pages.format));

    ImGui::Text("%zu baked light(s); a chart keeps %u of them",
                baked.light_uids.size(), shared::LIGHTMAP_LIGHTS_PER_CHART);

    if (!visibility_masks.empty())
      ImGui::Text("%zu visibility slot(s) in the debug masks",
                  visibility_masks.slot_count());
  }
  else
  {
    ImGui::TextDisabled("Nothing packed yet.");
  }

  ImGui::Separator();
  ImGui::Text("Map holds: %zu chart(s)", ctx.map->lightmap.charts.size());

  ImGui::Separator();
  ImGui::TextWrapped("Direct light sums radiance * falloff * N.L over every point, spot "
                     "and directional light, shadowed. Visibility is the same walk with "
                     "falloff forced to 1 and colour forced to white -- the debug view "
                     "that separates a wrong shadow test from wrong falloff math. Both "
                     "write RGB9E5; no bounces, and static meshes get no charts. Apply "
                     "writes map_t::lightmap and the .lightmap sidecar, and uploads the "
                     "atlas -- baked faces light from it immediately.");

  ImGui::Separator();
  ImGui::TextWrapped("Every bake also writes a per-light VISIBILITY page set: one "
                     "coverage scalar per light slot per texel, pure shadow-ray "
                     "occlusion, NOT gated on N.L against the flat face plane, because "
                     "the runtime shades with a normal-mapped normal. A chart keeps its "
                     "four strongest lights and names them in the resolve table; the "
                     "rest are dropped with a line naming the face and the light. The "
                     "debug masks are the same walk kept for EVERY light, written as "
                     "PNGs, which is where a dropped one is visible.");

  ImGui::End();
}

} // namespace client
