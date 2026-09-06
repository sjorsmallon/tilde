#include "lightmap_reflections.hpp"

#include "entities/entity_reflection.hpp"
#include "entities/generated/entities_generated.hpp"
#include "log.hpp"
#include "map_geometry.hpp"
#include "shapes.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <format>
#include <thread>

namespace shared
{

namespace
{

[[nodiscard]] bool point_is_inside_bounds(const aabb_bounds_t &bounds, const linalg::vec3 &point)
{
  return point.x >= bounds.min.x && point.x <= bounds.max.x && point.y >= bounds.min.y &&
         point.y <= bounds.max.y && point.z >= bounds.min.z && point.z <= bounds.max.z;
}

constexpr float PI = 3.14159265359f;

[[nodiscard]] float radical_inverse(uint32_t bits)
{
  bits = (bits << 16u) | (bits >> 16u);
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
  return (float)bits * 2.3283064365386963e-10f;
}

void orthonormal_basis(const linalg::vec3 &normal, linalg::vec3 &out_u, linalg::vec3 &out_v)
{
  const linalg::vec3 reference =
      std::abs(normal.x) < 0.9f ? linalg::vec3{1.f, 0.f, 0.f} : linalg::vec3{0.f, 1.f, 0.f};
  out_u = linalg::normalize(linalg::cross(normal, reference));
  out_v = linalg::cross(normal, out_u);
}

// The cube box-averaged level by level, in floats: what the prefilter fetches
// from, so a wide lobe at a coarse mip reads a mean rather than one texel.
struct averaged_cube_t
{
  int size_in_texels = 0;
  int mip_count = 0;
  std::vector<std::vector<linalg::vec3>> mips;

  [[nodiscard]] int size_of_mip(int mip) const { return std::max(size_in_texels >> mip, 1); }
  [[nodiscard]] const linalg::vec3 &at(int mip, int face, int x, int y) const
  {
    const size_t size = (size_t)size_of_mip(mip);
    return mips[(size_t)mip][((size_t)face * size + (size_t)y) * size + (size_t)x];
  }
  [[nodiscard]] linalg::vec3 fetch(float mip, const linalg::vec3 &direction) const
  {
    const int level = std::clamp((int)std::lround(mip), 0, mip_count - 1);
    const reflection_cube_texel_t texel = reflection_cube_texel_of(direction, size_of_mip(level));
    return at(level, texel.face, texel.x, texel.y);
  }
};

[[nodiscard]] averaged_cube_t average_cube_levels(const reflection_cube_t &cube)
{
  averaged_cube_t averaged;
  averaged.size_in_texels = cube.size_in_texels;
  averaged.mip_count = cube.mip_count;
  averaged.mips.resize((size_t)cube.mip_count);

  averaged.mips[0].resize(cube.texels_in_mip(0));
  for (size_t texel = 0; texel < averaged.mips[0].size(); ++texel)
    averaged.mips[0][texel] = cube.load(texel);

  for (int mip = 1; mip < cube.mip_count; ++mip)
  {
    const int size = averaged.size_of_mip(mip);
    const int above = averaged.size_of_mip(mip - 1);
    averaged.mips[(size_t)mip].resize(cube.texels_in_mip(mip));
    for (int face = 0; face < REFLECTION_CUBE_FACE_COUNT; ++face)
    for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x)
    {
      const int x0 = std::min(x * 2, above - 1);
      const int y0 = std::min(y * 2, above - 1);
      const int x1 = std::min(x * 2 + 1, above - 1);
      const int y1 = std::min(y * 2 + 1, above - 1);
      const linalg::vec3 sum = averaged.at(mip - 1, face, x0, y0) +
                               averaged.at(mip - 1, face, x1, y0) +
                               averaged.at(mip - 1, face, x0, y1) +
                               averaged.at(mip - 1, face, x1, y1);
      averaged.mips[(size_t)mip][((size_t)face * (size_t)size + (size_t)y) * (size_t)size +
                                 (size_t)x] = sum * 0.25f;
    }
  }
  return averaged;
}

} // namespace

reflection_cube_texel_t reflection_cube_texel_of(const linalg::vec3 &direction,
                                                 int size_in_texels)
{
  const float ax = std::abs(direction.x);
  const float ay = std::abs(direction.y);
  const float az = std::abs(direction.z);

  reflection_cube_texel_t texel;
  float major = 1.f;
  float u = 0.f;
  float v = 0.f;
  if (ax >= ay && ax >= az)
  {
    major = ax;
    texel.face = direction.x > 0.f ? 0 : 1;
    u = direction.x > 0.f ? -direction.z : direction.z;
    v = -direction.y;
  }
  else if (ay >= az)
  {
    major = ay;
    texel.face = direction.y > 0.f ? 2 : 3;
    u = direction.x;
    v = direction.y > 0.f ? direction.z : -direction.z;
  }
  else
  {
    major = az;
    texel.face = direction.z > 0.f ? 4 : 5;
    u = direction.z > 0.f ? direction.x : -direction.x;
    v = -direction.y;
  }
  if (!(major > 0.f)) return texel;

  const float size = (float)size_in_texels;
  texel.x = std::clamp((int)std::floor((u / major * 0.5f + 0.5f) * size), 0, size_in_texels - 1);
  texel.y = std::clamp((int)std::floor((v / major * 0.5f + 0.5f) * size), 0, size_in_texels - 1);
  return texel;
}

void prefilter_reflection_cube(reflection_cube_t &cube)
{
  if (cube.empty() || cube.mip_count < 2) return;

  const averaged_cube_t averaged = average_cube_levels(cube);
  const float texel_solid_angle =
      4.f * PI / ((float)REFLECTION_CUBE_FACE_COUNT * (float)cube.size_in_texels *
                  (float)cube.size_in_texels);

  for (int mip = 1; mip < cube.mip_count; ++mip)
  {
    const float roughness = (float)mip / (float)(cube.mip_count - 1);
    const float alpha = std::max(roughness, 0.001f);
    const float alpha_squared = alpha * alpha;
    const int size = cube.size_of_mip(mip);

    for (int face = 0; face < REFLECTION_CUBE_FACE_COUNT; ++face)
    for (int y = 0; y < size; ++y)
    for (int x = 0; x < size; ++x)
    {
      const linalg::vec3 normal = reflection_cube_direction(face, x, y, size);
      linalg::vec3 tangent_u;
      linalg::vec3 tangent_v;
      orthonormal_basis(normal, tangent_u, tangent_v);

      linalg::vec3 sum{0.f, 0.f, 0.f};
      float weight = 0.f;
      for (int i = 0; i < REFLECTION_PREFILTER_SAMPLES; ++i)
      {
        const float xi_x = ((float)i + 0.5f) / (float)REFLECTION_PREFILTER_SAMPLES;
        const float xi_y = radical_inverse((uint32_t)i);
        const float phi = 2.f * PI * xi_x;
        const float cos_theta =
            std::sqrt((1.f - xi_y) / (1.f + (alpha_squared - 1.f) * xi_y));
        const float sin_theta = std::sqrt(std::max(0.f, 1.f - cos_theta * cos_theta));
        const linalg::vec3 half = tangent_u * (sin_theta * std::cos(phi)) +
                                  tangent_v * (sin_theta * std::sin(phi)) + normal * cos_theta;

        const float n_dot_h = cos_theta;
        const linalg::vec3 light = half * (2.f * n_dot_h) - normal;
        const float n_dot_l = linalg::dot(normal, light);
        if (n_dot_l <= 0.f) continue;

        const float denominator = n_dot_h * n_dot_h * (alpha_squared - 1.f) + 1.f;
        const float distribution = alpha_squared / (PI * denominator * denominator);
        const float pdf = distribution * 0.25f + 1e-4f;
        const float sample_solid_angle =
            1.f / ((float)REFLECTION_PREFILTER_SAMPLES * pdf + 1e-4f);
        const float source_mip = std::clamp(
            0.5f * std::log2(sample_solid_angle / texel_solid_angle), 0.f,
            (float)(cube.mip_count - 1));

        sum = sum + averaged.fetch(source_mip, light) * n_dot_l;
        weight += n_dot_l;
      }
      cube.store(mip, face, x, y, weight > 0.f ? sum * (1.f / weight) : linalg::vec3{0.f, 0.f, 0.f});
    }
  }
}

aabb_bounds_t measure_reflection_box(const Bounding_Volume_Hierarchy &occluders,
                                     const linalg::vec3 &position, float open_face_extent,
                                     uint8_t &out_open_faces)
{
  aabb_bounds_t box{position, position};
  out_open_faces = 0;

  for (uint32_t face = 0; face < REFLECTION_BOX_FACE_COUNT; ++face)
  {
    const int axis = (int)(face / 2);
    const float sign = (face % 2 == 0) ? 1.f : -1.f;
    linalg::vec3 direction{0.f, 0.f, 0.f};
    direction[axis] = sign;

    ray_hit_result_t hit{};
    float reach = open_face_extent;
    if (bvh_intersect_ray(occluders, position, direction, hit) && hit.t >= 0.f &&
        hit.t < open_face_extent)
      reach = hit.t;
    else
      out_open_faces |= (uint8_t)(1u << face);

    if (sign < 0.f)
      box.min[axis] = position[axis] - reach;
    else
      box.max[axis] = position[axis] + reach;
  }
  return box;
}

reflection_capture_set_t build_reflection_captures(const map_t &map, const probe_grid_t &grid,
                                                   Span<const uint8_t> inside,
                                                   const Bounding_Volume_Hierarchy &occluders,
                                                   const reflection_capture_settings_t &settings)
{
  reflection_capture_set_t set;
  if (grid.probe_count() == 0 || map.geometry.empty()) return set;
  if (inside.size() != grid.probe_count())
    fatal_error("[lightmap] building reflection captures over a grid of {} with {} flag(s)",
                grid.probe_count(), inside.size());
  if (!(settings.spacing_in_world_units > 0.f))
  {
    log_error("[lightmap] reflection spacing must be positive, got {}.",
              settings.spacing_in_world_units);
    return set;
  }

  const int stride =
      std::max(1, (int)std::lround(settings.spacing_in_world_units / grid.spacing));
  set.spacing = (float)stride * grid.spacing;

  std::vector<aabb_bounds_t> overrides;
  for (const map_entity_t &entry : map.entities)
  {
    if (!entry.entity) continue;
    const entities::Reflection_Volume_Entity *volume =
        entities::entity_as<entities::Reflection_Volume_Entity>(entry.entity.get());
    if (!volume) continue;
    overrides.push_back(get_bounds(volume->volume, volume->position));
  }

  aabb_bounds_t geometry_bounds = get_bounds(map.geometry.front().value);
  for (const map_geometry_t &entry : map.geometry)
    geometry_bounds = union_aabb(geometry_bounds, get_bounds(entry.value));

  uint32_t open_face_count = 0;
  uint32_t buried_count = 0;
  uint32_t overridden_count = 0;
  for (int z = 0; z < grid.count.z; z += stride)
  for (int y = 0; y < grid.count.y; y += stride)
  for (int x = 0; x < grid.count.x; x += stride)
  {
    const size_t probe_index = grid.index_of(x, y, z);
    if (!point_is_inside_bounds(geometry_bounds, grid.position_of({x, y, z}))) continue;
    if (inside[probe_index])
    {
      ++buried_count;
      continue;
    }

    reflection_capture_t capture;
    capture.position = grid.position_of({x, y, z});
    capture.probe_index = (uint32_t)probe_index;
    capture.box = measure_reflection_box(occluders, capture.position,
                                         settings.open_face_extent, capture.open_faces);

    for (const aabb_bounds_t &override_box : overrides)
    {
      if (!point_is_inside_bounds(override_box, capture.position)) continue;
      capture.box = override_box;
      capture.open_faces = 0;
      capture.box_overridden = true;
    }

    if (capture.open_faces != 0) ++open_face_count;
    if (capture.box_overridden) ++overridden_count;
    set.captures.push_back(capture);
  }

  log_terminal("[lightmap] {} reflection capture(s) at {} unit spacing ({} buried, {} with an "
           "open face, {} overridden by {} volume(s)).",
           set.captures.size(), set.spacing, buried_count, open_face_count,
           overridden_count, overrides.size());
  return set;
}

reflection_lattice_t derive_reflection_lattice(const reflection_capture_set_t &set)
{
  reflection_lattice_t lattice;
  if (set.captures.empty()) return lattice;
  if (!(set.spacing > 0.f))
  {
    log_error("[lightmap] {} reflection capture(s) at a spacing of {}: no lattice.",
              set.captures.size(), set.spacing);
    return lattice;
  }

  linalg::vec3 low = set.captures.front().position;
  linalg::vec3 high = low;
  for (const reflection_capture_t &capture : set.captures)
    for (int axis = 0; axis < 3; ++axis)
    {
      low[axis] = std::min(low[axis], capture.position[axis]);
      high[axis] = std::max(high[axis], capture.position[axis]);
    }
  lattice.origin = low;
  lattice.spacing = set.spacing;
  for (int axis = 0; axis < 3; ++axis)
    lattice.count[axis] = (int)std::lround((high[axis] - low[axis]) / set.spacing) + 1;
  lattice.cells.assign((size_t)lattice.count.x * (size_t)lattice.count.y * (size_t)lattice.count.z,
                       -1);

  for (uint32_t index = 0; index < set.captures.size(); ++index)
  {
    const linalg::vec3 &position = set.captures[index].position;
    linalg::vec3i cell;
    for (int axis = 0; axis < 3; ++axis)
    {
      const float offset = (position[axis] - low[axis]) / set.spacing;
      cell[axis] = (int)std::lround(offset);
      if (std::abs(offset - (float)cell[axis]) > 1e-3f)
      {
        log_error("[lightmap] reflection capture {} at ({}, {}, {}) is off the {}-unit lattice "
                  "from ({}, {}, {}); no lattice.",
                  index, position.x, position.y, position.z, set.spacing, low.x, low.y, low.z);
        return {};
      }
    }
    int32_t &slot = lattice.cells[lattice.index_of(cell)];
    if (slot >= 0)
    {
      log_error("[lightmap] reflection captures {} and {} share lattice cell ({}, {}, {}); "
                "no lattice.",
                slot, index, cell.x, cell.y, cell.z);
      return {};
    }
    slot = (int32_t)index;
  }
  return lattice;
}

reflection_capture_pick_t find_captures_for(const reflection_capture_set_t &set,
                                            const reflection_lattice_t &lattice,
                                            const linalg::vec3 &point)
{
  reflection_capture_pick_t pick;
  if (set.captures.empty() || lattice.empty()) return pick;

  const linalg::vec3 local = (point - lattice.origin) * (1.f / lattice.spacing);
  linalg::vec3i base;
  linalg::vec3 t;
  for (int axis = 0; axis < 3; ++axis)
  {
    base[axis] = (int)std::floor(local[axis]);
    t[axis] = local[axis] - (float)base[axis];
  }

  for (int corner = 0; corner < 8; ++corner)
  {
    float weight = 1.f;
    linalg::vec3i cell;
    for (int axis = 0; axis < 3; ++axis)
    {
      const int high = (corner >> axis) & 1;
      weight *= high ? t[axis] : 1.f - t[axis];
      cell[axis] = std::clamp(base[axis] + high, 0, lattice.count[axis] - 1);
    }
    if (weight <= 0.f) continue;
    const int32_t index = lattice.cells[lattice.index_of(cell)];
    if (index < 0) continue;

    uint32_t slot = 0;
    while (slot < pick.count && pick.indices[slot] != (uint32_t)index) ++slot;
    if (slot == pick.count)
    {
      pick.indices[slot] = (uint32_t)index;
      pick.weights[slot] = 0.f;
      ++pick.count;
    }
    pick.weights[slot] += weight;
  }

  float total = 0.f;
  for (uint32_t slot = 0; slot < pick.count; ++slot) total += pick.weights[slot];
  if (!(total > 0.f))
  {
    pick.count = 0;
    return pick;
  }
  for (uint32_t slot = 0; slot < pick.count; ++slot) pick.weights[slot] /= total;

  for (uint32_t slot = 1; slot < pick.count; ++slot)
  {
    uint32_t at = slot;
    while (at > 0 && pick.weights[at - 1] < pick.weights[at])
    {
      std::swap(pick.weights[at - 1], pick.weights[at]);
      std::swap(pick.indices[at - 1], pick.indices[at]);
      --at;
    }
  }
  return pick;
}

reflection_volume_coverage_t reflection_volume_coverage_of(const reflection_capture_set_t &set,
                                                           const aabb_bounds_t &bounds)
{
  reflection_volume_coverage_t coverage;
  for (const reflection_capture_t &capture : set.captures)
  {
    if (!point_is_inside_bounds(bounds, capture.position)) continue;
    ++coverage.covered;
    const auto same = [](const linalg::vec3 &a, const linalg::vec3 &b) {
      return a.x == b.x && a.y == b.y && a.z == b.z;
    };
    if (capture.box_overridden && same(capture.box.min, bounds.min) &&
        same(capture.box.max, bounds.max))
      ++coverage.overridden_as_placed;
  }
  return coverage;
}

std::vector<gpu_sample_t> collect_capture_samples(const reflection_capture_set_t &set,
                                                  int size_in_texels)
{
  if (size_in_texels <= 0)
    fatal_error("[lightmap] collecting capture records at a face size of {}", size_in_texels);

  const size_t per_face = (size_t)size_in_texels * (size_t)size_in_texels;
  std::vector<gpu_sample_t> samples;
  samples.reserve(set.captures.size() * (size_t)REFLECTION_CUBE_FACE_COUNT * per_face);

  for (size_t capture = 0; capture < set.captures.size(); ++capture)
  for (int face = 0; face < REFLECTION_CUBE_FACE_COUNT; ++face)
  for (int y = 0; y < size_in_texels; ++y)
  for (int x = 0; x < size_in_texels; ++x)
  {
    gpu_sample_t sample;
    sample.position = set.captures[capture].position;
    sample.normal = reflection_cube_direction(face, x, y, size_in_texels);
    sample.chart_index =
        (uint32_t)((capture * (size_t)REFLECTION_CUBE_FACE_COUNT + (size_t)face) * per_face +
                   (size_t)y * (size_t)size_in_texels + (size_t)x);
    sample.seed = sample_hash((int)capture, face, y * size_in_texels + x, 0x52464c54);
    samples.push_back(sample);
  }
  return samples;
}

void bake_reflection_captures(reflection_capture_set_t &set, const traced_scene_t &scene,
                              Span<const baked_light_t> lights,
                              const indirect_trace_settings_t &settings, int size_in_texels,
                              lightmap_batch_solver_t *solver)
{
  const auto started = std::chrono::steady_clock::now();
  if (size_in_texels <= 0)
  {
    log_error("[lightmap] a reflection capture needs a positive face size, got {}.",
              size_in_texels);
    return;
  }

  for (reflection_capture_t &capture : set.captures) capture.cube.allocate(size_in_texels);
  if (set.captures.empty()) return;

  const std::vector<gpu_sample_t> samples = collect_capture_samples(set, size_in_texels);
  const size_t texels_per_capture = set.captures.front().cube.texels_in_mip(0);
  const auto store = [&](size_t record, const linalg::vec3 &value) {
    const uint32_t chart_index = samples[record].chart_index;
    set.captures[chart_index / texels_per_capture].cube.store(chart_index % texels_per_capture,
                                                              value);
  };

  unsigned int worker_count = std::thread::hardware_concurrency();
  if (worker_count == 0) worker_count = 1;
  worker_count = (unsigned int)std::min<size_t>(worker_count, std::max<size_t>(samples.size(), 1));

  if (solver)
  {
    worker_count = 1;
    gpu_capture_results_t results;
    solver->solve_captures(samples, results);
    if (results.values.size() != samples.size())
      fatal_error("[lightmap] {} answered {} capture record(s) of {}.", solver->name(),
                  results.values.size(), samples.size());
    for (size_t i = 0; i < samples.size(); ++i) store(i, results.values[i]);
  }
  else
  {
    std::atomic<size_t> next_record{0};
    const auto trace_until_done = [&]() {
      for (;;)
      {
        const size_t index = next_record.fetch_add(1, std::memory_order_relaxed);
        if (index >= samples.size()) return;
        const gpu_sample_t &sample = samples[(uint32_t)index];
        store(index, trace_capture_direction(scene, lights, sample.position, sample.normal,
                                             settings, sample.seed));
      }
    };

    std::vector<std::thread> workers;
    workers.reserve(worker_count - 1);
    for (unsigned int i = 1; i < worker_count; ++i) workers.emplace_back(trace_until_done);
    trace_until_done();
    for (std::thread &worker : workers) worker.join();
  }

  {
    std::atomic<size_t> next_capture{0};
    const auto prefilter_until_done = [&]() {
      for (;;)
      {
        const size_t index = next_capture.fetch_add(1, std::memory_order_relaxed);
        if (index >= set.captures.size()) return;
        prefilter_reflection_cube(set.captures[index].cube);
      }
    };
    unsigned int prefilter_workers = std::thread::hardware_concurrency();
    if (prefilter_workers == 0) prefilter_workers = 1;
    prefilter_workers = (unsigned int)std::min<size_t>(prefilter_workers, set.captures.size());
    std::vector<std::thread> workers;
    workers.reserve(prefilter_workers - 1);
    for (unsigned int i = 1; i < prefilter_workers; ++i) workers.emplace_back(prefilter_until_done);
    prefilter_until_done();
    for (std::thread &worker : workers) worker.join();
  }

  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  const std::string shaded_by =
      solver ? std::format("by {}", solver->name()) : std::format("on {} thread(s)", worker_count);
  log_terminal("[lightmap] baked {} reflection capture(s) at {}x{} a face, {} mip(s), in {:.2f}s "
               "{} -- {} texel(s), {} chain(s) each.",
               set.captures.size(), size_in_texels, size_in_texels,
               set.captures.front().cube.mip_count, seconds, shaded_by, samples.size(),
               settings.rays_per_sample);
}

} // namespace shared
