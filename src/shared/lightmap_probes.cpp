#include "lightmap_probes.hpp"

#include "log.hpp"
#include "map_geometry.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>

namespace shared
{

std::optional<probe_grid_t> try_build_probe_grid(const map_t &map, float spacing)
{
  if (!(spacing > 0.f))
  {
    log_error("[lightmap] probe spacing must be positive, got {}.", spacing);
    return std::nullopt;
  }
  if (map.geometry.empty()) return std::nullopt;

  aabb_bounds_t bounds = get_bounds(map.geometry.front().value);
  for (const map_geometry_t &entry : map.geometry)
    bounds = union_aabb(bounds, get_bounds(entry.value));

  const auto snap_down = [spacing](float value) {
    return (std::floor(value / spacing) - 1.f) * spacing;
  };
  const auto snap_up = [spacing](float value) {
    return (std::ceil(value / spacing) + 1.f) * spacing;
  };

  probe_grid_t grid;
  grid.spacing = spacing;
  grid.origin = {snap_down(bounds.min.x), snap_down(bounds.min.y), snap_down(bounds.min.z)};

  const linalg::vec3 far_corner{snap_up(bounds.max.x), snap_up(bounds.max.y),
                                snap_up(bounds.max.z)};
  const auto count_along = [&](float from, float to) {
    return (int)std::lround((to - from) / spacing) + 1;
  };
  grid.count = {count_along(grid.origin.x, far_corner.x),
                count_along(grid.origin.y, far_corner.y),
                count_along(grid.origin.z, far_corner.z)};

  const int longest = std::max(grid.count.x, std::max(grid.count.y, grid.count.z));
  if (longest > MAX_PROBE_GRID_EXTENT)
  {
    const float extent = std::max(far_corner.x - grid.origin.x,
                                  std::max(far_corner.y - grid.origin.y,
                                           far_corner.z - grid.origin.z));
    log_error("[lightmap] a probe grid of {}x{}x{} exceeds {} on an axis; a spacing of "
              "at least {} would fit.",
              grid.count.x, grid.count.y, grid.count.z, MAX_PROBE_GRID_EXTENT,
              std::ceil(extent / (float)(MAX_PROBE_GRID_EXTENT - 1)));
    return std::nullopt;
  }

  return grid;
}

std::vector<uint8_t> classify_probes_inside_solid(const probe_grid_t &grid,
                                                  const Bounding_Volume_Hierarchy &occluders)
{
  std::vector<uint8_t> inside(grid.probe_count(), 0);
  for (size_t index = 0; index < inside.size(); ++index)
  {
    const linalg::vec3 position = grid.position_of(grid.coordinates_of(index));
    inside[index] = bvh_point_is_inside_solid(occluders, position) ? 1 : 0;
  }
  return inside;
}

probe_visibility_slots_t assign_probe_visibility_channels(Span<const baked_light_t> lights)
{
  probe_visibility_slots_t slots = NO_PROBE_VISIBILITY_SLOTS;
  uint32_t claimed = 0;
  for (size_t slot = 0; slot < lights.size(); ++slot)
  {
    if (!light_is_analytic(lights[slot].light.mode)) continue;
    if (claimed < PROBE_VISIBILITY_CHANNELS)
    {
      slots[claimed++] = (int16_t)slot;
      continue;
    }
    log_warning("[lightmap] Mixed light {} is past the {} the probes hold a visibility for; "
                "it casts no static shadow onto dynamic objects. Make it Baked or Dynamic, or "
                "make an earlier Mixed light one.",
                lights[slot].uid, PROBE_VISIBILITY_CHANNELS);
  }
  return slots;
}

void dilate_probes_inside_solid(const probe_grid_t &grid, Span<const uint8_t> inside,
                                std::vector<probe_trace_t> &values)
{
  if (values.size() != grid.probe_count() || inside.size() != grid.probe_count())
    fatal_error("[lightmap] dilating {} probe(s) over a grid of {} with {} flag(s)",
                values.size(), grid.probe_count(), inside.size());

  std::vector<uint8_t> filled(inside.begin(), inside.end());
  for (uint8_t &flag : filled) flag = flag ? 0 : 1;

  std::vector<uint8_t> filled_after;
  std::vector<probe_trace_t> values_after;
  for (;;)
  {
    filled_after = filled;
    values_after = values;
    bool changed = false;

    for (size_t index = 0; index < values.size(); ++index)
    {
      if (filled[index]) continue;

      const linalg::vec3i at = grid.coordinates_of(index);
      indirect_sh_l1_t sum;
      Array<float, PROBE_VISIBILITY_CHANNELS> visibility_sum{};
      int neighbours = 0;
      for (int dz = -1; dz <= 1; ++dz)
      for (int dy = -1; dy <= 1; ++dy)
      for (int dx = -1; dx <= 1; ++dx)
      {
        const linalg::vec3i near{at.x + dx, at.y + dy, at.z + dz};
        if (near.x < 0 || near.y < 0 || near.z < 0 || near.x >= grid.count.x ||
            near.y >= grid.count.y || near.z >= grid.count.z)
          continue;
        const size_t neighbour = grid.index_of(near.x, near.y, near.z);
        if (!filled[neighbour]) continue;

        sum.l0 = sum.l0 + values[neighbour].light.l0;
        for (int axis = 0; axis < SH_L1_LAYERS_PER_PAGE; ++axis)
          sum.l1[axis] = sum.l1[axis] + values[neighbour].light.l1[axis];
        for (uint32_t channel = 0; channel < PROBE_VISIBILITY_CHANNELS; ++channel)
          visibility_sum[channel] += values[neighbour].visibility[channel];
        ++neighbours;
      }
      if (neighbours == 0) continue;

      const float scale = 1.f / (float)neighbours;
      values_after[index].light.l0 = sum.l0 * scale;
      for (int axis = 0; axis < SH_L1_LAYERS_PER_PAGE; ++axis)
        values_after[index].light.l1[axis] = sum.l1[axis] * scale;
      for (uint32_t channel = 0; channel < PROBE_VISIBILITY_CHANNELS; ++channel)
        values_after[index].visibility[channel] = visibility_sum[channel] * scale;
      filled_after[index] = 1;
      changed = true;
    }

    filled.swap(filled_after);
    values.swap(values_after);
    if (!changed) break;
  }
}

probe_volume_t bake_probe_volume(const probe_grid_t &grid,
                                 const Bounding_Volume_Hierarchy &occluders,
                                 const traced_scene_t &scene,
                                 Span<const baked_light_t> lights,
                                 const indirect_trace_settings_t &settings)
{
  const auto started = std::chrono::steady_clock::now();

  const std::vector<uint8_t> inside = classify_probes_inside_solid(grid, occluders);
  const probe_visibility_slots_t visibility_slots = assign_probe_visibility_channels(lights);
  std::vector<probe_trace_t> values(grid.probe_count());

  unsigned int worker_count = std::thread::hardware_concurrency();
  if (worker_count == 0) worker_count = 1;
  worker_count = (unsigned int)std::min<size_t>(worker_count, std::max<size_t>(values.size(), 1));

  std::atomic<size_t> next_probe{0};
  const auto trace_until_done = [&]() {
    for (;;)
    {
      const size_t index = next_probe.fetch_add(1, std::memory_order_relaxed);
      if (index >= values.size()) return;
      if (inside[index]) continue;

      const linalg::vec3i at = grid.coordinates_of(index);
      const uint32_t hash = sample_hash(at.x, at.y, at.z, 0x50524f42);
      values[index] = trace_probe_light(scene, lights, visibility_slots, grid.position_of(at),
                                        settings, hash);
    }
  };

  std::vector<std::thread> workers;
  workers.reserve(worker_count - 1);
  for (unsigned int i = 1; i < worker_count; ++i) workers.emplace_back(trace_until_done);
  trace_until_done();
  for (std::thread &worker : workers) worker.join();

  dilate_probes_inside_solid(grid, inside, values);

  probe_volume_t volume;
  volume.allocate(grid);
  volume.visibility_slots = visibility_slots;
  for (size_t index = 0; index < values.size(); ++index)
  {
    volume.store(index, values[index].light);
    volume.store_visibility(index, values[index].visibility);
  }

  size_t inside_count = 0;
  for (const uint8_t flag : inside) inside_count += flag;
  uint32_t visibility_channels = 0;
  for (const int16_t slot : visibility_slots) visibility_channels += slot != LIGHTMAP_NO_LIGHT_SLOT;
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  log_terminal("[lightmap] baked {}x{}x{} probes at spacing {} in {:.2f}s -- {} traced, {} "
               "inside solids and filled from their neighbours, {} chain(s) per probe, {} "
               "Mixed light visibility channel(s).",
               grid.count.x, grid.count.y, grid.count.z, grid.spacing, seconds,
               values.size() - inside_count, inside_count, settings.rays_per_sample,
               visibility_channels);

  return volume;
}

} // namespace shared
