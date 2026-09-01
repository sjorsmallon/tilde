#include "lightmap_solve.hpp"

#include "collision_detection.hpp"
#include "entities/generated/entities_generated.hpp"
#include "lighting.hpp"
#include "log.hpp"
#include "map_geometry.hpp"
#include "shader_math.hpp"
#include "span.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <thread>

namespace shared
{

namespace
{

// A light the bake solves, and WHICH entity it is. The uid is how a mask slot is
// named and what the runtime resolve table will match against.
struct baked_light_t
{
  entity_uid_t uid = 0;
  scene_light_t light;
};

std::vector<baked_light_t> collect_lights(const map_t &map)
{
  std::vector<baked_light_t> lights;

  for (const map_entity_t &entry : map.entities)
  {
    if (!entry.entity)
      continue;

    const std::optional<scene_light_t> light = try_light_of(*entry.entity);
    if (!light) continue;

    // Baked and Mixed both solve into the atlas; Dynamic never does. Without
    // this every light in the map is in the atlas AND in the runtime array, and
    // a level lit by both is lit twice (lighting_def.md ss2).
    if (!light_is_baked(light->mode)) continue;

    lights.push_back({entry.uid, *light});
  }

  return lights;
}

// The falloff and the cone are shader_math.hpp's, which is
// resources/shaders/light_falloff.glsl compiled as C++ -- the same text the
// shaders compile as GLSL. It used to be a copy of pbr.frag's, with a comment
// saying so; lighting_def.md decision I is why a copy was not good enough.
using shader_math::distance_attenuation;
using shader_math::spot_cone_factor;

// Rec. 709, which is what "how much light is this" means when the answer has to
// be one number. Only the RANKING reads it -- nothing stored is ever collapsed
// to a luminance.
float luminance_of(const linalg::vec3 &linear_rgb)
{
  return 0.2126f * linear_rgb.x + 0.7152f * linear_rgb.y + 0.0722f * linear_rgb.z;
}

// A light a chart could not keep. Collected per worker and logged after the join
// rather than from inside it: the log has no lock, and a line interleaved with
// three others is a line nobody reads.
struct dropped_light_t
{
  entity_uid_t object_uid = 0;
  entity_uid_t light_uid = 0;
};

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
// to it, how far, and how much of it arrives.
//
// TWO gates, and the split is the whole of lighting_def.md ss14 step 6. `arrives`
// is range, the cone and a positive attenuation -- what light_arrival.glsl
// recomputes at runtime anyway, so nothing downstream can be wrong about it.
// `reaches` adds N.L against the chart's FLAT plane normal, which the runtime
// does NOT reproduce: it shades with the normal-mapped normal. The irradiance sum
// needs `reaches`; a visibility mask must be gated on `arrives` alone.
struct light_arrival_t
{
  bool arrives = false;
  bool reaches = false;
  linalg::vec3 direction{0.f, 0.f, 0.f};
  float distance = 0.f;
  float attenuation = 0.f;
  float normal_dot_light = 0.f;
};

light_arrival_t arrival_at(const scene_light_t &light,
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
          spot_cone_factor(cos_angle, light.cos_inner, light.cos_outer);
      if (spot_factor <= 0.f) return arrival;
      arrival.attenuation *= spot_factor;
    }
  }

  if (arrival.attenuation <= 0.f) return arrival;
  arrival.arrives = true;

  arrival.normal_dot_light = linalg::dot(surface_normal, arrival.direction);
  if (arrival.normal_dot_light <= 0.f) return arrival;

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
//
// CHANNELS rather than a vec3, because the per-light coverage rides the same
// buffer: it has to be dilated into the gutter exactly as the irradiance is, and
// two dilation passes are two things free to disagree about a chart edge. 0..2
// are the irradiance, 3.. is one coverage per light.
struct chart_scratch_t
{
  int channel_count = 3;
  std::vector<float> values;
  std::vector<uint8_t> written;
  std::vector<uint8_t> written_at_round_start;
  std::vector<float> neighbour_total;

  // What each light DELIVERS to this chart, summed over its samples, and the
  // ranking that picks the LIGHTMAP_LIGHTS_PER_CHART strongest out of it.
  std::vector<float> light_weight;
  std::vector<uint32_t> ranked_lights;

  void reset(int width, int height, int channels)
  {
    channel_count = channels;
    const size_t count = (size_t)width * (size_t)height;
    values.assign(count * (size_t)channels, 0.f);
    written.assign(count, 0);
    neighbour_total.assign((size_t)channels, 0.f);
    light_weight.assign((size_t)channels - 3, 0.f);
    ranked_lights.clear();
  }

  [[nodiscard]] Span<float> at(size_t index)
  {
    return Span<float>(values.data() + index * (size_t)channel_count,
                       (uint32_t)channel_count);
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
                 linalg::vec3 &out_value, Span<float> out_coverage,
                 Span<float> out_light_weight)
{
  const int samples_per_edge = std::max(solve_settings.samples_per_texel_edge, 1);
  const bool binary = solve_settings.mode == lightmap_solve_mode_t::Visibility;

  const int atlas_x = chart.atlas_rect.min_x + settings.gutter_in_texels + texel_x;
  const int atlas_y = chart.atlas_rect.min_y + settings.gutter_in_texels + texel_y;

  if (out_coverage.size() != (uint32_t)lights.size() ||
      out_light_weight.size() != (uint32_t)lights.size())
    fatal_error("[lightmap] a coverage span of {} and a weight span of {} for {} lights.",
                out_coverage.size(), out_light_weight.size(), lights.size());
  for (float &coverage : out_coverage) coverage = 0.f;

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
      for (uint32_t slot = 0; slot < (uint32_t)lights.size(); ++slot)
      {
        const scene_light_t &light = lights[slot].light;
        const light_arrival_t arrival =
            arrival_at(light, world_position, chart.plane.normal,
                       solve_settings.directional_shadow_distance);
        if (!arrival.arrives) continue;

        // The ray is cast for every light that ARRIVES, including one the flat
        // face plane faces away from -- that is the one place a visibility costs
        // a ray the irradiance sum does not, and it is not optional: the runtime
        // shades with a normal-mapped normal, which at a grazing angle faces a
        // light the geometric normal does not.
        if (!is_unoccluded(bvh, world_position, chart.plane.normal, arrival,
                           solve_settings.shadow_ray_bias))
          continue;

        out_coverage[slot] += 1.f;

        // What the chart's four slots are ranked by, and it is deliberately the
        // light's DELIVERY rather than its coverage: a dim lamp lighting the
        // whole face has coverage 1 everywhere and is not what the face is lit
        // by. N.L is left out for the same reason it is left out of the mask.
        out_light_weight[slot] += arrival.attenuation * luminance_of(light.radiance);

        if (!arrival.reaches) continue;

        // The two modes, and this is the whole of the difference between them:
        // falloff forced to 1, colour forced to white, nothing left to sum.
        if (binary)
        {
          irradiance = {1.f, 1.f, 1.f};
          continue;
        }

        irradiance = irradiance + light.radiance * (arrival.attenuation *
                                                    arrival.normal_dot_light);
      }

      total = total + irradiance;
    }

  if (inside_count == 0) return false;

  out_value = total * (1.f / (float)inside_count);
  for (float &coverage : out_coverage) coverage *= 1.f / (float)inside_count;
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

        for (float &channel : scratch.neighbour_total) channel = 0.f;
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

            const Span<float> neighbour_values = scratch.at(neighbour);
            for (uint32_t channel = 0; channel < neighbour_values.size(); ++channel)
              scratch.neighbour_total[channel] += neighbour_values[channel];
            ++neighbour_count;
          }

        if (neighbour_count == 0) continue;

        const Span<float> filled = scratch.at(index);
        for (uint32_t channel = 0; channel < filled.size(); ++channel)
          filled[channel] = scratch.neighbour_total[channel] / (float)neighbour_count;
        scratch.written[index] = 1;
        filled_any = true;
      }

    if (!filled_any) return;
  }
}

// Everything a chart solve READS. Bundled because the four of them travel
// together to every chart and none of them is the chart's own.
struct solve_inputs_t
{
  const lightmap_bake_settings_t &settings;
  const lightmap_solve_settings_t &solve_settings;
  const std::vector<baked_light_t> &lights;
  const Bounding_Volume_Hierarchy &bvh;
};

// Everything one WRITES. The two page sets are the bake; the masks are the debug
// view and may be null.
struct solve_outputs_t
{
  lightmap_pages_t &irradiance_pages;
  lightmap_pages_t &visibility_pages;
  lightmap_visibility_masks_t *masks = nullptr;
};

// Which lights this chart keeps: the strongest LIGHTMAP_LIGHTS_PER_CHART by
// delivered light, written into the chart's slots, with the rest reported.
//
// Ties break on the slot index so a rebake ranks them the same way -- the
// weights are summed in a fixed order by one thread, so nothing else about this
// is at the mercy of the scheduler either.
void choose_chart_lights(lightmap_chart_t &chart, chart_scratch_t &scratch,
                         const std::vector<baked_light_t> &lights,
                         std::vector<dropped_light_t> &dropped)
{
  scratch.ranked_lights.clear();
  for (uint32_t slot = 0; slot < (uint32_t)lights.size(); ++slot)
    if (scratch.light_weight[slot] > 0.f) scratch.ranked_lights.push_back(slot);

  std::sort(scratch.ranked_lights.begin(), scratch.ranked_lights.end(),
            [&](uint32_t left, uint32_t right) {
              if (scratch.light_weight[left] != scratch.light_weight[right])
                return scratch.light_weight[left] > scratch.light_weight[right];
              return left < right;
            });

  for (int16_t &slot : chart.light_slots) slot = LIGHTMAP_NO_LIGHT_SLOT;

  for (size_t rank = 0; rank < scratch.ranked_lights.size(); ++rank)
  {
    const uint32_t slot = scratch.ranked_lights[rank];
    if (rank < LIGHTMAP_LIGHTS_PER_CHART)
    {
      chart.light_slots[(uint32_t)rank] = (int16_t)slot;
      continue;
    }
    dropped.push_back({chart.object_uid, lights[slot].uid});
  }
}

void solve_chart(lightmap_chart_t &chart, const solve_inputs_t &in,
                 const solve_outputs_t &out, chart_scratch_t &scratch,
                 std::vector<dropped_light_t> &dropped)
{
  const lightmap_bake_settings_t &settings = in.settings;
  const lightmap_solve_settings_t &solve_settings = in.solve_settings;
  const std::vector<baked_light_t> &lights = in.lights;

  const int width = chart.atlas_rect.width;
  const int height = chart.atlas_rect.height;
  if (width <= 0 || height <= 0) return;

  // The coverage rides the same scratch as the irradiance, as extra CHANNELS,
  // so ONE gutter dilation covers both -- a second pass over the masks is two
  // things free to disagree about a chart edge.
  scratch.reset(width, height, 3 + (int)lights.size());

  const int covered_width = chart_covered_width(chart, settings);
  const int covered_height = chart_covered_height(chart, settings);

  for (int texel_y = 0; texel_y < covered_height; ++texel_y)
    for (int texel_x = 0; texel_x < covered_width; ++texel_x)
    {
      const size_t index =
          (size_t)(texel_y + settings.gutter_in_texels) * (size_t)width +
          (size_t)(texel_x + settings.gutter_in_texels);
      const Span<float> channels = scratch.at(index);

      linalg::vec3 value{0.f, 0.f, 0.f};
      if (!solve_texel(chart, settings, solve_settings, lights, in.bvh, texel_x, texel_y,
                       value, channels.subspan(3),
                       Span<float>(scratch.light_weight.data(),
                                   (uint32_t)scratch.light_weight.size())))
        continue;

      // Marked written even when it is BLACK, which is what makes the fill
      // correct: a texel in shadow is a surface that got no light, and dilating
      // into it would smear the lit side of a shadow edge back over it.
      channels[0] = value.x;
      channels[1] = value.y;
      channels[2] = value.z;
      scratch.written[index] = 1;
    }

  if (solve_settings.dilate_into_the_gutter) fill_the_gutter(scratch, width, height);

  // Before the store, which is what needs the slots. The ranking itself was
  // summed over the COVERED texels as they were solved, so the gutter gets no
  // vote in it whichever side of the dilation this sits on.
  choose_chart_lights(chart, scratch, lights, dropped);

  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x)
    {
      const size_t index = (size_t)y * (size_t)width + (size_t)x;
      if (!scratch.written[index]) continue;

      const int atlas_x = chart.atlas_rect.min_x + x;
      const int atlas_y = chart.atlas_rect.min_y + y;
      const Span<float> channels = scratch.at(index);
      out.irradiance_pages.store(chart.page, atlas_x, atlas_y,
                                 {channels[0], channels[1], channels[2]});

      // A slot no light claimed stores ZERO, which reads as fully occluded --
      // the same answer an unwritten texel gives, and the safe one: a channel
      // defaulting to 1 is a light nobody baked shining through every wall.
      Array<float, LIGHTMAP_LIGHTS_PER_CHART> coverage;
      for (uint32_t slot = 0; slot < LIGHTMAP_LIGHTS_PER_CHART; ++slot)
      {
        const int16_t light = chart.light_slots[slot];
        if (light == LIGHTMAP_NO_LIGHT_SLOT) continue;
        coverage[slot] = channels[(uint32_t)(3 + light)];
      }
      out.visibility_pages.store_visibility(chart.page, atlas_x, atlas_y, coverage);

      if (!out.masks) continue;
      for (uint32_t slot = 0; slot < (uint32_t)lights.size(); ++slot)
        out.masks->coverage[out.masks->index_of(slot, chart.page, atlas_x, atlas_y)] =
            channels[3 + slot];
    }
}

} // namespace

size_t lightmap_visibility_masks_t::index_of(size_t slot, int page, int x, int y) const
{
  const size_t texels_per_page = (size_t)size_in_texels * (size_t)size_in_texels;
  const size_t texel =
      (size_t)page * texels_per_page + (size_t)y * (size_t)size_in_texels + (size_t)x;
  return slot * texels_per_page * (size_t)page_count + texel;
}

void lightmap_visibility_masks_t::allocate(const lightmap_atlas_t &atlas,
                                           std::vector<entity_uid_t> uids)
{
  size_in_texels = atlas.size_in_texels;
  page_count = atlas.page_count;
  light_uids = std::move(uids);
  coverage.assign(light_uids.size() * (size_t)page_count * (size_t)size_in_texels *
                      (size_t)size_in_texels,
                  0.f);
}

void bake_lightmap(const map_t &map, lightmap_t &lightmap,
                   const lightmap_solve_settings_t &solve_settings,
                   lightmap_visibility_masks_t *out_masks)
{
  const lightmap_atlas_t &atlas = lightmap.atlas;
  const lightmap_bake_settings_t &settings = lightmap.settings;
  std::vector<lightmap_chart_t> &charts = lightmap.charts;

  // Everything a solve decides is cleared first, so a failed bake leaves no half
  // of an older one behind -- a stale slot naming a light this run never looked
  // at is exactly the disagreement the resolve table exists to prevent.
  lightmap.irradiance_pages = {};
  lightmap.visibility_pages = {};
  lightmap.light_uids.clear();
  for (lightmap_chart_t &chart : charts)
    for (int16_t &slot : chart.light_slots) slot = LIGHTMAP_NO_LIGHT_SLOT;
  if (out_masks) *out_masks = {};

  if (atlas.size_in_texels <= 0 || atlas.page_count <= 0)
  {
    log_error("[lightmap] cannot bake into an atlas of {} pages at {} texels.",
              atlas.page_count, atlas.size_in_texels);
    return;
  }

  const std::vector<baked_light_t> lights = collect_lights(map);
  if (lights.empty())
  {
    log_error("[lightmap] the map holds no BAKED light; every texel would bake "
              "black. A map lit entirely by Dynamic lights is this case.");
    return;
  }

  lightmap.irradiance_pages.allocate(atlas, lightmap_pixel_format_t::Rgb9e5);
  lightmap.visibility_pages.allocate(atlas, lightmap_pixel_format_t::Unorm8x4);

  // The resolve table is every BAKED light in the map, in the order the solve
  // walked them, and a chart's slots index into it. It is written before the
  // solve because the slots the solve chooses are meaningless without it.
  lightmap.light_uids.reserve(lights.size());
  for (const baked_light_t &light : lights) lightmap.light_uids.push_back(light.uid);

  log_terminal("[lightmap] {} baked light(s) in the resolve table; a chart keeps the {} "
               "strongest of them.", lightmap.light_uids.size(),
               LIGHTMAP_LIGHTS_PER_CHART);

  if (out_masks) out_masks->allocate(atlas, lightmap.light_uids);

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
    return;
  }

  const solve_inputs_t inputs{settings, solve_settings, lights, bvh};
  const solve_outputs_t outputs{lightmap.irradiance_pages, lightmap.visibility_pages,
                                out_masks};

  unsigned int worker_count = std::thread::hardware_concurrency();
  if (worker_count == 0) worker_count = 1;
  worker_count = std::min((unsigned int)packed_charts.size(), worker_count);

  std::atomic<size_t> next_chart{0};
  std::vector<std::vector<dropped_light_t>> dropped_per_worker(worker_count);

  const auto solve_until_done = [&](unsigned int worker_index) {
    chart_scratch_t scratch;
    for (;;)
    {
      const size_t at = next_chart.fetch_add(1, std::memory_order_relaxed);
      if (at >= packed_charts.size()) return;
      solve_chart(charts[packed_charts[at]], inputs, outputs, scratch,
                  dropped_per_worker[worker_index]);
    }
  };

  std::vector<std::thread> workers;
  workers.reserve(worker_count - 1);
  for (unsigned int i = 1; i < worker_count; ++i)
    workers.emplace_back(solve_until_done, i);

  // The calling thread is one of the workers, so a one-chart bake spawns nothing.
  solve_until_done(0);

  for (std::thread &worker : workers) worker.join();

  // Loud, and after the join for the reason dropped_light_t gives. One line per
  // chart-light pair: an author needs to know WHICH face went dark and which
  // light stopped reaching it, and a count would say neither.
  for (const std::vector<dropped_light_t> &dropped : dropped_per_worker)
    for (const dropped_light_t &drop : dropped)
      log_warning("[lightmap] object {} has a face lit by more than {} baked lights; "
                  "light {} is dropped from it and that face will not be lit by it. "
                  "Merge lights, or set the weakest to Dynamic.",
                  drop.object_uid, LIGHTMAP_LIGHTS_PER_CHART, drop.light_uid);

  set_lightmap_geometry_id(lightmap);
}

} // namespace shared
