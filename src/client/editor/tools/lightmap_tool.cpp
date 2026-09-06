#include "lightmap_tool.hpp"

#include "../../../shared/lightmap_debug_image.hpp"
#include "../../../shared/lightmap_gpu.hpp"
#include "../../../shared/lightmap_lights.hpp"
#include "../../../shared/lightmap_reflections.hpp"
#include "../../../shared/lightmap_solve.hpp"
#include "../../../shared/lightmap_trace.hpp"
#include "../../../shared/log.hpp"
#include "../../../shared/cvars/generated/cvars_generated.hpp"
#include "../../hud/announcement.hpp"
#include "../../lightmap_gpu_vulkan.hpp"
#include "../../renderer.hpp"
#include "../../state_manager.hpp"
#include "imgui.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <optional>

namespace client
{

namespace
{

constexpr const char* PACKING_IMAGE_PREFIX = "lightmap_packing";
constexpr const char* PAGES_IMAGE_PREFIX = "lightmap_pages";
constexpr const char* MASK_IMAGE_PREFIX = "lightmap_mask";
constexpr const char* VISIBILITY_IMAGE_PREFIX = "lightmap_visibility";
constexpr const char* INDIRECT_IMAGE_PREFIX = "lightmap_indirect";
constexpr const char* INDIRECT_DIRECTION_IMAGE_PREFIX = "lightmap_indirect_direction";
constexpr const char* COMPARE_CPU_IMAGE_PREFIX = "lightmap_compare_cpu";
constexpr const char* COMPARE_GPU_IMAGE_PREFIX = "lightmap_compare_gpu";
constexpr const char* COMPARE_DIFFERENCE_IMAGE_PREFIX = "lightmap_compare_difference";
constexpr const char* COMPARE_DIRECT_CPU_IMAGE_PREFIX = "lightmap_compare_direct_cpu";
constexpr const char* COMPARE_DIRECT_GPU_IMAGE_PREFIX = "lightmap_compare_direct_gpu";
constexpr const char* COMPARE_DIRECT_DIFFERENCE_IMAGE_PREFIX =
    "lightmap_compare_direct_difference";

// The two solvers' pictures side by side and their difference, the way the
// debug pages already are written.
void write_comparison_pages(const shared::lightmap_t& lightmap,
                            const shared::lightmap_sample_set_t& set,
                            Span<const linalg::vec3> from_cpu, Span<const linalg::vec3> from_gpu,
                            const char* cpu_prefix, const char* gpu_prefix,
                            const char* difference_prefix, float exposure)
{
  const shared::lightmap_pages_t cpu_pages =
      shared::reduce_record_values_to_pages(lightmap, set, from_cpu);
  const shared::lightmap_pages_t gpu_pages =
      shared::reduce_record_values_to_pages(lightmap, set, from_gpu);
  (void)shared::try_write_lightmap_pages_png(cpu_pages, cpu_prefix, exposure);
  (void)shared::try_write_lightmap_pages_png(gpu_pages, gpu_prefix, exposure);
  (void)shared::try_write_lightmap_pages_png(
      shared::absolute_difference_pages(cpu_pages, gpu_pages), difference_prefix, exposure);
}

float mean_of(const std::vector<float>& values, size_t first, size_t count)
{
  double sum = 0.0;
  for (size_t k = first; k < first + count; ++k) sum += values[k];
  return count ? (float)(sum / (double)count) : 0.f;
}

// indirect_sh_l1_t's twelve coefficients, in the order the comparison reports.
constexpr const char* SH_COEFFICIENT_NAMES[shared::SH_L1_COEFFICIENT_COUNT] = {
    "L0.r",  "L0.g",  "L0.b",  "L1x.r", "L1x.g", "L1x.b",
    "L1y.r", "L1y.g", "L1y.b", "L1z.r", "L1z.g", "L1z.b"};

// lightmap_gpu_plan.md step 7b: which solver a bake runs through, and in words
// why, so a bake that ran on the CPU is never a silent downgrade.
struct bake_path_t
{
  bool through_the_gpu = false;
  std::string description;
};

[[nodiscard]] bake_path_t bake_path_for(const cvars::cvar_state_t& cvars)
{
  if (!cvars.r_lightmap_gpu)
    return {false, "the CPU reference solve (r_lightmap_gpu is off)"};
  if (!renderer::ray_query_is_available())
    return {false, std::format("the CPU reference solve (ray query unavailable: {})",
                               renderer::ray_query_unavailable_reason())};
  return {true, "the Vulkan ray query solver"};
}

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

// The CPU's slab test against a convex piece and the GPU's triangle test reach
// the same plane by different arithmetic; this is how far apart two floats may
// land on it before the difference is called a disagreement.
constexpr float PROBE_RAY_DISTANCE_TOLERANCE = 0.1f;

void Lightmap_Tool::probe_gpu_rays(editor_context_t& ctx)
{
  probe_ray_report.reset();
  probe_ray_triangle_count = 0;
  if (!renderer::ray_query_is_available() || !has_packed()) return;

  const Bounding_Volume_Hierarchy bvh = shared::build_occluder_bvh(*ctx.map);
  const std::vector<shared::gpu_sample_t> samples =
      shared::collect_lightmap_samples(baked, solve_settings, bvh);
  const shared::traced_scene_t traced = shared::build_traced_scene(*ctx.map, bvh);
  const shared::gpu_bake_scene_t gpu_scene = shared::build_gpu_bake_scene(*ctx.map, traced);
  const std::vector<shared::baked_light_t> lights = shared::collect_lights(*ctx.map);

  const float bias = solve_settings.shadow_ray_bias;
  const float max_distance = solve_settings.directional_shadow_distance;

  using clock = std::chrono::steady_clock;
  const auto milliseconds_since = [](clock::time_point started) {
    return std::chrono::duration<double, std::milli>(clock::now() - started).count();
  };

  vulkan_batch_solver_t solver;
  solver.upload_scene({&gpu_scene, &bvh, &traced, Span<const shared::baked_light_t>(lights),
                       shared::gpu_bake_settings_from(solve_settings)});
  probe_ray_triangle_count = solver.triangle_count();
  if (!solver.has_scene()) return;

  const clock::time_point gpu_started = clock::now();
  std::vector<float> gpu_distances;
  solver.probe_rays(samples, bias, max_distance, gpu_distances);
  probe_ray_gpu_milliseconds = milliseconds_since(gpu_started);

  const clock::time_point cpu_started = clock::now();
  const std::vector<float> cpu_distances =
      shared::probe_ray_distances(bvh, samples, bias, max_distance);
  probe_ray_cpu_milliseconds = milliseconds_since(cpu_started);

  probe_ray_report =
      shared::compare_probe_rays(cpu_distances, gpu_distances, PROBE_RAY_DISTANCE_TOLERANCE);
  const shared::probe_ray_report_t& report = *probe_ray_report;
  log_terminal("[lightmap-gpu] probed {} ray(s) over {} triangle(s): {} hit on both, of which {} "
               "started inside a solid (CPU answers 0 there); of the rest {} apart by more than "
               "{} units ({} GPU farther, {} GPU nearer), largest {:.4f}, mean {:.6f}; {} missed "
               "on both, {} CPU-only hit(s), {} GPU-only hit(s); GPU {:.1f} ms, CPU {:.1f} ms.",
               report.sample_count, probe_ray_triangle_count, report.both_hit,
               report.reference_started_inside_a_solid, report.hits_outside_tolerance,
               PROBE_RAY_DISTANCE_TOLERANCE, report.candidate_farther, report.candidate_nearer,
               report.largest_distance_error, report.mean_distance_error, report.both_missed,
               report.reference_only_hit, report.candidate_only_hit, probe_ray_gpu_milliseconds,
               probe_ray_cpu_milliseconds);

  // The one ray to go and look at: where it started, which way it went, and
  // what each side said.
  probe_ray_worst_line.clear();
  if (report.worst_sample >= 0)
  {
    const shared::gpu_sample_t& worst = samples[(size_t)report.worst_sample];
    probe_ray_worst_line = std::format(
        "worst ray: from ({:.1f}, {:.1f}, {:.1f}) along ({:.2f}, {:.2f}, {:.2f}): CPU {:.3f}, "
        "GPU {:.3f}",
        worst.position.x, worst.position.y, worst.position.z, worst.normal.x, worst.normal.y,
        worst.normal.z, cpu_distances[(size_t)report.worst_sample],
        gpu_distances[(size_t)report.worst_sample]);
    log_terminal("[lightmap-gpu] {}", probe_ray_worst_line);
  }
}

void Lightmap_Tool::compare_gpu_indirect(editor_context_t& ctx)
{
  indirect_comparison.reset();
  if (!renderer::ray_query_is_available() || !has_packed()) return;

  const Bounding_Volume_Hierarchy bvh = shared::build_occluder_bvh(*ctx.map);
  const shared::lightmap_sample_set_t set =
      shared::collect_lightmap_sample_set(baked, solve_settings, bvh);
  const shared::traced_scene_t traced = shared::build_traced_scene(*ctx.map, bvh);
  const shared::gpu_bake_scene_t gpu_scene = shared::build_gpu_bake_scene(*ctx.map, traced);
  const std::vector<shared::baked_light_t> lights = shared::collect_lights(*ctx.map);
  const shared::batch_solver_scene_t scene{&gpu_scene, &bvh, &traced,
                                           Span<const shared::baked_light_t>(lights),
                                           shared::gpu_bake_settings_from(solve_settings)};

  using clock = std::chrono::steady_clock;
  const auto milliseconds_since = [](clock::time_point started) {
    return std::chrono::duration<double, std::milli>(clock::now() - started).count();
  };

  // The GPU first: it is the cheap one, and a scene it refuses is found before
  // the CPU spends its minutes.
  vulkan_batch_solver_t gpu;
  gpu.upload_scene(scene);
  if (!gpu.has_scene()) return;

  shared::gpu_indirect_results_t from_gpu;
  const clock::time_point gpu_started = clock::now();
  gpu.solve_indirect(set.samples, from_gpu);
  indirect_compare_gpu_milliseconds = milliseconds_since(gpu_started);

  shared::cpu_batch_solver_t cpu;
  cpu.upload_scene(scene);
  shared::gpu_indirect_results_t from_cpu;
  const clock::time_point cpu_started = clock::now();
  cpu.solve_indirect(set.samples, from_cpu);
  indirect_compare_cpu_milliseconds = milliseconds_since(cpu_started);

  indirect_comparison =
      shared::compare_indirect_results(set.samples, set.charts, from_cpu.values, from_gpu.values);
  const shared::record_comparison_report_t& report = *indirect_comparison;

  log_terminal("[lightmap-gpu] indirect: {} record(s) over {} chart(s), {} chain(s) each, {} "
               "with a non-zero answer; CPU {:.1f} ms, GPU {:.1f} ms. Reference L0 mean {:.5f}, "
               "mean |dL0| {:.6f} ({:.3f}%); {} record(s) differ in any coefficient; {} chart(s) "
               "beyond {} sigma.",
               report.record_count, report.chart_count, solve_settings.indirect_rays_per_sample,
               report.reference_nonzero_records, indirect_compare_cpu_milliseconds,
               indirect_compare_gpu_milliseconds, report.reference_mean_over(0, 3),
               report.mean_absolute_difference_over(0, 3),
               report.reference_mean_over(0, 3) > 0.f
                   ? 100.0 * (double)report.mean_absolute_difference_over(0, 3) /
                         (double)report.reference_mean_over(0, 3)
                   : 0.0,
               report.differing_records, report.charts_beyond_tolerance,
               shared::RECORD_COMPARISON_SIGMA);
  for (size_t i = 0; i < std::min<size_t>(report.charts.size(), 8); ++i)
  {
    const shared::record_chart_comparison_t& chart = report.charts[i];
    const int k = std::max(chart.largest_sigma_coefficient, 0);
    log_terminal("[lightmap-gpu]   chart {} (uid {}, {} records): {} CPU {:.6f} GPU {:.6f} "
                 "+- {:.6f}, {:.1f} sigma",
                 chart.chart, baked.charts[chart.chart].object_uid, chart.record_count,
                 SH_COEFFICIENT_NAMES[k], chart.reference_mean[(uint32_t)k],
                 chart.candidate_mean[(uint32_t)k],
                 chart.difference_standard_error[(uint32_t)k], chart.largest_sigma);
  }

  std::vector<linalg::vec3> cpu_l0(set.samples.size());
  std::vector<linalg::vec3> gpu_l0(set.samples.size());
  for (size_t i = 0; i < set.samples.size(); ++i)
  {
    cpu_l0[i] = from_cpu.values[i].l0;
    gpu_l0[i] = from_gpu.values[i].l0;
  }
  write_comparison_pages(baked, set, cpu_l0, gpu_l0, COMPARE_CPU_IMAGE_PREFIX,
                         COMPARE_GPU_IMAGE_PREFIX, COMPARE_DIFFERENCE_IMAGE_PREFIX,
                         preview_exposure);
}

// lightmap_gpu_plan.md step 6's pin. Every chart's mask admits EVERY light, so
// the irradiance sum is exercised over all of them: a bake's first dispatch runs
// under all-zero masks, which would leave the sum identically zero on both sides
// and compare nothing. Coverage and weight read no mask.
void Lightmap_Tool::compare_gpu_direct(editor_context_t& ctx)
{
  direct_comparison.reset();
  if (!renderer::ray_query_is_available() || !has_packed()) return;

  const Bounding_Volume_Hierarchy bvh = shared::build_occluder_bvh(*ctx.map);
  const shared::lightmap_sample_set_t set =
      shared::collect_lightmap_sample_set(baked, solve_settings, bvh);
  const shared::traced_scene_t traced = shared::build_traced_scene(*ctx.map, bvh);
  const shared::gpu_bake_scene_t gpu_scene = shared::build_gpu_bake_scene(*ctx.map, traced);
  const std::vector<shared::baked_light_t> lights = shared::collect_lights(*ctx.map);
  const shared::batch_solver_scene_t scene{&gpu_scene, &bvh, &traced,
                                           Span<const shared::baked_light_t>(lights),
                                           shared::gpu_bake_settings_from(solve_settings)};
  direct_compare_light_count = lights.size();
  const std::vector<uint64_t> every_light(set.charts.size(), ~(uint64_t)0);

  using clock = std::chrono::steady_clock;
  const auto milliseconds_since = [](clock::time_point started) {
    return std::chrono::duration<double, std::milli>(clock::now() - started).count();
  };

  vulkan_batch_solver_t gpu;
  gpu.upload_scene(scene);
  if (!gpu.has_scene()) return;

  shared::gpu_direct_results_t from_gpu;
  const clock::time_point gpu_started = clock::now();
  gpu.solve_direct(set.samples, every_light, from_gpu);
  direct_compare_gpu_milliseconds = milliseconds_since(gpu_started);
  direct_compare_gpu_rays = gpu.statistics().shade.direct_rays;

  shared::cpu_batch_solver_t cpu;
  cpu.upload_scene(scene);
  shared::gpu_direct_results_t from_cpu;
  const clock::time_point cpu_started = clock::now();
  cpu.solve_direct(set.samples, every_light, from_cpu);
  direct_compare_cpu_milliseconds = milliseconds_since(cpu_started);
  direct_compare_cpu_rays = cpu.statistics().shade.direct_rays;

  direct_comparison = shared::compare_direct_results(set.samples, set.charts, from_cpu, from_gpu);
  const shared::record_comparison_report_t& report = *direct_comparison;

  const size_t light_count = lights.size();
  log_terminal("[lightmap-gpu] direct: {} record(s) over {} chart(s) and {} light(s), {} lit "
               "by something; CPU {:.1f} ms casting {} ray(s), GPU {:.1f} ms casting {}. "
               "Reference irradiance mean {:.5f}, mean |dE| {:.6f} ({:.3f}%); {} record(s) "
               "differ in any coefficient; largest |d| irradiance {:.3g}, coverage {:.3g}, "
               "weight {:.3g}; {} chart(s) beyond {} sigma.",
               report.record_count, report.chart_count, light_count,
               report.reference_nonzero_records, direct_compare_cpu_milliseconds,
               direct_compare_cpu_rays, direct_compare_gpu_milliseconds, direct_compare_gpu_rays,
               report.reference_mean_over(0, 3), report.mean_absolute_difference_over(0, 3),
               report.reference_mean_over(0, 3) > 0.f
                   ? 100.0 * (double)report.mean_absolute_difference_over(0, 3) /
                         (double)report.reference_mean_over(0, 3)
                   : 0.0,
               report.differing_records, report.largest_absolute_difference_over(0, 3),
               light_count ? report.largest_absolute_difference_over(3, light_count) : 0.f,
               light_count ? report.largest_absolute_difference_over(3 + light_count, light_count)
                           : 0.f,
               report.charts_beyond_tolerance, shared::RECORD_COMPARISON_SIGMA);
  for (size_t i = 0; i < std::min<size_t>(report.charts.size(), 8); ++i)
  {
    const shared::record_chart_comparison_t& chart = report.charts[i];
    // The flagged coefficient, or the irradiance when nothing is flagged: a
    // chart at zero sigma is shown by what it is lit with.
    const size_t k = chart.largest_sigma_coefficient >= 0
                         ? (size_t)chart.largest_sigma_coefficient
                         : 0;
    char name[shared::DIRECT_COEFFICIENT_NAME_CAPACITY];
    log_terminal("[lightmap-gpu]   chart {} (uid {}, {} records): {} CPU {:.6f} GPU {:.6f} "
                 "+- {:.6f}, {:.1f} sigma; mean coverage CPU {:.4f} GPU {:.4f}",
                 chart.chart, baked.charts[chart.chart].object_uid, chart.record_count,
                 shared::direct_coefficient_name(k, light_count, Span<char>(name)),
                 chart.reference_mean[k], chart.candidate_mean[k],
                 chart.difference_standard_error[k], chart.largest_sigma,
                 light_count ? mean_of(chart.reference_mean, 3, light_count) : 0.f,
                 light_count ? mean_of(chart.candidate_mean, 3, light_count) : 0.f);
  }

  write_comparison_pages(baked, set, from_cpu.irradiance, from_gpu.irradiance,
                         COMPARE_DIRECT_CPU_IMAGE_PREFIX, COMPARE_DIRECT_GPU_IMAGE_PREFIX,
                         COMPARE_DIRECT_DIFFERENCE_IMAGE_PREFIX, preview_exposure);
}

// lightmap_gpu_plan.md step 7's pin: every open probe of the grid the panel's
// spacing would bake, through both solvers, at the current chain count. Needs
// no packed atlas -- a probe is a point in space, not a texel -- but a scene
// with lights, since a probe with nothing to see answers zero on both sides
// and compares nothing. The paired test groups probes by z SLICE: a probe on
// its own has no standard error, and a slice is the natural row of the grid.
void Lightmap_Tool::compare_gpu_probes(editor_context_t& ctx)
{
  probe_comparison.reset();
  if (!renderer::ray_query_is_available()) return;

  const std::optional<shared::probe_grid_t> grid =
      shared::try_build_probe_grid(*ctx.map, settings.probe_spacing_in_world_units);
  if (!grid)
  {
    log_error("[lightmap-gpu] no probe grid at spacing {}; nothing to compare.",
              settings.probe_spacing_in_world_units);
    return;
  }

  const Bounding_Volume_Hierarchy bvh = shared::build_occluder_bvh(*ctx.map);
  const shared::traced_scene_t traced = shared::build_traced_scene(*ctx.map, bvh);
  const shared::gpu_bake_scene_t gpu_scene = shared::build_gpu_bake_scene(*ctx.map, traced);
  const std::vector<shared::baked_light_t> lights = shared::collect_lights(*ctx.map);
  const shared::batch_solver_scene_t scene{&gpu_scene, &bvh, &traced,
                                           Span<const shared::baked_light_t>(lights),
                                           shared::gpu_bake_settings_from(solve_settings)};
  const std::vector<uint8_t> inside = shared::classify_probes_inside_solid(*grid, bvh);
  const shared::probe_visibility_slots_t slots =
      shared::assign_probe_visibility_channels(lights);
  const std::vector<shared::gpu_sample_t> samples = shared::collect_probe_samples(*grid, inside);
  probe_compare_grid_count = grid->count;
  probe_compare_open_count = samples.size();
  probe_compare_light_count = lights.size();

  using clock = std::chrono::steady_clock;
  const auto milliseconds_since = [](clock::time_point started) {
    return std::chrono::duration<double, std::milli>(clock::now() - started).count();
  };

  vulkan_batch_solver_t gpu;
  gpu.upload_scene(scene);
  if (!gpu.has_scene()) return;

  shared::gpu_probe_results_t from_gpu;
  const clock::time_point gpu_started = clock::now();
  gpu.solve_probes(samples, slots, from_gpu);
  probe_compare_gpu_milliseconds = milliseconds_since(gpu_started);

  shared::cpu_batch_solver_t cpu;
  cpu.upload_scene(scene);
  shared::gpu_probe_results_t from_cpu;
  const clock::time_point cpu_started = clock::now();
  cpu.solve_probes(samples, slots, from_cpu);
  probe_compare_cpu_milliseconds = milliseconds_since(cpu_started);

  std::vector<shared::gpu_sample_t> by_slice = samples;
  for (shared::gpu_sample_t& sample : by_slice)
    sample.chart_index = (uint32_t)grid->coordinates_of(sample.chart_index).z;
  std::vector<size_t> slices((size_t)grid->count.z);
  for (size_t z = 0; z < slices.size(); ++z) slices[z] = z;

  probe_comparison = shared::compare_probe_results(by_slice, slices, from_cpu.values,
                                                   from_gpu.values);
  const shared::record_comparison_report_t& report = *probe_comparison;

  uint32_t channels = 0;
  for (const int16_t slot : slots) channels += slot != shared::LIGHTMAP_NO_LIGHT_SLOT;
  log_terminal("[lightmap-gpu] probes: {} open of {}x{}x{}, {} light(s) ({} Mixed visibility "
               "channel(s)), {} chain(s) each; CPU {:.1f} ms, GPU {:.1f} ms. Reference L0 mean "
               "{:.5f}, mean |dL0| {:.6f} ({:.3f}%); visibility mean {:.4f}, mean |dV| {:.6f}; "
               "{} record(s) differ in any coefficient; {} slice(s) beyond {} sigma.",
               report.record_count, grid->count.x, grid->count.y, grid->count.z, lights.size(),
               channels, solve_settings.indirect_rays_per_sample, probe_compare_cpu_milliseconds,
               probe_compare_gpu_milliseconds, report.reference_mean_over(0, 3),
               report.mean_absolute_difference_over(0, 3),
               report.reference_mean_over(0, 3) > 0.f
                   ? 100.0 * (double)report.mean_absolute_difference_over(0, 3) /
                         (double)report.reference_mean_over(0, 3)
                   : 0.0,
               report.reference_mean_over(shared::SH_L1_COEFFICIENT_COUNT,
                                          shared::PROBE_VISIBILITY_CHANNELS),
               report.mean_absolute_difference_over(shared::SH_L1_COEFFICIENT_COUNT,
                                                    shared::PROBE_VISIBILITY_CHANNELS),
               report.differing_records, report.charts_beyond_tolerance,
               shared::RECORD_COMPARISON_SIGMA);
  for (size_t i = 0; i < std::min<size_t>(report.charts.size(), 8); ++i)
  {
    const shared::record_chart_comparison_t& slice = report.charts[i];
    const size_t k = (size_t)std::max(slice.largest_sigma_coefficient, 0);
    log_terminal("[lightmap-gpu]   slice z={} ({} probes): {} CPU {:.6f} GPU {:.6f} +- {:.6f}, "
                 "{:.1f} sigma",
                 slice.chart, slice.record_count, shared::probe_coefficient_name(k),
                 slice.reference_mean[k], slice.candidate_mean[k],
                 slice.difference_standard_error[k], slice.largest_sigma);
  }
}

void Lightmap_Tool::compare_gpu_captures(editor_context_t& ctx)
{
  capture_comparison.reset();
  if (!renderer::ray_query_is_available()) return;

  const std::optional<shared::probe_grid_t> grid =
      shared::try_build_probe_grid(*ctx.map, settings.probe_spacing_in_world_units);
  if (!grid)
  {
    log_error("[lightmap-gpu] no probe grid at spacing {}; nothing to compare.",
              settings.probe_spacing_in_world_units);
    return;
  }

  const Bounding_Volume_Hierarchy bvh = shared::build_occluder_bvh(*ctx.map);
  const shared::traced_scene_t traced = shared::build_traced_scene(*ctx.map, bvh);
  const shared::gpu_bake_scene_t gpu_scene = shared::build_gpu_bake_scene(*ctx.map, traced);
  const std::vector<shared::baked_light_t> lights = shared::collect_lights(*ctx.map);
  const shared::batch_solver_scene_t scene{&gpu_scene, &bvh, &traced,
                                           Span<const shared::baked_light_t>(lights),
                                           shared::gpu_bake_settings_from(solve_settings)};
  const std::vector<uint8_t> inside = shared::classify_probes_inside_solid(*grid, bvh);
  const shared::reflection_capture_settings_t capture_settings{
      settings.reflection_spacing_in_world_units, solve_settings.directional_shadow_distance};
  const shared::reflection_capture_set_t set =
      shared::build_reflection_captures(*ctx.map, *grid, inside, bvh, capture_settings);
  if (set.empty())
  {
    log_error("[lightmap-gpu] no reflection captures at spacing {}; nothing to compare.",
              settings.reflection_spacing_in_world_units);
    return;
  }
  const std::vector<shared::gpu_sample_t> samples =
      shared::collect_capture_samples(set, settings.reflection_size_in_texels);
  capture_compare_capture_count = set.captures.size();
  capture_compare_record_count = samples.size();
  capture_compare_light_count = lights.size();

  using clock = std::chrono::steady_clock;
  const auto milliseconds_since = [](clock::time_point started) {
    return std::chrono::duration<double, std::milli>(clock::now() - started).count();
  };

  vulkan_batch_solver_t gpu;
  gpu.upload_scene(scene);
  if (!gpu.has_scene()) return;

  shared::gpu_capture_results_t from_gpu;
  const clock::time_point gpu_started = clock::now();
  gpu.solve_captures(samples, from_gpu);
  capture_compare_gpu_milliseconds = milliseconds_since(gpu_started);

  shared::cpu_batch_solver_t cpu;
  cpu.upload_scene(scene);
  shared::gpu_capture_results_t from_cpu;
  const clock::time_point cpu_started = clock::now();
  cpu.solve_captures(samples, from_cpu);
  capture_compare_cpu_milliseconds = milliseconds_since(cpu_started);

  std::vector<shared::gpu_sample_t> by_capture = samples;
  for (shared::gpu_sample_t& sample : by_capture)
    sample.chart_index = (uint32_t)shared::capture_index_of_record(
        sample.chart_index, settings.reflection_size_in_texels);
  std::vector<size_t> groups(set.captures.size());
  for (size_t i = 0; i < groups.size(); ++i) groups[i] = i;

  capture_comparison = shared::compare_capture_results(by_capture, groups, from_cpu.values,
                                                       from_gpu.values);
  const shared::record_comparison_report_t& report = *capture_comparison;

  log_terminal("[lightmap-gpu] captures: {} capture(s) at {}x{} a face, {} record(s), {} "
               "light(s), {} chain(s) each; CPU {:.1f} ms, GPU {:.1f} ms. Reference mean "
               "{:.5f}, mean |d| {:.6f} ({:.3f}%); {} record(s) differ in any coefficient; {} "
               "capture(s) beyond {} sigma.",
               set.captures.size(), settings.reflection_size_in_texels,
               settings.reflection_size_in_texels, samples.size(), lights.size(),
               solve_settings.indirect_rays_per_sample, capture_compare_cpu_milliseconds,
               capture_compare_gpu_milliseconds, report.reference_mean_over(0, 3),
               report.mean_absolute_difference_over(0, 3),
               report.reference_mean_over(0, 3) > 0.f
                   ? 100.0 * (double)report.mean_absolute_difference_over(0, 3) /
                         (double)report.reference_mean_over(0, 3)
                   : 0.0,
               report.differing_records, report.charts_beyond_tolerance,
               shared::RECORD_COMPARISON_SIGMA);
  for (size_t i = 0; i < std::min<size_t>(report.charts.size(), 8); ++i)
  {
    const shared::record_chart_comparison_t& capture = report.charts[i];
    const size_t k = (size_t)std::max(capture.largest_sigma_coefficient, 0);
    log_terminal("[lightmap-gpu]   capture {} at ({:.0f}, {:.0f}, {:.0f}) ({} rec): channel {} "
                 "CPU {:.6f} GPU {:.6f} +- {:.6f}, {:.1f} sigma",
                 capture.chart, set.captures[capture.chart].position.x,
                 set.captures[capture.chart].position.y, set.captures[capture.chart].position.z,
                 capture.record_count, k, capture.reference_mean[k], capture.candidate_mean[k],
                 capture.difference_standard_error[k], capture.largest_sigma);
  }
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

  cvars::cvar_state_t& cvars = *state_manager::get_client_context().cvars;
  ImGui::Checkbox("Bake on the GPU (r_lightmap_gpu)", &cvars.r_lightmap_gpu);
  ImGui::TextWrapped("next bake runs through %s", bake_path_for(cvars).description.c_str());

  if (ImGui::Button("Bake", {-1, 0}))
  {
    visibility_masks = {};
    const bake_path_t path = bake_path_for(cvars);
    std::optional<vulkan_batch_solver_t> gpu_solver;
    if (path.through_the_gpu) gpu_solver.emplace();

    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    shared::bake_lightmap(*ctx.map, baked, solve_settings,
                          gpu_solver ? &*gpu_solver : nullptr,
                          emit_per_light_visibility ? &visibility_masks : nullptr);
    const double bake_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    last_bake_line = std::format("last bake: {} in {:.2f} s", path.description, bake_seconds);
    log_terminal("[lightmap] {}", last_bake_line);

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

  if (!last_bake_line.empty()) ImGui::TextWrapped("%s", last_bake_line.c_str());

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

  // lightmap_gpu_plan.md step 4. The status line is what says which path a bake
  // CAN take on this machine, and the button is the ray query pin.
  ImGui::Separator();
  ImGui::Text("GPU bake");
  if (renderer::ray_query_is_available())
    ImGui::TextDisabled("ray query: available");
  else
    ImGui::TextWrapped("ray query: unavailable (%s); bakes run on the CPU",
                       renderer::ray_query_unavailable_reason());

  ImGui::BeginDisabled(!renderer::ray_query_is_available() || !has_packed());
  if (ImGui::Button("Probe GPU rays against the CPU BVH", {-1, 0})) probe_gpu_rays(ctx);
  ImGui::EndDisabled();

  if (probe_ray_report)
  {
    const shared::probe_ray_report_t& report = *probe_ray_report;
    if (report.agrees())
      ImGui::TextColored({0.4f, 1.f, 0.4f, 1.f}, "GPU and CPU agree on all %zu rays",
                         report.sample_count);
    else
      ImGui::TextColored({1.f, 0.4f, 0.4f, 1.f}, "%zu disagreement(s) over %zu rays",
                         report.reference_only_hit + report.candidate_only_hit +
                             report.hits_outside_tolerance,
                         report.sample_count);
    ImGui::Text("%zu hit on both, %zu missed on both", report.both_hit, report.both_missed);
    ImGui::Text("%zu started inside a solid (CPU answers 0, GPU flies through)",
                report.reference_started_inside_a_solid);
    ImGui::Text("%zu CPU-only hits, %zu GPU-only hits", report.reference_only_hit,
                report.candidate_only_hit);
    ImGui::Text("%zu apart by more than %.2f (%zu GPU farther, %zu GPU nearer)",
                report.hits_outside_tolerance, PROBE_RAY_DISTANCE_TOLERANCE,
                report.candidate_farther, report.candidate_nearer);
    ImGui::Text("distance error: largest %.4f, mean %.6f", report.largest_distance_error,
                report.mean_distance_error);
    if (!probe_ray_worst_line.empty()) ImGui::TextWrapped("%s", probe_ray_worst_line.c_str());
    ImGui::Text("%u triangles; GPU %.1f ms, CPU %.1f ms", probe_ray_triangle_count,
                probe_ray_gpu_milliseconds, probe_ray_cpu_milliseconds);
  }

  // lightmap_gpu_plan.md step 5. Both solvers over the bake's own records at the
  // current chain count, whatever the trace switch says: the switch decides what
  // a BAKE stores, this asks what the two kernels ANSWER.
  ImGui::BeginDisabled(!renderer::ray_query_is_available() || !has_packed());
  if (ImGui::Button("Compare GPU indirect against the CPU chain", {-1, 0}))
    compare_gpu_indirect(ctx);
  ImGui::EndDisabled();

  if (indirect_comparison)
  {
    const shared::record_comparison_report_t& report = *indirect_comparison;
    if (report.agrees())
      ImGui::TextColored({0.4f, 1.f, 0.4f, 1.f},
                         "GPU and CPU agree within %.0f sigma on all %zu charts",
                         shared::RECORD_COMPARISON_SIGMA, report.charts.size());
    else
      ImGui::TextColored({1.f, 0.4f, 0.4f, 1.f}, "%zu of %zu chart(s) beyond %.0f sigma",
                         report.charts_beyond_tolerance, report.charts.size(),
                         shared::RECORD_COMPARISON_SIGMA);
    ImGui::Text("%zu records, %d chains each; CPU %.1f ms, GPU %.1f ms", report.record_count,
                solve_settings.indirect_rays_per_sample, indirect_compare_cpu_milliseconds,
                indirect_compare_gpu_milliseconds);
    ImGui::Text("reference L0 mean %.5f; mean |dL0| %.6f (%.3f%%)", report.reference_mean_over(0, 3),
                report.mean_absolute_difference_over(0, 3),
                report.reference_mean_over(0, 3) > 0.f
                    ? 100.0 * (double)report.mean_absolute_difference_over(0, 3) /
                          (double)report.reference_mean_over(0, 3)
                    : 0.0);
    ImGui::Text("%zu records with a non-zero answer; %zu differ in any coefficient",
                report.reference_nonzero_records, report.differing_records);
    for (size_t i = 0; i < std::min<size_t>(report.charts.size(), 5); ++i)
    {
      const shared::record_chart_comparison_t& chart = report.charts[i];
      const int k = std::max(chart.largest_sigma_coefficient, 0);
      ImGui::Text("  chart %zu (uid %u, %zu rec): %s CPU %.5f GPU %.5f +- %.5f, %.1f sigma",
                  chart.chart, baked.charts[chart.chart].object_uid, chart.record_count,
                  SH_COEFFICIENT_NAMES[k], chart.reference_mean[(uint32_t)k],
                  chart.candidate_mean[(uint32_t)k],
                  chart.difference_standard_error[(uint32_t)k], chart.largest_sigma);
    }
    ImGui::TextDisabled("PNGs: %s, %s, %s", COMPARE_CPU_IMAGE_PREFIX, COMPARE_GPU_IMAGE_PREFIX,
                        COMPARE_DIFFERENCE_IMAGE_PREFIX);
  }

  // lightmap_gpu_plan.md step 6. Both solvers' direct term over the same
  // records, every light admitted into the sum.
  ImGui::BeginDisabled(!renderer::ray_query_is_available() || !has_packed());
  if (ImGui::Button("Compare GPU direct against the CPU shade", {-1, 0}))
    compare_gpu_direct(ctx);
  ImGui::EndDisabled();

  if (direct_comparison)
  {
    const shared::record_comparison_report_t& report = *direct_comparison;
    if (report.agrees())
      ImGui::TextColored({0.4f, 1.f, 0.4f, 1.f},
                         "GPU and CPU agree within %.0f sigma on all %zu charts",
                         shared::RECORD_COMPARISON_SIGMA, report.charts.size());
    else
      ImGui::TextColored({1.f, 0.4f, 0.4f, 1.f}, "%zu of %zu chart(s) beyond %.0f sigma",
                         report.charts_beyond_tolerance, report.charts.size(),
                         shared::RECORD_COMPARISON_SIGMA);
    ImGui::Text("%zu records, %zu lights; CPU %.1f ms (%zu rays), GPU %.1f ms (%zu rays)",
                report.record_count, direct_compare_light_count, direct_compare_cpu_milliseconds,
                direct_compare_cpu_rays, direct_compare_gpu_milliseconds,
                direct_compare_gpu_rays);
    ImGui::Text("reference irradiance mean %.5f; mean |dE| %.6f (%.3f%%)",
                report.reference_mean_over(0, 3), report.mean_absolute_difference_over(0, 3),
                report.reference_mean_over(0, 3) > 0.f
                    ? 100.0 * (double)report.mean_absolute_difference_over(0, 3) /
                          (double)report.reference_mean_over(0, 3)
                    : 0.0);
    ImGui::Text("%zu records lit by something; %zu differ in any coefficient",
                report.reference_nonzero_records, report.differing_records);
    for (size_t i = 0; i < std::min<size_t>(report.charts.size(), 5); ++i)
    {
      const shared::record_chart_comparison_t& chart = report.charts[i];
      const size_t k = (size_t)std::max(chart.largest_sigma_coefficient, 0);
      char name[shared::DIRECT_COEFFICIENT_NAME_CAPACITY];
      const std::string_view coefficient =
          shared::direct_coefficient_name(k, direct_compare_light_count, Span<char>(name));
      ImGui::Text("  chart %zu (uid %u, %zu rec): %.*s CPU %.5f GPU %.5f +- %.5f, %.1f sigma",
                  chart.chart, baked.charts[chart.chart].object_uid, chart.record_count,
                  (int)coefficient.size(), coefficient.data(), chart.reference_mean[k],
                  chart.candidate_mean[k], chart.difference_standard_error[k],
                  chart.largest_sigma);
    }
    ImGui::TextDisabled("PNGs: %s, %s, %s", COMPARE_DIRECT_CPU_IMAGE_PREFIX,
                        COMPARE_DIRECT_GPU_IMAGE_PREFIX, COMPARE_DIRECT_DIFFERENCE_IMAGE_PREFIX);
  }

  // lightmap_gpu_plan.md step 7. Both solvers' probe term over the grid the
  // panel's spacing implies; no atlas needed.
  ImGui::BeginDisabled(!renderer::ray_query_is_available() || ctx.map->geometry.empty());
  if (ImGui::Button("Compare GPU probes against the CPU trace", {-1, 0}))
    compare_gpu_probes(ctx);
  ImGui::EndDisabled();

  if (probe_comparison)
  {
    const shared::record_comparison_report_t& report = *probe_comparison;
    if (report.agrees())
      ImGui::TextColored({0.4f, 1.f, 0.4f, 1.f},
                         "GPU and CPU agree within %.0f sigma on all %zu slices",
                         shared::RECORD_COMPARISON_SIGMA, report.charts.size());
    else
      ImGui::TextColored({1.f, 0.4f, 0.4f, 1.f}, "%zu of %zu slice(s) beyond %.0f sigma",
                         report.charts_beyond_tolerance, report.charts.size(),
                         shared::RECORD_COMPARISON_SIGMA);
    ImGui::Text("%zu open probes of %dx%dx%d, %zu lights, %d chains each; CPU %.1f ms, GPU "
                "%.1f ms",
                probe_compare_open_count, probe_compare_grid_count.x,
                probe_compare_grid_count.y, probe_compare_grid_count.z,
                probe_compare_light_count, solve_settings.indirect_rays_per_sample,
                probe_compare_cpu_milliseconds, probe_compare_gpu_milliseconds);
    ImGui::Text("reference L0 mean %.5f; mean |dL0| %.6f (%.3f%%)", report.reference_mean_over(0, 3),
                report.mean_absolute_difference_over(0, 3),
                report.reference_mean_over(0, 3) > 0.f
                    ? 100.0 * (double)report.mean_absolute_difference_over(0, 3) /
                          (double)report.reference_mean_over(0, 3)
                    : 0.0);
    ImGui::Text("visibility mean %.4f; mean |dV| %.6f",
                report.reference_mean_over(shared::SH_L1_COEFFICIENT_COUNT,
                                           shared::PROBE_VISIBILITY_CHANNELS),
                report.mean_absolute_difference_over(shared::SH_L1_COEFFICIENT_COUNT,
                                                     shared::PROBE_VISIBILITY_CHANNELS));
    ImGui::Text("%zu records with a non-zero answer; %zu differ in any coefficient",
                report.reference_nonzero_records, report.differing_records);
    for (size_t i = 0; i < std::min<size_t>(report.charts.size(), 5); ++i)
    {
      const shared::record_chart_comparison_t& slice = report.charts[i];
      const size_t k = (size_t)std::max(slice.largest_sigma_coefficient, 0);
      ImGui::Text("  slice z=%zu (%zu probes): %s CPU %.5f GPU %.5f +- %.5f, %.1f sigma",
                  slice.chart, slice.record_count, shared::probe_coefficient_name(k),
                  slice.reference_mean[k], slice.candidate_mean[k],
                  slice.difference_standard_error[k], slice.largest_sigma);
    }
  }

  // Gate 6 step 2: both solvers' capture term over the lattice the panel's
  // spacings imply.
  ImGui::BeginDisabled(!renderer::ray_query_is_available() || ctx.map->geometry.empty());
  if (ImGui::Button("Compare GPU captures against the CPU trace", {-1, 0}))
    compare_gpu_captures(ctx);
  ImGui::EndDisabled();

  if (capture_comparison)
  {
    const shared::record_comparison_report_t& report = *capture_comparison;
    if (report.agrees())
      ImGui::TextColored({0.4f, 1.f, 0.4f, 1.f},
                         "GPU and CPU agree within %.0f sigma on all %zu captures",
                         shared::RECORD_COMPARISON_SIGMA, report.charts.size());
    else
      ImGui::TextColored({1.f, 0.4f, 0.4f, 1.f}, "%zu of %zu capture(s) beyond %.0f sigma",
                         report.charts_beyond_tolerance, report.charts.size(),
                         shared::RECORD_COMPARISON_SIGMA);
    ImGui::Text("%zu captures, %zu texel records, %zu lights, %d chains each; CPU %.1f ms, "
                "GPU %.1f ms",
                capture_compare_capture_count, capture_compare_record_count,
                capture_compare_light_count, solve_settings.indirect_rays_per_sample,
                capture_compare_cpu_milliseconds, capture_compare_gpu_milliseconds);
    ImGui::Text("reference mean %.5f; mean |d| %.6f (%.3f%%)", report.reference_mean_over(0, 3),
                report.mean_absolute_difference_over(0, 3),
                report.reference_mean_over(0, 3) > 0.f
                    ? 100.0 * (double)report.mean_absolute_difference_over(0, 3) /
                          (double)report.reference_mean_over(0, 3)
                    : 0.0);
    ImGui::Text("%zu records with a non-zero answer; %zu differ in any coefficient",
                report.reference_nonzero_records, report.differing_records);
    for (size_t i = 0; i < std::min<size_t>(report.charts.size(), 5); ++i)
    {
      const shared::record_chart_comparison_t& capture = report.charts[i];
      const size_t k = (size_t)std::max(capture.largest_sigma_coefficient, 0);
      ImGui::Text("  capture %zu (%zu rec): channel %zu CPU %.5f GPU %.5f +- %.5f, %.1f sigma",
                  capture.chart, capture.record_count, k, capture.reference_mean[k],
                  capture.candidate_mean[k], capture.difference_standard_error[k],
                  capture.largest_sigma);
    }
  }

  ImGui::Separator();
  ImGui::Text("Irradiance probes");

  const bool spacing_changed =
      ImGui::SliderFloat("Probe spacing", &settings.probe_spacing_in_world_units, 16.f,
                         512.f, "%.0f", ImGuiSliderFlags_Logarithmic);
  ImGui::Checkbox("Bake probes", &solve_settings.bake_probes);

  ImGui::Separator();
  ImGui::Text("Reflection captures");
  ImGui::SliderFloat("Capture spacing", &settings.reflection_spacing_in_world_units, 64.f,
                     2048.f, "%.0f", ImGuiSliderFlags_Logarithmic);
  ImGui::SliderInt("Capture face size", &settings.reflection_size_in_texels, 8, 128);
  ImGui::Checkbox("Bake reflection captures", &solve_settings.bake_reflection_captures);
  if (solve_settings.bake_reflection_captures && !solve_settings.trace_indirect_light)
    ImGui::TextColored({1.f, 0.55f, 0.2f, 1.f},
                       "Captures are traced: turn on \"Trace indirect light\" or none are baked.");
  if (baked.reflections.baked())
    ImGui::Text("Last bake: %zu capture(s) at %dx%d a face", baked.reflections.captures.size(),
                baked.reflections.captures.front().cube.size_in_texels,
                baked.reflections.captures.front().cube.size_in_texels);
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
