#include "lightmap_gpu.hpp"

#include "asset.hpp"
#include "log.hpp"
#include "map_geometry.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <limits>
#include <thread>

namespace shared
{

namespace
{

// The same usability test sample_texture applies before it reads a pixel: a
// texture failing it is the fallback colour there and GPU_NO_TEXTURE here.
bool texture_is_usable(const assets::texture_asset_t *texture)
{
  return texture && texture->width > 0 && texture->height > 0 && texture->channels >= 3 &&
         texture->pixels.size() >=
             (size_t)texture->width * (size_t)texture->height * (size_t)texture->channels;
}

// One entry per distinct texture, in first-use order.
uint32_t texture_index_for(std::vector<const assets::texture_asset_t *> &textures,
                           const assets::texture_asset_t *texture)
{
  if (!texture_is_usable(texture)) return GPU_NO_TEXTURE;
  for (size_t index = 0; index < textures.size(); ++index)
    if (textures[index] == texture) return (uint32_t)index;
  textures.push_back(texture);
  return (uint32_t)(textures.size() - 1);
}

void append_triangle(gpu_bake_scene_t &scene, entity_uid_t uid, const linalg::vec3 &a,
                     const linalg::vec3 &b, const linalg::vec3 &c, const linalg::vec2 &uv_a,
                     const linalg::vec2 &uv_b, const linalg::vec2 &uv_c, uint32_t material)
{
  const uint32_t base = (uint32_t)scene.vertices.size();
  scene.vertices.push_back({a.x, a.y, a.z, 0.f});
  scene.vertices.push_back({b.x, b.y, b.z, 0.f});
  scene.vertices.push_back({c.x, c.y, c.z, 0.f});
  scene.indices.push_back(base);
  scene.indices.push_back(base + 1);
  scene.indices.push_back(base + 2);

  gpu_triangle_t triangle;
  triangle.material = material;
  triangle.uv0 = uv_a;
  triangle.uv1 = uv_b;
  triangle.uv2 = uv_c;
  scene.triangles.push_back(triangle);
  scene.triangle_object_uids.push_back(uid);
}

void append_brush(gpu_bake_scene_t &scene, const map_t &map, entity_uid_t uid,
                  const brush_geometry_t &brush)
{
  const assets::mesh_asset_t mesh = generate_brush_mesh(brush, map.materials);

  // Keyed by PLANE, the face's identity, through the same find_face_surface the
  // tracer's surface_at asks at a hit -- so the material a triangle carries is
  // the one the CPU would have resolved there. A brush whose face_surfaces are
  // empty answers with material 0, the map default, exactly as surface_at does.
  const face_surface_t brush_default;

  for (size_t first = 0; first + 2 < mesh.indices.size(); first += 3)
  {
    const vertex_xnu &a = mesh.vertices[mesh.indices[first]];
    const vertex_xnu &b = mesh.vertices[mesh.indices[first + 1]];
    const vertex_xnu &c = mesh.vertices[mesh.indices[first + 2]];

    linalg::vec3 normal = linalg::cross(b.position - a.position, c.position - a.position);
    const float length = linalg::length(normal);
    if (length <= 1e-6f) continue;
    normal = normal * (1.f / length);

    const face_surface_t *matched = find_face_surface(brush, Plane{a.position, normal});
    const face_surface_t &face = matched ? *matched : brush_default;
    const uint32_t material = face.material < scene.untextured_material
                                  ? (uint32_t)face.material
                                  : scene.untextured_material;

    append_triangle(scene, uid, a.position, b.position, c.position, a.uv, b.uv, c.uv,
                    material);
  }
}

// A static mesh is untextured to the tracer -- surface_at resolves brushes only
// -- and it is untextured here, carrying its real uvs so the day a mesh material
// arrives it is one table entry and not a new record.
void append_static_mesh(gpu_bake_scene_t &scene, entity_uid_t uid,
                        const static_mesh_geometry_t &static_mesh)
{
  const std::vector<world_triangle_t> triangles = static_mesh_world_triangles(static_mesh);
  const assets::mesh_asset_t *asset = assets::get(resolve_surface_mesh(static_mesh.surface));
  if (triangles.empty() || !asset)
  {
    log_warning("[lightmap] static mesh {} names no mesh that resolves; the GPU scene "
                "has no triangles for it.", uid);
    return;
  }

  for (size_t triangle = 0; triangle < triangles.size(); ++triangle)
  {
    const world_triangle_t &world = triangles[triangle];
    if (world.is_degenerate()) continue;

    const size_t first = triangle * 3;
    const linalg::vec2 uv_a = asset->vertices[asset->indices[first]].uv;
    const linalg::vec2 uv_b = asset->vertices[asset->indices[first + 1]].uv;
    const linalg::vec2 uv_c = asset->vertices[asset->indices[first + 2]].uv;

    append_triangle(scene, uid, world.corners[0], world.corners[1], world.corners[2], uv_a,
                    uv_b, uv_c, scene.untextured_material);
  }
}

} // namespace

gpu_bake_scene_t build_gpu_bake_scene(const map_t &map, const traced_scene_t &traced)
{
  gpu_bake_scene_t scene;

  scene.materials.reserve(traced.materials.size() + 1);
  for (const traced_scene_t::material_t &material : traced.materials)
    scene.materials.push_back({texture_index_for(scene.textures, material.albedo),
                               texture_index_for(scene.textures, material.emissive), 0, 0});

  scene.untextured_material = (uint32_t)scene.materials.size();
  scene.materials.push_back({GPU_NO_TEXTURE, GPU_NO_TEXTURE, 0, 0});

  for (const map_geometry_t &entry : map.geometry)
  {
    if (const brush_geometry_t *brush = std::get_if<brush_geometry_t>(&entry.value))
      append_brush(scene, map, entry.uid, *brush);
    else if (const static_mesh_geometry_t *static_mesh =
                 std::get_if<static_mesh_geometry_t>(&entry.value))
      append_static_mesh(scene, entry.uid, *static_mesh);
  }

  return scene;
}

traced_surface_t gpu_surface_at(const gpu_bake_scene_t &scene, uint32_t triangle_index,
                                const linalg::vec2 &uv)
{
  constexpr linalg::vec3 untextured{UNTEXTURED_BOUNCE_ALBEDO, UNTEXTURED_BOUNCE_ALBEDO,
                                    UNTEXTURED_BOUNCE_ALBEDO};
  if (triangle_index >= scene.triangles.size()) return {untextured, {}};

  const uint32_t material_index = scene.triangles[triangle_index].material;
  if (material_index >= scene.materials.size()) return {untextured, {}};
  const gpu_material_t &material = scene.materials[material_index];

  const auto texture_at = [&](uint32_t index, const linalg::vec3 &fallback) {
    if (index == GPU_NO_TEXTURE || index >= scene.textures.size()) return fallback;
    return sample_texture(*scene.textures[index], uv, fallback);
  };
  return {texture_at(material.albedo_texture, untextured),
          texture_at(material.emissive_texture, {0.f, 0.f, 0.f})};
}

linalg::vec2 gpu_triangle_uv_at(const gpu_bake_scene_t &scene, uint32_t triangle_index,
                                const linalg::vec3 &position)
{
  if (triangle_index >= scene.triangles.size()) return {0.f, 0.f};
  const gpu_triangle_t &triangle = scene.triangles[triangle_index];

  const auto corner = [&](size_t offset) {
    const linalg::vec4 &vertex =
        scene.vertices[scene.indices[(size_t)triangle_index * 3 + offset]];
    return linalg::vec3{vertex.x, vertex.y, vertex.z};
  };
  const linalg::vec3 a = corner(0);
  const linalg::vec3 b = corner(1);
  const linalg::vec3 c = corner(2);

  const linalg::vec3 edge_ab = b - a;
  const linalg::vec3 edge_ac = c - a;
  const linalg::vec3 to_point = position - a;
  const float dot_bb = linalg::dot(edge_ab, edge_ab);
  const float dot_bc = linalg::dot(edge_ab, edge_ac);
  const float dot_cc = linalg::dot(edge_ac, edge_ac);
  const float dot_pb = linalg::dot(to_point, edge_ab);
  const float dot_pc = linalg::dot(to_point, edge_ac);
  const float denominator = dot_bb * dot_cc - dot_bc * dot_bc;
  if (std::abs(denominator) <= 1e-12f) return triangle.uv0;

  const float weight_b = (dot_cc * dot_pb - dot_bc * dot_pc) / denominator;
  const float weight_c = (dot_bb * dot_pc - dot_bc * dot_pb) / denominator;
  const float weight_a = 1.f - weight_b - weight_c;

  return {triangle.uv0.x * weight_a + triangle.uv1.x * weight_b + triangle.uv2.x * weight_c,
          triangle.uv0.y * weight_a + triangle.uv1.y * weight_b + triangle.uv2.y * weight_c};
}

// --- The seam --------------------------------------------------------------

gpu_bake_settings_t gpu_bake_settings_from(const lightmap_solve_settings_t &solve_settings)
{
  gpu_bake_settings_t settings;
  settings.mode = solve_settings.mode;
  settings.rays_per_sample = solve_settings.indirect_rays_per_sample;
  settings.bounces_before_roulette = solve_settings.indirect_bounces_before_roulette;
  settings.max_bounces = solve_settings.indirect_max_bounces;
  // The same number for both biases because it is the same problem: a ray
  // starting exactly on a face re-enters the solid it left.
  settings.ray_bias = solve_settings.shadow_ray_bias;
  settings.shadow_ray_bias = solve_settings.shadow_ray_bias;
  settings.soft_shadow_samples = solve_settings.soft_shadow_samples;
  settings.directional_shadow_distance = solve_settings.directional_shadow_distance;
  return settings;
}

indirect_trace_settings_t indirect_trace_settings_from(const gpu_bake_settings_t &settings)
{
  indirect_trace_settings_t indirect;
  indirect.rays_per_sample = settings.rays_per_sample;
  indirect.bounces_before_roulette = settings.bounces_before_roulette;
  indirect.max_bounces = settings.max_bounces;
  indirect.ray_bias = settings.ray_bias;
  indirect.shadow_ray_bias = settings.shadow_ray_bias;
  indirect.soft_shadow_samples = settings.soft_shadow_samples;
  indirect.directional_shadow_distance = settings.directional_shadow_distance;
  return indirect;
}

namespace
{

// Records are independent, so a batch is handed out in fixed slices to however
// many workers it is worth having; a slice boundary moves no result. Statistics
// are per worker and summed after the join, like the reference path's.
template <typename Shade_One>
shade_statistics_t shade_in_slices(size_t record_count, unsigned int requested_workers,
                                   Shade_One &&shade_one)
{
  constexpr size_t SLICE_IN_RECORDS = 256;

  unsigned int worker_count =
      requested_workers ? requested_workers : std::thread::hardware_concurrency();
  if (worker_count == 0) worker_count = 1;
  const size_t slice_count = (record_count + SLICE_IN_RECORDS - 1) / SLICE_IN_RECORDS;
  worker_count = (unsigned int)std::min<size_t>(worker_count, std::max<size_t>(slice_count, 1));

  std::vector<shade_statistics_t> per_worker(worker_count);
  std::atomic<size_t> next_slice{0};

  const auto run = [&](unsigned int worker) {
    for (;;)
    {
      const size_t begin = next_slice.fetch_add(SLICE_IN_RECORDS, std::memory_order_relaxed);
      if (begin >= record_count) return;
      const size_t end = std::min(begin + SLICE_IN_RECORDS, record_count);
      for (size_t i = begin; i < end; ++i) shade_one(i, per_worker[worker]);
    }
  };

  std::vector<std::thread> workers;
  workers.reserve(worker_count - 1);
  for (unsigned int i = 1; i < worker_count; ++i) workers.emplace_back(run, i);
  run(0);
  for (std::thread &worker : workers) worker.join();

  shade_statistics_t total;
  for (const shade_statistics_t &worker : per_worker) total.add(worker);
  return total;
}

} // namespace

void cpu_batch_solver_t::upload_scene(const batch_solver_scene_t &uploaded)
{
  if (!uploaded.gpu_scene || !uploaded.bvh || !uploaded.traced)
    fatal_error("[lightmap] the CPU batch solver was handed a scene with a null half "
                "(triangles {}, bvh {}, traced {}).",
                (const void *)uploaded.gpu_scene, (const void *)uploaded.bvh,
                (const void *)uploaded.traced);
  if (uploaded.lights.size() > LIGHT_MASK_BITS)
    fatal_error("[lightmap] {} baked lights, and a chart's light mask holds {} bits.",
                uploaded.lights.size(), LIGHT_MASK_BITS);

  scene = uploaded;
  indirect = indirect_trace_settings_from(uploaded.settings);
  accumulated = {};
}

void cpu_batch_solver_t::solve_direct(Span<const gpu_sample_t> samples,
                                      Span<const uint64_t> chart_light_masks,
                                      gpu_direct_results_t &out)
{
  if (!scene.bvh)
    fatal_error("[lightmap] the CPU batch solver was asked to shade before upload_scene.");

  const size_t light_count = scene.lights.size();
  out.resize(samples.size(), light_count);

  const shade_statistics_t added = shade_in_slices(
      samples.size(), worker_count, [&](size_t i, shade_statistics_t &statistics) {
        const gpu_sample_t &sample = samples[(uint32_t)i];
        if (sample.chart_index >= chart_light_masks.size())
          fatal_error("[lightmap] record {} names chart {} of a batch carrying {} chart "
                      "masks.",
                      i, sample.chart_index, chart_light_masks.size());

        shade_sample_direct(sample, scene.lights, *scene.bvh, scene.settings,
                            chart_light_masks[sample.chart_index], out.irradiance[i],
                            Span<float>(out.coverage.data() + i * light_count,
                                        (uint32_t)light_count),
                            Span<float>(out.weight.data() + i * light_count,
                                        (uint32_t)light_count),
                            statistics);
      });

  accumulated.shade.add(added);
  ++accumulated.direct_dispatches;
}

void cpu_batch_solver_t::solve_indirect(Span<const gpu_sample_t> samples,
                                        gpu_indirect_results_t &out)
{
  if (!scene.traced)
    fatal_error("[lightmap] the CPU batch solver was asked to trace before upload_scene.");

  out.values.assign(samples.size(), indirect_sh_l1_t{});

  const shade_statistics_t added = shade_in_slices(
      samples.size(), worker_count, [&](size_t i, shade_statistics_t &statistics) {
        out.values[i] = shade_sample_indirect(samples[(uint32_t)i], *scene.traced,
                                              scene.lights, indirect, statistics);
      });

  accumulated.shade.add(added);
  ++accumulated.indirect_dispatches;
}

// --- Step 4's pin ------------------------------------------------------------

std::vector<float> probe_ray_distances(const Bounding_Volume_Hierarchy &bvh,
                                       Span<const gpu_sample_t> samples, float ray_bias,
                                       float max_distance)
{
  std::vector<float> distances(samples.size(), -1.f);
  for (uint32_t i = 0; i < samples.size(); ++i)
  {
    const gpu_sample_t &sample = samples[i];
    ray_hit_result_t hit{};
    if (!bvh_intersect_ray(bvh, sample.position + sample.normal * ray_bias, sample.normal, hit))
      continue;
    if (hit.t < 0.f || hit.t > max_distance) continue;
    distances[i] = hit.t;
  }
  return distances;
}

probe_ray_report_t compare_probe_rays(Span<const float> reference, Span<const float> candidate,
                                      float distance_tolerance)
{
  if (reference.size() != candidate.size())
    fatal_error("[lightmap-gpu] comparing {} reference distances against {} candidates.",
                reference.size(), candidate.size());

  probe_ray_report_t report;
  report.sample_count = reference.size();
  double error_sum = 0.0;
  size_t compared = 0;
  for (uint32_t i = 0; i < reference.size(); ++i)
  {
    const bool reference_hit = reference[i] >= 0.f;
    const bool candidate_hit = candidate[i] >= 0.f;
    if (reference_hit && candidate_hit)
    {
      ++report.both_hit;
      if (reference[i] == 0.f)
      {
        ++report.reference_started_inside_a_solid;
        continue;
      }
      ++compared;
      const float error = std::abs(candidate[i] - reference[i]);
      error_sum += error;
      if (error > report.largest_distance_error)
      {
        report.largest_distance_error = error;
        report.worst_sample = (int64_t)i;
      }
      if (error > distance_tolerance)
      {
        ++report.hits_outside_tolerance;
        if (candidate[i] > reference[i]) ++report.candidate_farther;
        else ++report.candidate_nearer;
      }
    }
    else if (!reference_hit && !candidate_hit) ++report.both_missed;
    else if (reference_hit) ++report.reference_only_hit;
    else ++report.candidate_only_hit;
  }
  report.mean_distance_error = compared ? (float)(error_sum / (double)compared) : 0.f;
  return report;
}

// --- The pins of steps 5 and 6 ----------------------------------------------

namespace
{

float coefficient_of(const indirect_sh_l1_t &value, size_t coefficient)
{
  const linalg::vec3 &channel =
      coefficient < 3 ? value.l0 : value.l1[(uint32_t)(coefficient / 3 - 1)];
  switch (coefficient % 3)
  {
  case 0: return channel.x;
  case 1: return channel.y;
  default: return channel.z;
  }
}

float coefficient_of(const gpu_direct_results_t &results, size_t record, size_t coefficient)
{
  if (coefficient < 3)
  {
    const linalg::vec3 &irradiance = results.irradiance[record];
    return coefficient == 0 ? irradiance.x : coefficient == 1 ? irradiance.y : irradiance.z;
  }
  const size_t per_light = coefficient - 3;
  const size_t light_count = results.light_count;
  if (per_light < light_count) return results.coverage[record * light_count + per_light];
  return results.weight[record * light_count + (per_light - light_count)];
}

// The paired test itself, over any record type: `reference_at(i, k)` and
// `candidate_at(i, k)` read coefficient k of record i.
template <typename Reference_At, typename Candidate_At>
record_comparison_report_t compare_records(Span<const gpu_sample_t> samples,
                                           Span<const size_t> charts, size_t coefficient_count,
                                           Span<const uint32_t> scale_group,
                                           Reference_At &&reference_at,
                                           Candidate_At &&candidate_at)
{
  if (scale_group.size() != coefficient_count)
    fatal_error("[lightmap-gpu] {} scale groups for {} coefficients.", scale_group.size(),
                coefficient_count);
  size_t group_count = 0;
  for (const uint32_t group : scale_group) group_count = std::max<size_t>(group_count, group + 1);

  // Sums in double: a chart is thousands of records and a variance is a
  // difference of two sums that nearly cancel.
  struct chart_sums_t
  {
    size_t count = 0;
    std::vector<double> reference;
    std::vector<double> candidate;
    std::vector<double> difference;
    std::vector<double> squared_difference;
  };
  std::vector<chart_sums_t> sums(charts.size());
  for (chart_sums_t &chart : sums)
  {
    chart.reference.assign(coefficient_count, 0.0);
    chart.candidate.assign(coefficient_count, 0.0);
    chart.difference.assign(coefficient_count, 0.0);
    chart.squared_difference.assign(coefficient_count, 0.0);
  }

  record_comparison_report_t report;
  report.record_count = samples.size();
  report.chart_count = charts.size();
  report.coefficient_count = coefficient_count;

  std::vector<double> reference_sum(coefficient_count, 0.0);
  std::vector<double> absolute_reference_sum(coefficient_count, 0.0);
  std::vector<double> absolute_difference_sum(coefficient_count, 0.0);
  report.largest_absolute_difference.assign(coefficient_count, 0.f);
  for (uint32_t i = 0; i < samples.size(); ++i)
  {
    const uint32_t chart_index = samples[i].chart_index;
    if (chart_index >= charts.size())
      fatal_error("[lightmap-gpu] record {} names chart {} of a set holding {}.", i,
                  chart_index, charts.size());
    chart_sums_t &chart = sums[chart_index];
    ++chart.count;
    bool reference_nonzero = false;
    bool differs = false;
    for (size_t k = 0; k < coefficient_count; ++k)
    {
      const double from_reference = reference_at(i, k);
      const double from_candidate = candidate_at(i, k);
      const double difference = from_candidate - from_reference;
      chart.reference[k] += from_reference;
      chart.candidate[k] += from_candidate;
      chart.difference[k] += difference;
      chart.squared_difference[k] += difference * difference;
      reference_sum[k] += from_reference;
      absolute_reference_sum[k] += std::abs(from_reference);
      absolute_difference_sum[k] += std::abs(difference);
      report.largest_absolute_difference[k] =
          std::max(report.largest_absolute_difference[k], (float)std::abs(difference));
      reference_nonzero |= from_reference != 0.0;
      differs |= difference != 0.0;
    }
    report.reference_nonzero_records += reference_nonzero ? 1 : 0;
    report.differing_records += differs ? 1 : 0;
  }

  const double record_count = (double)samples.size();
  report.reference_mean.assign(coefficient_count, 0.f);
  report.mean_absolute_difference.assign(coefficient_count, 0.f);
  std::vector<double> group_sum(group_count, 0.0);
  std::vector<double> group_values(group_count, 0.0);
  for (size_t k = 0; k < coefficient_count; ++k)
  {
    if (record_count > 0.0)
    {
      report.reference_mean[k] = (float)(reference_sum[k] / record_count);
      report.mean_absolute_difference[k] = (float)(absolute_difference_sum[k] / record_count);
    }
    group_sum[scale_group[(uint32_t)k]] += absolute_reference_sum[k];
    group_values[scale_group[(uint32_t)k]] += record_count;
  }
  report.group_scale.assign(group_count, 0.f);
  for (size_t group = 0; group < group_count; ++group)
    if (group_values[group] > 0.0)
      report.group_scale[group] = (float)(group_sum[group] / group_values[group]);

  for (uint32_t chart_index = 0; chart_index < charts.size(); ++chart_index)
  {
    const chart_sums_t &chart = sums[chart_index];
    if (chart.count == 0) continue;

    record_chart_comparison_t compared;
    compared.chart = charts[chart_index];
    compared.record_count = chart.count;
    compared.reference_mean.resize(coefficient_count);
    compared.candidate_mean.resize(coefficient_count);
    compared.difference_standard_error.resize(coefficient_count);
    const double n = (double)chart.count;

    for (size_t k = 0; k < coefficient_count; ++k)
    {
      const double mean_difference = chart.difference[k] / n;
      // The sample variance of the differences over n - 1, so one record has no
      // spread to speak of and reports a standard error of zero.
      const double variance =
          chart.count > 1 ? std::max(0.0, (chart.squared_difference[k] -
                                           n * mean_difference * mean_difference) /
                                              (n - 1.0))
                          : 0.0;
      const double standard_error = std::sqrt(variance / n);

      compared.reference_mean[k] = (float)(chart.reference[k] / n);
      compared.candidate_mean[k] = (float)(chart.candidate[k] / n);
      compared.difference_standard_error[k] = (float)standard_error;

      const double floor = (double)RECORD_COMPARISON_RELATIVE_FLOOR *
                           (double)report.group_scale[scale_group[(uint32_t)k]];
      double sigma = 0.0;
      if (std::abs(mean_difference) > floor)
        sigma = standard_error > 0.0 ? std::abs(mean_difference) / standard_error
                                     : std::numeric_limits<double>::infinity();
      if (sigma > compared.largest_sigma)
      {
        compared.largest_sigma = (float)sigma;
        compared.largest_sigma_coefficient = (int)k;
      }
    }

    if (compared.largest_sigma > RECORD_COMPARISON_SIGMA) ++report.charts_beyond_tolerance;
    report.charts.push_back(std::move(compared));
  }

  const auto brightness_of = [](const record_chart_comparison_t &chart) {
    double sum = 0.0;
    for (const float mean : chart.reference_mean) sum += std::abs(mean);
    return sum;
  };
  std::sort(report.charts.begin(), report.charts.end(),
            [&](const record_chart_comparison_t &left, const record_chart_comparison_t &right) {
              if (left.largest_sigma != right.largest_sigma)
                return left.largest_sigma > right.largest_sigma;
              return brightness_of(left) > brightness_of(right);
            });
  return report;
}

float mean_over(const std::vector<float> &values, size_t first, size_t count)
{
  if (count == 0 || first + count > values.size())
    fatal_error("[lightmap-gpu] a mean over coefficients [{}, {}) of {}.", first, first + count,
                values.size());
  double sum = 0.0;
  for (size_t k = first; k < first + count; ++k) sum += values[k];
  return (float)(sum / (double)count);
}

} // namespace

float record_comparison_report_t::reference_mean_over(size_t first, size_t count) const
{
  return mean_over(reference_mean, first, count);
}

float record_comparison_report_t::mean_absolute_difference_over(size_t first, size_t count) const
{
  return mean_over(mean_absolute_difference, first, count);
}

float record_comparison_report_t::largest_absolute_difference_over(size_t first,
                                                                   size_t count) const
{
  if (count == 0 || first + count > largest_absolute_difference.size())
    fatal_error("[lightmap-gpu] a largest difference over coefficients [{}, {}) of {}.", first,
                first + count, largest_absolute_difference.size());
  float largest = 0.f;
  for (size_t k = first; k < first + count; ++k)
    largest = std::max(largest, largest_absolute_difference[k]);
  return largest;
}

record_comparison_report_t compare_indirect_results(Span<const gpu_sample_t> samples,
                                                    Span<const size_t> charts,
                                                    Span<const indirect_sh_l1_t> reference,
                                                    Span<const indirect_sh_l1_t> candidate)
{
  if (reference.size() != samples.size() || candidate.size() != samples.size())
    fatal_error("[lightmap-gpu] comparing {} reference and {} candidate answers over {} "
                "records.",
                reference.size(), candidate.size(), samples.size());

  const uint32_t scale_group[SH_L1_COEFFICIENT_COUNT] = {0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  return compare_records(
      samples, charts, SH_L1_COEFFICIENT_COUNT, Span<const uint32_t>(scale_group),
      [&](uint32_t i, size_t k) { return coefficient_of(reference[i], k); },
      [&](uint32_t i, size_t k) { return coefficient_of(candidate[i], k); });
}

record_comparison_report_t compare_direct_results(Span<const gpu_sample_t> samples,
                                                  Span<const size_t> charts,
                                                  const gpu_direct_results_t &reference,
                                                  const gpu_direct_results_t &candidate)
{
  if (reference.light_count != candidate.light_count)
    fatal_error("[lightmap-gpu] comparing a direct answer over {} lights against one over {}.",
                reference.light_count, candidate.light_count);
  const size_t light_count = reference.light_count;
  const auto check_shape = [&](const gpu_direct_results_t &results, const char *which) {
    if (results.irradiance.size() != samples.size() ||
        results.coverage.size() != samples.size() * light_count ||
        results.weight.size() != samples.size() * light_count)
      fatal_error("[lightmap-gpu] the {} direct answer holds {} irradiances, {} coverages and "
                  "{} weights over {} records and {} lights.",
                  which, results.irradiance.size(), results.coverage.size(),
                  results.weight.size(), samples.size(), light_count);
  };
  check_shape(reference, "reference");
  check_shape(candidate, "candidate");

  const size_t coefficient_count = 3 + 2 * light_count;
  std::vector<uint32_t> scale_group(coefficient_count, 0);
  for (size_t k = 3; k < coefficient_count; ++k) scale_group[k] = k - 3 < light_count ? 1 : 2;

  return compare_records(
      samples, charts, coefficient_count, Span<const uint32_t>(scale_group),
      [&](uint32_t i, size_t k) { return coefficient_of(reference, i, k); },
      [&](uint32_t i, size_t k) { return coefficient_of(candidate, i, k); });
}

std::string_view direct_coefficient_name(size_t coefficient, size_t light_count,
                                         Span<char> storage)
{
  if (coefficient >= 3 + 2 * light_count)
    fatal_error("[lightmap-gpu] naming direct coefficient {} of {} lights.", coefficient,
                light_count);
  if (storage.size() < DIRECT_COEFFICIENT_NAME_CAPACITY)
    fatal_error("[lightmap-gpu] {} bytes for a coefficient name; {} are needed.", storage.size(),
                DIRECT_COEFFICIENT_NAME_CAPACITY);

  int written = 0;
  if (coefficient < 3)
    written = std::snprintf(storage.data, storage.size(), "irradiance.%c", "rgb"[coefficient]);
  else if (coefficient - 3 < light_count)
    written = std::snprintf(storage.data, storage.size(), "coverage[%zu]", coefficient - 3);
  else
    written = std::snprintf(storage.data, storage.size(), "weight[%zu]",
                            coefficient - 3 - light_count);
  return std::string_view(storage.data, (size_t)std::max(written, 0));
}

lightmap_pages_t reduce_record_values_to_pages(const lightmap_t &lightmap,
                                               const lightmap_sample_set_t &set,
                                               Span<const linalg::vec3> values)
{
  if (values.size() != set.samples.size() || set.origins.size() != set.samples.size())
    fatal_error("[lightmap-gpu] reducing {} answers over {} records with {} origins.",
                values.size(), set.samples.size(), set.origins.size());

  lightmap_pages_t pages;
  pages.allocate(lightmap.atlas, lightmap_pixel_format_t::Rgb9e5);
  const int gutter = lightmap.settings.gutter_in_texels;
  const int size = pages.size_in_texels;

  struct texel_sum_t
  {
    linalg::vec3 sum{0.f, 0.f, 0.f};
    uint32_t count = 0;
  };
  std::vector<texel_sum_t> texels(pages.texel_count());

  const auto texel_index_of = [&](int page, int x, int y) {
    return ((size_t)page * (size_t)size + (size_t)y) * (size_t)size + (size_t)x;
  };

  for (uint32_t i = 0; i < set.samples.size(); ++i)
  {
    const gpu_sample_t &sample = set.samples[i];
    const sample_origin_t &origin = set.origins[i];
    if (sample.chart_index >= set.charts.size())
      fatal_error("[lightmap-gpu] record {} names chart {} of a set holding {}.", i,
                  sample.chart_index, set.charts.size());
    const lightmap_chart_t &chart = lightmap.charts[set.charts[sample.chart_index]];
    const int x = chart.atlas_rect.min_x + gutter + origin.texel_x;
    const int y = chart.atlas_rect.min_y + gutter + origin.texel_y;
    if (chart.page < 0 || chart.page >= pages.page_count || x < 0 || y < 0 || x >= size ||
        y >= size)
      fatal_error("[lightmap-gpu] record {} lands on page {} texel ({}, {}) of {} pages of {}.",
                  i, chart.page, x, y, pages.page_count, size);

    texel_sum_t &texel = texels[texel_index_of(chart.page, x, y)];
    texel.sum = texel.sum + values[i];
    ++texel.count;
  }

  for (int page = 0; page < pages.page_count; ++page)
    for (int y = 0; y < size; ++y)
      for (int x = 0; x < size; ++x)
      {
        const texel_sum_t &texel = texels[texel_index_of(page, x, y)];
        if (texel.count == 0) continue;
        pages.store(page, x, y, texel.sum * (1.f / (float)texel.count));
      }

  return pages;
}

lightmap_pages_t absolute_difference_pages(const lightmap_pages_t &a, const lightmap_pages_t &b)
{
  if (a.size_in_texels != b.size_in_texels || a.page_count != b.page_count ||
      a.format != b.format)
    fatal_error("[lightmap-gpu] differencing {} page(s) of {} texels against {} of {}.",
                a.page_count, a.size_in_texels, b.page_count, b.size_in_texels);

  lightmap_pages_t difference = a;
  for (int page = 0; page < a.page_count; ++page)
    for (int y = 0; y < a.size_in_texels; ++y)
      for (int x = 0; x < a.size_in_texels; ++x)
      {
        const linalg::vec3 from_a = a.load(page, x, y);
        const linalg::vec3 from_b = b.load(page, x, y);
        difference.store(page, x, y,
                         {std::abs(from_a.x - from_b.x), std::abs(from_a.y - from_b.y),
                          std::abs(from_a.z - from_b.z)});
      }
  return difference;
}

} // namespace shared
