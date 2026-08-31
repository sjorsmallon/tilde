#include "lightmap_solve.hpp"

#include "collision_detection.hpp"
#include "entities/generated/entities_generated.hpp"
#include "lighting.hpp"
#include "log.hpp"
#include "map_geometry.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>

namespace shared
{

namespace
{

enum class light_kind_t
{
  Point,
  Spot,
  Directional,
};

// One row per light, flattened out of the three entity types at collect time --
// so the texel loop, which runs millions of times, switches on a small enum
// rather than asking the entity system what it is holding.
struct baked_light_t
{
  light_kind_t kind = light_kind_t::Point;
  linalg::vec3 position{0.f, 0.f, 0.f};
  // The way the light POINTS, which is basis_from(orientation).forward -- the
  // same convention the editor cone and ray gizmos draw.
  linalg::vec3 forward{0.f, -1.f, 0.f};
  linalg::vec3 radiance{1.f, 1.f, 1.f};
  float range = 0.f;
  float cos_inner = 1.f;
  float cos_outer = 0.f;
};

std::vector<baked_light_t> collect_lights(const map_t &map)
{
  std::vector<baked_light_t> lights;

  for (const map_entity_t &entry : map.entities)
  {
    if (!entry.entity) continue;

    if (const entities::Point_Light_Entity *point =
            entities::entity_as<entities::Point_Light_Entity>(entry.entity.get()))
    {
      baked_light_t light;
      light.kind = light_kind_t::Point;
      light.position = point->position;
      light.radiance = radiance_of(point->light);
      light.range = point->range;
      lights.push_back(light);
      continue;
    }

    if (const entities::Spot_Light_Entity *spot =
            entities::entity_as<entities::Spot_Light_Entity>(entry.entity.get()))
    {
      baked_light_t light;
      light.kind = light_kind_t::Spot;
      light.position = spot->position;
      light.forward = linalg::basis_from(spot->orientation).forward;
      light.radiance = radiance_of(spot->light);
      light.range = spot->range;
      light.cos_inner = std::cos(linalg::to_radians(spot->inner_degrees));
      light.cos_outer = std::cos(linalg::to_radians(spot->outer_degrees));
      lights.push_back(light);
      continue;
    }

    if (const entities::Directional_Light_Entity *directional =
            entities::entity_as<entities::Directional_Light_Entity>(entry.entity.get()))
    {
      baked_light_t light;
      light.kind = light_kind_t::Directional;
      light.forward = linalg::basis_from(directional->orientation).forward;
      light.radiance = radiance_of(directional->light);
      lights.push_back(light);
      continue;
    }
  }

  return lights;
}

// pbr.frag's distance_attenuation, unchanged: Frostbite's windowed inverse
// square (Lagarde 2014). Copied rather than approximated, because a baked light
// and the same light at runtime disagreeing is the one artifact nobody can debug
// from a screenshot.
float distance_attenuation(float squared_distance, float range)
{
  const float inverse_squared_radius = 1.f / std::max(range * range, 0.0001f);
  const float factor = squared_distance * inverse_squared_radius;
  const float smooth_factor = std::clamp(1.f - factor * factor, 0.f, 1.f);
  return (smooth_factor * smooth_factor) / std::max(squared_distance, 0.0001f);
}

Bounding_Volume_Hierarchy build_occluder_bvh(const map_t &map)
{
  std::vector<BVH_Input> inputs;
  inputs.reserve(map.geometry.size());

  for (const map_geometry_t &entry : map.geometry)
  {
    const std::vector<collision_piece_t> pieces =
        get_collision_pieces(entry.value, entry.uid);
    if (pieces.empty())
      log_warning("[lightmap] object {} has no collision pieces and casts no shadow.",
                  entry.uid);

    for (const collision_piece_t &piece : pieces)
    {
      BVH_Input input;
      input.aabb = piece.bounds;
      input.id = {Collision_Id::Type::Static_Geometry, entry.uid};
      input.collision_planes = piece.planes;
      inputs.push_back(input);
    }
  }

  return build_bvh(inputs);
}

// What one light does at one texel, before the mode has its say: the direction
// to it, how far, and how much of it arrives. `reaches` false is the union of
// every GATE -- out of range, behind the surface, outside the cone -- and those
// gates ARE the visibility question, which is why both modes share them and only
// the contribution differs.
struct light_arrival_t
{
  bool reaches = false;
  linalg::vec3 direction{0.f, 0.f, 0.f};
  float distance = 0.f;
  float attenuation = 0.f;
  float normal_dot_light = 0.f;
};

light_arrival_t arrival_at(const baked_light_t &light,
                           const linalg::vec3 &surface_position,
                           const linalg::vec3 &surface_normal,
                           float directional_shadow_distance)
{
  light_arrival_t arrival;

  if (light.kind == light_kind_t::Directional)
  {
    arrival.direction = linalg::normalize(light.forward * -1.f);
    arrival.distance = directional_shadow_distance;
    arrival.attenuation = 1.f;
  }
  else
  {
    const linalg::vec3 to_light = light.position - surface_position;
    const float squared_distance = linalg::dot(to_light, to_light);
    arrival.distance = std::sqrt(squared_distance);

    if (arrival.distance > light.range || arrival.distance < 1e-4f) return arrival;

    arrival.direction = to_light * (1.f / arrival.distance);
    arrival.attenuation = distance_attenuation(squared_distance, light.range);

    if (light.kind == light_kind_t::Spot)
    {
      const float cos_angle =
          linalg::dot(arrival.direction * -1.f, linalg::normalize(light.forward));
      const float spot_factor =
          std::clamp((cos_angle - light.cos_outer) /
                         std::max(light.cos_inner - light.cos_outer, 0.001f),
                     0.f, 1.f);
      if (spot_factor <= 0.f) return arrival;
      arrival.attenuation *= spot_factor;
    }
  }

  arrival.normal_dot_light = linalg::dot(surface_normal, arrival.direction);
  if (arrival.normal_dot_light <= 0.f) return arrival;
  if (arrival.attenuation <= 0.f) return arrival;

  arrival.reaches = true;
  return arrival;
}

bool is_unoccluded(const Bounding_Volume_Hierarchy &bvh,
                   const linalg::vec3 &surface_position,
                   const linalg::vec3 &surface_normal, const light_arrival_t &arrival,
                   float shadow_ray_bias)
{
  const linalg::vec3 origin = surface_position + surface_normal * shadow_ray_bias;

  ray_hit_result_t hit = {};
  if (!bvh_intersect_ray(bvh, origin, arrival.direction, hit)) return true;

  return !(hit.hit && hit.t > 0.f && hit.t < arrival.distance - shadow_ray_bias);
}


// The jitter is DERIVED, never drawn from shared/rng.hpp's global state: a rebake
// at unchanged settings has to reproduce the same pixels, and the chart loop runs
// on several threads, so a sequence anyone can advance is a bake that differs
// from itself. Keyed by atlas position, which is unique across the whole solve.
uint32_t sample_hash(int atlas_x, int atlas_y, int page, int sample_index)
{
  uint32_t hash = 2166136261u;
  const auto mix = [&](uint32_t value) {
    for (int byte = 0; byte < 4; ++byte)
    {
      hash ^= (value >> (byte * 8)) & 0xffu;
      hash *= 16777619u;
    }
  };
  mix((uint32_t)atlas_x);
  mix((uint32_t)atlas_y);
  mix((uint32_t)page);
  mix((uint32_t)sample_index);
  return hash;
}

// Where one axis of a stratum lands inside a texel, in [0, 1). N == 1 is the
// CENTRE with no jitter, so a one-sample solve is exactly the solve this had
// before supersampling.
float stratum_offset(int stratum, int count, uint32_t hash)
{
  if (count <= 1) return 0.5f;
  const float jitter = (float)(hash & 0xffffffu) * (1.f / 16777216.f);
  return ((float)stratum + jitter) / (float)count;
}

// A chart's rect while it is being solved: the values, and which of them a
// surface actually wrote. Held per WORKER and resized per chart, because a chart
// is solved into floats and quantized once -- dilation averaging RGB9E5 values it
// had just read back would quantize twice.
struct chart_scratch_t
{
  std::vector<linalg::vec3> values;
  std::vector<uint8_t> written;
  std::vector<uint8_t> written_at_round_start;

  void reset(int width, int height)
  {
    const size_t count = (size_t)width * (size_t)height;
    values.assign(count, linalg::vec3{0.f, 0.f, 0.f});
    written.assign(count, 0);
  }
};

// What one texel is worth: the NxN stratified samples of its footprint that land
// on the face, averaged. A sample outside the face is EXCLUDED rather than summed
// as zero -- outside the face there is no surface to be dark, and counting it
// would darken every chart edge by the fraction of the texel that hangs off it.
// No sample inside means the texel is the gutter pass's problem, not the solve's.
bool solve_texel(const lightmap_chart_t &chart,
                 const lightmap_bake_settings_t &settings,
                 const lightmap_solve_settings_t &solve_settings,
                 const std::vector<baked_light_t> &lights,
                 const Bounding_Volume_Hierarchy &bvh, int texel_x, int texel_y,
                 linalg::vec3 &out_value)
{
  const int samples_per_edge = std::max(solve_settings.samples_per_texel_edge, 1);
  const bool binary = solve_settings.mode == lightmap_solve_mode_t::Visibility;

  const int atlas_x = chart.atlas_rect.min_x + settings.gutter_in_texels + texel_x;
  const int atlas_y = chart.atlas_rect.min_y + settings.gutter_in_texels + texel_y;

  linalg::vec3 total{0.f, 0.f, 0.f};
  int inside_count = 0;

  for (int sample_y = 0; sample_y < samples_per_edge; ++sample_y)
    for (int sample_x = 0; sample_x < samples_per_edge; ++sample_x)
    {
      const int sample_index = sample_y * samples_per_edge + sample_x;
      const uint32_t hash = sample_hash(atlas_x, atlas_y, chart.page, sample_index);

      const linalg::vec2 chart_space = {
          ((float)texel_x + stratum_offset(sample_x, samples_per_edge, hash)) *
              chart.world_units_per_texel,
          ((float)texel_y + stratum_offset(sample_y, samples_per_edge, hash >> 8)) *
              chart.world_units_per_texel};

      if (!chart_space_is_inside_face(chart, chart_space)) continue;
      ++inside_count;

      const linalg::vec3 world_position = chart_space_to_world(chart, chart_space);

      linalg::vec3 irradiance{0.f, 0.f, 0.f};
      for (const baked_light_t &light : lights)
      {
        const light_arrival_t arrival =
            arrival_at(light, world_position, chart.plane.normal,
                       solve_settings.directional_shadow_distance);
        if (!arrival.reaches) continue;

        if (!is_unoccluded(bvh, world_position, chart.plane.normal, arrival,
                           solve_settings.shadow_ray_bias))
          continue;

        // The two modes, and this is the whole of the difference between them:
        // falloff forced to 1, colour forced to white, nothing left to sum.
        if (binary)
        {
          irradiance = {1.f, 1.f, 1.f};
          break;
        }

        irradiance = irradiance + light.radiance * (arrival.attenuation *
                                                    arrival.normal_dot_light);
      }

      total = total + irradiance;
    }

  if (inside_count == 0) return false;

  out_value = total * (1.f / (float)inside_count);
  return true;
}

// The gutter, filled from the chart's OWN covered texels and nothing else.
// lightmap.hpp promised this pass and never had one, so the gutter stayed at the
// zero it was allocated with and bilinear filtering pulled black in at every
// chart edge -- a dark seam on every face boundary in the level.
//
// Confined to the chart's rect, which is what makes it correct rather than merely
// bounded: the packer places rects flush, so a page-wide dilation would carry one
// face's light across the seam into its neighbour's gutter, which is the exact
// failure the gutter exists to prevent. Inside the rect there is nobody else to
// bleed from, so it runs to a fixed point rather than for a picked number of
// rounds -- a triangular face leaves most of its rect uncovered, and half a
// filled corner is the same seam one texel further out.
void fill_the_gutter(chart_scratch_t &scratch, int width, int height)
{
  for (;;)
  {
    scratch.written_at_round_start = scratch.written;

    bool filled_any = false;
    for (int y = 0; y < height; ++y)
      for (int x = 0; x < width; ++x)
      {
        const size_t index = (size_t)y * (size_t)width + (size_t)x;
        if (scratch.written_at_round_start[index]) continue;

        linalg::vec3 total{0.f, 0.f, 0.f};
        int neighbour_count = 0;
        for (int offset_y = -1; offset_y <= 1; ++offset_y)
          for (int offset_x = -1; offset_x <= 1; ++offset_x)
          {
            const int neighbour_x = x + offset_x;
            const int neighbour_y = y + offset_y;
            if (neighbour_x < 0 || neighbour_x >= width) continue;
            if (neighbour_y < 0 || neighbour_y >= height) continue;

            const size_t neighbour =
                (size_t)neighbour_y * (size_t)width + (size_t)neighbour_x;
            if (!scratch.written_at_round_start[neighbour]) continue;

            total = total + scratch.values[neighbour];
            ++neighbour_count;
          }

        if (neighbour_count == 0) continue;

        scratch.values[index] = total * (1.f / (float)neighbour_count);
        scratch.written[index] = 1;
        filled_any = true;
      }

    if (!filled_any) return;
  }
}

void solve_chart(const lightmap_chart_t &chart,
                 const lightmap_bake_settings_t &settings,
                 const lightmap_solve_settings_t &solve_settings,
                 const std::vector<baked_light_t> &lights,
                 const Bounding_Volume_Hierarchy &bvh, chart_scratch_t &scratch,
                 lightmap_pages_t &pages)
{
  const int width = chart.atlas_rect.width;
  const int height = chart.atlas_rect.height;
  if (width <= 0 || height <= 0) return;

  scratch.reset(width, height);

  const int covered_width = chart_covered_width(chart, settings);
  const int covered_height = chart_covered_height(chart, settings);

  for (int texel_y = 0; texel_y < covered_height; ++texel_y)
    for (int texel_x = 0; texel_x < covered_width; ++texel_x)
    {
      linalg::vec3 value{0.f, 0.f, 0.f};
      if (!solve_texel(chart, settings, solve_settings, lights, bvh, texel_x, texel_y,
                       value))
        continue;

      // Marked written even when it is BLACK, which is what makes the fill
      // correct: a texel in shadow is a surface that got no light, and dilating
      // into it would smear the lit side of a shadow edge back over it.
      const size_t index =
          (size_t)(texel_y + settings.gutter_in_texels) * (size_t)width +
          (size_t)(texel_x + settings.gutter_in_texels);
      scratch.values[index] = value;
      scratch.written[index] = 1;
    }

  if (solve_settings.dilate_into_the_gutter) fill_the_gutter(scratch, width, height);

  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
    {
      const size_t index = (size_t)y * (size_t)width + (size_t)x;
      if (!scratch.written[index]) continue;
      pages.store(chart.page, chart.atlas_rect.min_x + x, chart.atlas_rect.min_y + y,
                  scratch.values[index]);
    }
}

} // namespace

lightmap_pages_t bake_lightmap_pages(const map_t &map,
                                     const std::vector<lightmap_chart_t> &charts,
                                     const lightmap_atlas_t &atlas,
                                     const lightmap_bake_settings_t &settings,
                                     const lightmap_solve_settings_t &solve_settings)
{
  lightmap_pages_t pages;

  if (atlas.size_in_texels <= 0 || atlas.page_count <= 0)
  {
    log_error("[lightmap] cannot bake into an atlas of {} pages at {} texels.",
              atlas.page_count, atlas.size_in_texels);
    return pages;
  }

  const std::vector<baked_light_t> lights = collect_lights(map);
  if (lights.empty())
  {
    log_error("[lightmap] the map holds no light; every texel would bake black.");
    return pages;
  }

  pages.allocate(atlas, lightmap_pixel_format_t::Rgb9e5);

  const Bounding_Volume_Hierarchy bvh = build_occluder_bvh(map);

  // The chart loop is embarrassingly parallel: the packer places charts without
  // overlap, so each one writes a byte range of `pages` no other chart touches,
  // and everything else it reads is const. What it deliberately does NOT use is
  // Task_System -- that is a persistent pool with a fire-and-forget submit and no
  // way to join, and a bake needs exactly the thing it does not have.
  std::vector<size_t> packed_charts;
  packed_charts.reserve(charts.size());
  for (size_t i = 0; i < charts.size(); ++i)
    if (charts[i].page >= 0) packed_charts.push_back(i);

  if (packed_charts.empty())
  {
    log_error("[lightmap] none of the {} charts was packed; there is nothing to bake.",
              charts.size());
    return pages;
  }

  std::atomic<size_t> next_chart{0};
  const auto solve_until_done = [&]() {
    chart_scratch_t scratch;
    for (;;)
    {
      const size_t at = next_chart.fetch_add(1, std::memory_order_relaxed);
      if (at >= packed_charts.size()) return;
      solve_chart(charts[packed_charts[at]], settings, solve_settings, lights, bvh,
                  scratch, pages);
    }
  };

  unsigned int worker_count = std::thread::hardware_concurrency();
  if (worker_count == 0) worker_count = 1;
  worker_count = std::min((unsigned int)packed_charts.size(), worker_count);

  std::vector<std::thread> workers;
  workers.reserve(worker_count - 1);
  for (unsigned int i = 1; i < worker_count; ++i) workers.emplace_back(solve_until_done);

  // The calling thread is one of the workers, so a one-chart bake spawns nothing.
  solve_until_done();

  for (std::thread &worker : workers) worker.join();

  return pages;
}

} // namespace shared
