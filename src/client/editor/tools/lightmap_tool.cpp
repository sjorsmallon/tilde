#include "lightmap_tool.hpp"

#include "../../../shared/lightmap_debug_image.hpp"
#include "../../../shared/lightmap_lights.hpp"
#include "../../../shared/lightmap_solve.hpp"
#include "../../../shared/log.hpp"
#include "../../hud/announcement.hpp"
#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace client
{

namespace
{

constexpr const char *PACKING_IMAGE_PREFIX = "lightmap_packing";
constexpr const char *PAGES_IMAGE_PREFIX = "lightmap_pages";
constexpr const char *MASK_IMAGE_PREFIX = "lightmap_mask";
constexpr const char *VISIBILITY_IMAGE_PREFIX = "lightmap_visibility";
constexpr const char *INDIRECT_IMAGE_PREFIX = "lightmap_indirect";
constexpr const char *INDIRECT_DIRECTION_IMAGE_PREFIX = "lightmap_indirect_direction";

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

void Lightmap_Tool::rebuild_probe_preview(editor_context_t& ctx)
{
  probe_preview_grid =
      shared::try_build_probe_grid(*ctx.map, settings.probe_spacing_in_world_units);
  probe_preview_inside.clear();
  probe_preview_inside_count = 0;
  if (!probe_preview_grid) return;

  const Bounding_Volume_Hierarchy occluders = shared::build_occluder_bvh(*ctx.map);
  probe_preview_inside = shared::classify_probes_inside_solid(*probe_preview_grid, occluders);
  for (const uint8_t flag : probe_preview_inside) probe_preview_inside_count += flag;
}

void Lightmap_Tool::on_draw_overlay(editor_context_t& ctx, pass_builder_t& draws)
{
  if (!show_probe_preview) return;
  const bool draw_baked = !baked.probes.empty();
  if (!draw_baked && !probe_preview_grid) return;

  const shared::probe_grid_t &grid = draw_baked ? baked.probes.grid : *probe_preview_grid;
  const shared::aabb_bounds_t bounds = grid.bounds();
  draws.debug.aabb(bounds.min, bounds.max, with_alpha(colors::white, 0x60));

  const linalg::vec3 eye = draws.view.camera.position;
  const float radius = grid.spacing * (float)PROBE_PREVIEW_RADIUS_IN_SPACINGS;
  const auto lowest_coordinate = [&](float eye_axis, float origin_axis, int count) {
    return std::clamp((int)std::floor((eye_axis - radius - origin_axis) / grid.spacing), 0,
                      count - 1);
  };
  const auto highest_coordinate = [&](float eye_axis, float origin_axis, int count) {
    return std::clamp((int)std::ceil((eye_axis + radius - origin_axis) / grid.spacing), 0,
                      count - 1);
  };
  const linalg::vec3i lowest{lowest_coordinate(eye.x, grid.origin.x, grid.count.x),
                             lowest_coordinate(eye.y, grid.origin.y, grid.count.y),
                             lowest_coordinate(eye.z, grid.origin.z, grid.count.z)};
  const linalg::vec3i highest{highest_coordinate(eye.x, grid.origin.x, grid.count.x),
                              highest_coordinate(eye.y, grid.origin.y, grid.count.y),
                              highest_coordinate(eye.z, grid.origin.z, grid.count.z)};

  probe_preview_drawn_count = 0;
  const float arm = grid.spacing * 0.1f;
  for (int z = lowest.z; z <= highest.z; ++z)
  for (int y = lowest.y; y <= highest.y; ++y)
  for (int x = lowest.x; x <= highest.x; ++x)
  {
    const linalg::vec3 position = grid.position_of({x, y, z});
    if (linalg::length(position - eye) > radius) continue;

    ++probe_preview_drawn_count;
    color_t color = colors::green;
    if (draw_baked)
    {
      const linalg::vec3 l0 = baked.probes.load(grid.index_of(x, y, z)).l0 * preview_exposure;
      const auto to_byte = [](float value) {
        return (uint8_t)std::clamp((int)(value * 255.f), 0, 255);
      };
      color = {to_byte(l0.x), to_byte(l0.y), to_byte(l0.z)};
    }
    else if (probe_preview_inside[grid.index_of(x, y, z)])
      color = colors::red;
    draws.debug.line(position - linalg::vec3{arm, 0, 0}, position + linalg::vec3{arm, 0, 0},
                     color);
    draws.debug.line(position - linalg::vec3{0, arm, 0}, position + linalg::vec3{0, arm, 0},
                     color);
    draws.debug.line(position - linalg::vec3{0, 0, arm}, position + linalg::vec3{0, 0, arm},
                     color);
  }
}

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
  // Spent only on a light with a source radius, so this costs nothing on a map
  // whose lights are all punctual -- which is what it says rather than the plain
  // "Shadow samples" a reader would price against every light in the level.
  ImGui::SliderInt("Rays / area light", &solve_settings.soft_shadow_samples, 1, 64);
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

  // The one expensive switch in this panel: a chain per texel sample, and every
  // vertex of it fires the shadow rays afresh. Off by default for that reason.
  ImGui::Checkbox("Trace indirect light", &solve_settings.trace_indirect_light);
  ImGui::BeginDisabled(!solve_settings.trace_indirect_light);
  ImGui::SliderInt("Chains / texel sample", &solve_settings.indirect_rays_per_sample, 1,
                   512, "%d", ImGuiSliderFlags_Logarithmic);
  ImGui::SliderInt("Roulette after", &solve_settings.indirect_bounces_before_roulette, 1,
                   8);

  // The cost, before it is paid rather than after. A chain is tens of rays and
  // this multiplies against the supersampling, so the difference between a
  // two-minute bake and a two-hour one is two sliders nobody can price by eye.
  if (has_packed())
  {
    size_t covered_texels = 0;
    for (const shared::lightmap_chart_t &chart : baked.charts)
      covered_texels += (size_t)shared::chart_covered_width(chart, baked.settings) *
                        (size_t)shared::chart_covered_height(chart, baked.settings);

    const int samples_per_texel =
        std::max(solve_settings.samples_per_texel_edge, 1) *
        std::max(solve_settings.samples_per_texel_edge, 1);
    const double chains = (double)covered_texels * (double)samples_per_texel *
                          (double)std::max(solve_settings.indirect_rays_per_sample, 1);

    ImGui::TextDisabled("%.1fM chains (%zu texels x %d samples x %d)", chains / 1e6,
                        covered_texels, samples_per_texel,
                        solve_settings.indirect_rays_per_sample);
  }

  ImGui::EndDisabled();

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

    // Written by the BAKE, exactly as the irradiance pages are, and the button
    // below is the second look rather than the only one. A traced bake whose
    // picture needs a separate click is indistinguishable from an untraced one:
    // no file appears and nothing says why.
    indirect_texel_count = 0;
    for (int page = 0; page < baked.indirect_l0_pages.page_count; ++page)
      for (int y = 0; y < baked.indirect_l0_pages.size_in_texels; ++y)
        for (int x = 0; x < baked.indirect_l0_pages.size_in_texels; ++x)
        {
          const linalg::vec3 texel = baked.indirect_l0_pages.load(page, x, y);
          if (texel.x > 0.f || texel.y > 0.f || texel.z > 0.f) ++indirect_texel_count;
        }

    if (!baked.indirect_l0_pages.empty())
    {
      (void)shared::try_write_lightmap_pages_png(baked.indirect_l0_pages,
                                                 INDIRECT_IMAGE_PREFIX, preview_exposure);
      (void)shared::try_write_lightmap_l1_pages_png(baked.indirect_l1_pages,
                                                    baked.indirect_l0_pages,
                                                    INDIRECT_DIRECTION_IMAGE_PREFIX);
    }
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

  ImGui::BeginDisabled(baked.indirect_l0_pages.empty());

  if (ImGui::Button("Write indirect PNGs", {-1, 0}))
  {
    (void)shared::try_write_lightmap_pages_png(baked.indirect_l0_pages,
                                               INDIRECT_IMAGE_PREFIX, preview_exposure);
    (void)shared::try_write_lightmap_l1_pages_png(baked.indirect_l1_pages,
                                                  baked.indirect_l0_pages,
                                                  INDIRECT_DIRECTION_IMAGE_PREFIX);
  }

  ImGui::EndDisabled();

  ImGui::SliderFloat("Preview exposure", &preview_exposure, 0.05f, 256.f, "%.2f",
                     ImGuiSliderFlags_Logarithmic);

  ImGui::Separator();
  ImGui::Text("Irradiance probes");

  const bool spacing_changed =
      ImGui::SliderFloat("Probe spacing", &settings.probe_spacing_in_world_units, 16.f,
                         512.f, "%.0f", ImGuiSliderFlags_Logarithmic);
  ImGui::Checkbox("Bake probes", &solve_settings.bake_probes);
  const bool preview_toggled_on =
      ImGui::Checkbox("Show probe preview", &show_probe_preview) && show_probe_preview;
  ImGui::SameLine();
  const bool rebuild_requested = ImGui::Button("Rebuild preview");

  if (show_probe_preview && (spacing_changed || preview_toggled_on || rebuild_requested))
    rebuild_probe_preview(ctx);

  if (show_probe_preview)
  {
    if (probe_preview_grid)
    {
      const shared::probe_grid_t &grid = *probe_preview_grid;
      const size_t total = grid.probe_count();
      ImGui::Text("%dx%dx%d = %zu probes, %zu inside solids (%.0f%%)", grid.count.x,
                  grid.count.y, grid.count.z, total, probe_preview_inside_count,
                  total ? 100.0 * (double)probe_preview_inside_count / (double)total : 0.0);
      ImGui::Text("%.1f KB at 16 bytes a probe", (double)total * 16.0 / 1024.0);
      ImGui::Text("Drawing %zu within %.0f units of the camera", probe_preview_drawn_count,
                  grid.spacing * (float)PROBE_PREVIEW_RADIUS_IN_SPACINGS);
      if (baked.probes.empty())
        ImGui::TextDisabled("Green: open space. Red: inside a solid, filled from its "
                            "neighbours at bake time. Rebuild after editing geometry.");
      else
        ImGui::TextDisabled("Baked: each probe is tinted by its L0 at the preview "
                            "exposure.");
    }
    else
    {
      ImGui::TextColored({1.f, 0.55f, 0.2f, 1.f},
                         "No probe grid: the map has no geometry, or an axis exceeds %d "
                         "probes at this spacing (see the log).",
                         shared::MAX_PROBE_GRID_EXTENT);
    }
  }

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

    // Per light, because "3 baked lights" says nothing about the one that
    // reached no face -- and that one draws nothing, with no other symptom.
    for (size_t slot = 0; slot < baked.light_uids.size(); ++slot)
    {
      const size_t kept_by =
          shared::count_charts_keeping_light(baked, (int16_t)slot);
      if (kept_by == 0)
        ImGui::TextColored({1.f, 0.55f, 0.2f, 1.f},
                           "  light uid %u: slot %zu, kept by NO chart -- lights nothing",
                           baked.light_uids[slot], slot);
      else
        ImGui::Text("  light uid %u: slot %zu, kept by %zu chart(s)",
                    baked.light_uids[slot], slot, kept_by);
    }

    if (!visibility_masks.empty())
      ImGui::Text("%zu visibility slot(s) in the debug masks",
                  visibility_masks.slot_count());

    if (!baked.indirect_l0_pages.empty())
      ImGui::Text("indirect: %zu texel(s) bounced across %d page(s), SH L1",
                  indirect_texel_count, baked.indirect_l0_pages.page_count);

    if (!baked.probes.empty())
      ImGui::Text("probes: %dx%dx%d at spacing %.0f, %.1f KB", baked.probes.grid.count.x,
                  baked.probes.grid.count.y, baked.probes.grid.count.z,
                  baked.probes.grid.spacing,
                  (double)(baked.probes.l0_bytes.size() + baked.probes.l1_bytes.size()) /
                      1024.0);
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
