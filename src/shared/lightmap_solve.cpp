#include "lightmap_solve.hpp"

#include "brush.hpp"
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

  // The radius of the disc the emitter subtends AT THIS SURFACE POINT, which is
  // what the shadow rays are spread over. A point or spot's is its own radius; a
  // directional light's source_radius is a tangent, so its disc grows with the
  // distance the ray is cast over. One number either way, so the sampler below
  // needs no light-type branch. Zero is a punctual light and costs one ray.
  float shadow_disc_radius = 0.f;
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
    arrival.shadow_disc_radius = light.source_radius * arrival.distance;
  }
  else
  {
    const linalg::vec3 to_light = light.position - surface_position;
    const float squared_distance = linalg::dot(to_light, to_light);
    arrival.distance = std::sqrt(squared_distance);

    if (arrival.distance > light.range || arrival.distance < 1e-4f) return arrival;

    arrival.direction = to_light * (1.f / arrival.distance);
    arrival.attenuation =
        distance_attenuation(squared_distance, light.range, light.source_radius);
    arrival.shadow_disc_radius = light.source_radius;

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

// ONE ray, from the surface toward a named point on the emitter. The primitive
// under both the punctual test and the area one, so a soft shadow cannot use a
// different bias or a different miss rule from a hard one.
bool shadow_ray_reaches(const Bounding_Volume_Hierarchy &bvh,
                        const linalg::vec3 &surface_position,
                        const linalg::vec3 &surface_normal,
                        const linalg::vec3 &direction, float distance,
                        float shadow_ray_bias)
{
  const linalg::vec3 origin = surface_position + surface_normal * shadow_ray_bias;

  ray_hit_result_t hit = {};
  if (!bvh_intersect_ray(bvh, origin, direction, hit)) return true;

  return !(hit.hit && hit.t > 0.f && hit.t < distance - shadow_ray_bias);
}

// The jitter is DERIVED, never drawn from shared/rng.hpp's global state: a rebake
// at unchanged settings has to reproduce the same pixels, and the chart loop runs
// on several threads, so a sequence anyone can advance is a bake that differs
// from itself. Keyed by atlas position, which is unique across the whole solve.
uint32_t hash_mix(uint32_t hash, uint32_t value)
{
  for (int byte = 0; byte < 4; ++byte)
  {
    hash ^= (value >> (byte * 8)) & 0xffu;
    hash *= 16777619u;
  }
  return hash;
}

uint32_t sample_hash(int atlas_x, int atlas_y, int page, int sample_index)
{
  uint32_t hash = 2166136261u;
  hash = hash_mix(hash, (uint32_t)atlas_x);
  hash = hash_mix(hash, (uint32_t)atlas_y);
  hash = hash_mix(hash, (uint32_t)page);
  hash = hash_mix(hash, (uint32_t)sample_index);
  return hash;
}

// What FRACTION of the emitter this surface point can see: 1 for an unshadowed
// punctual light, 0 for a fully occluded one, and anything between for a point
// inside a penumbra. It is one number rather than a bool because that is the only
// difference an area light makes to everything downstream -- the coverage stored
// in the visibility channel, the slot ranking and the residual sum all just
// multiply by it.
//
// A light with no size takes exactly ONE ray whatever soft_shadow_samples says,
// so every map authored before area lights existed bakes bit for bit what it did.
float light_visibility(const Bounding_Volume_Hierarchy &bvh,
                       const linalg::vec3 &surface_position,
                       const linalg::vec3 &surface_normal,
                       const light_arrival_t &arrival,
                       const lightmap_solve_settings_t &solve_settings, uint32_t hash)
{
  const int sample_count = arrival.shadow_disc_radius > 0.f
                               ? std::max(solve_settings.soft_shadow_samples, 1)
                               : 1;

  if (sample_count == 1)
    return shadow_ray_reaches(bvh, surface_position, surface_normal, arrival.direction,
                              arrival.distance, solve_settings.shadow_ray_bias)
               ? 1.f
               : 0.f;

  // The emitter is a sphere and this samples the DISC facing the surface, which
  // is the sphere's silhouette from here -- the half of it the surface cannot see
  // is the half that emits nothing toward it.
  linalg::vec3 tangent_u;
  linalg::vec3 tangent_v;
  brush_face_grid_tangents(arrival.direction, tangent_u, tangent_v);

  const linalg::vec3 centre = surface_position + arrival.direction * arrival.distance;

  // The golden angle: consecutive samples land as far from each other in rotation
  // as an irrational turn allows, so a handful of them cover the disc evenly
  // instead of clumping the way independent random angles do.
  constexpr float GOLDEN_ANGLE = 2.39996323f;
  constexpr float TWO_PI = 6.28318531f;

  int reached = 0;
  for (int sample = 0; sample < sample_count; ++sample)
  {
    const uint32_t sample_bits = hash_mix(hash, (uint32_t)sample);

    // sqrt of the stratum, because a disc's area grows with the square of the
    // radius -- sampling the radius uniformly crowds every sample into the middle
    // and gives a penumbra a hard rim.
    const float radius_jitter = (float)(sample_bits & 0xffffu) * (1.f / 65536.f);
    const float angle_jitter = (float)((sample_bits >> 16) & 0xffffu) * (1.f / 65536.f);

    const float radius =
        arrival.shadow_disc_radius *
        std::sqrt(((float)sample + radius_jitter) / (float)sample_count);
    const float angle = (float)sample * GOLDEN_ANGLE + angle_jitter * TWO_PI;

    const linalg::vec3 target = centre + tangent_u * (std::cos(angle) * radius) +
                                tangent_v * (std::sin(angle) * radius);

    const linalg::vec3 to_target = target - surface_position;
    const float distance = std::sqrt(linalg::dot(to_target, to_target));
    if (distance < 1e-4f) continue;

    if (shadow_ray_reaches(bvh, surface_position, surface_normal,
                           to_target * (1.f / distance), distance,
                           solve_settings.shadow_ray_bias))
      ++reached;
  }

  return (float)reached / (float)sample_count;
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

  // Which lights sum into the IRRADIANCE, which after lighting_def.md ss14 step
  // 6 is exactly the ones this chart could not keep. The runtime shades every
  // slot a chart kept analytically against its baked visibility, so summing one
  // here as well is the ss2 double-count; what is left for the atlas to hold is
  // the residual, and this is the set of it.
  std::vector<uint8_t> sums_into_irradiance;
  // Pass two re-derives a weight nothing reads -- the ranking is already fixed.
  std::vector<float> discarded_weight;

  void reset(int width, int height, int channels)
  {
    channel_count = channels;
    const size_t count = (size_t)width * (size_t)height;
    values.assign(count * (size_t)channels, 0.f);
    written.assign(count, 0);
    neighbour_total.assign((size_t)channels, 0.f);
    light_weight.assign((size_t)channels - 3, 0.f);
    discarded_weight.assign((size_t)channels - 3, 0.f);
    sums_into_irradiance.assign((size_t)channels - 3, 0);
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
                 Span<const uint8_t> sums_into_irradiance, linalg::vec3 &out_value,
                 Span<float> out_coverage, Span<float> out_light_weight)
{
  const int samples_per_edge = std::max(solve_settings.samples_per_texel_edge, 1);
  const bool binary = solve_settings.mode == lightmap_solve_mode_t::Visibility;

  const int atlas_x = chart.atlas_rect.min_x + settings.gutter_in_texels + texel_x;
  const int atlas_y = chart.atlas_rect.min_y + settings.gutter_in_texels + texel_y;

  if (out_coverage.size() != (uint32_t)lights.size() ||
      out_light_weight.size() != (uint32_t)lights.size() ||
      sums_into_irradiance.size() != (uint32_t)lights.size())
    fatal_error("[lightmap] a coverage span of {}, a weight span of {} and an "
                "irradiance set of {} for {} lights.",
                out_coverage.size(), out_light_weight.size(),
                sums_into_irradiance.size(), lights.size());
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

        // The rays are cast for every light that ARRIVES, including one the flat
        // face plane faces away from -- that is the one place a visibility costs
        // rays the irradiance sum does not, and it is not optional: the runtime
        // shades with a normal-mapped normal, which at a grazing angle faces a
        // light the geometric normal does not.
        //
        // A FRACTION, not a bool: an area light's penumbra is the share of the
        // emitter this point can see, and every use below is a multiply, so the
        // softness reaches the stored coverage, the slot ranking and the residual
        // by arithmetic rather than by three separate arms.
        const float visibility =
            light_visibility(bvh, world_position, chart.plane.normal, arrival,
                             solve_settings, hash_mix(hash, slot));
        if (visibility <= 0.f) continue;

        out_coverage[slot] += visibility;

        // What the chart's four slots are ranked by, and it is deliberately the
        // light's DELIVERY rather than its coverage: a dim lamp lighting the
        // whole face has coverage 1 everywhere and is not what the face is lit
        // by. N.L is left out for the same reason it is left out of the mask.
        out_light_weight[slot] +=
            visibility * arrival.attenuation * luminance_of(light.radiance);

        if (!arrival.reaches) continue;

        // The two modes, and this is the whole of the difference between them:
        // falloff forced to 1, colour forced to white, nothing left to sum. The
        // mode is a picture of the shadow rays, so it shows every baked light --
        // including the ones the arm below leaves out of what ships. It takes the
        // BRIGHTEST light's visibility rather than a sum, so a penumbra reads as a
        // penumbra instead of saturating the moment two lights overlap.
        if (binary)
        {
          const float strongest = std::max(irradiance.x, visibility);
          irradiance = {strongest, strongest, strongest};
          continue;
        }

        // The atlas holds what the RUNTIME does not evaluate, which is the whole
        // of lighting_def.md ss12 A arriving. Every light a chart KEPT is shaded
        // by mesh_lit.frag with the real light direction and the mask above as
        // its occlusion, so summing one here as well is the double-count ss2
        // exists to prevent -- and it is the flat half that would win, a frozen
        // N.L off the face plane, which is what makes a normal map inert.
        //
        // What is left is the RESIDUAL: the lights this chart ranked below its
        // four and had to drop. They have no visibility channel to be shadowed
        // by, so flat irradiance is the best answer available for them, and it
        // is strictly better than the darkness dropping them used to mean.
        if (!sums_into_irradiance[slot]) continue;

        irradiance = irradiance + light.radiance * (arrival.attenuation *
                                                    arrival.normal_dot_light *
                                                    visibility);
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

  // PASS ONE: the coverage of every light and what each of them delivers. No
  // light sums into the irradiance yet, because which ones may is exactly what
  // the ranking below has not decided.
  const auto solve_covered_texels = [&]() {
    for (int texel_y = 0; texel_y < covered_height; ++texel_y)
      for (int texel_x = 0; texel_x < covered_width; ++texel_x)
      {
        const size_t index =
            (size_t)(texel_y + settings.gutter_in_texels) * (size_t)width +
            (size_t)(texel_x + settings.gutter_in_texels);
        const Span<float> channels = scratch.at(index);

        linalg::vec3 value{0.f, 0.f, 0.f};
        if (!solve_texel(chart, settings, solve_settings, lights, in.bvh, texel_x, texel_y,
                         Span<const uint8_t>(scratch.sums_into_irradiance.data(),
                                             (uint32_t)scratch.sums_into_irradiance.size()),
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
  };

  solve_covered_texels();

  // Before the dilation and before the store. The ranking was summed over the
  // COVERED texels as they were solved, so the gutter gets no vote in it
  // whichever side of the dilation this sits on -- and pass two needs the
  // verdict, which is what pulled it above the fill.
  choose_chart_lights(chart, scratch, lights, dropped);

  // PASS TWO, and only for a chart that could not keep every light that reached
  // it: the residual irradiance of the ones it dropped. Such a chart is solved
  // TWICE, every light and every ray, which is the price of a ranking that
  // cannot be known until the whole chart has been solved once -- and it is paid
  // only where the warning below already says the level has a problem.
  //
  // Skipped in Visibility mode, where the irradiance channels are a picture of
  // the shadow rays for EVERY light and not a term anything composes.
  if (solve_settings.mode != lightmap_solve_mode_t::Visibility &&
      scratch.ranked_lights.size() > LIGHTMAP_LIGHTS_PER_CHART)
  {
    for (size_t rank = LIGHTMAP_LIGHTS_PER_CHART; rank < scratch.ranked_lights.size();
         ++rank)
      scratch.sums_into_irradiance[scratch.ranked_lights[rank]] = 1;

    std::swap(scratch.light_weight, scratch.discarded_weight);
    solve_covered_texels();
    std::swap(scratch.light_weight, scratch.discarded_weight);
  }

  if (solve_settings.dilate_into_the_gutter) fill_the_gutter(scratch, width, height);

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
  // chart-light pair: an author needs to know WHICH face fell back and which
  // light did it, and a count would say neither.
  //
  // A dropped light is no longer LOST -- it is folded into the residual
  // irradiance instead, which is what the atlas holds now, and it KEEPS ITS
  // SHADOW: the sum below is scaled by the same `light_visibility` the coverage is,
  // so the occlusion is baked into the flat colour rather than into a channel of
  // its own. What it loses is everything else a visibility channel buys -- a
  // normal map, a specular highlight, and the ability to be retuned at runtime,
  // because its N.L is frozen against the flat face plane. A quality cliff
  // rather than a black face, and still worth saying out loud.
  for (const std::vector<dropped_light_t> &dropped : dropped_per_worker)
    for (const dropped_light_t &drop : dropped)
      log_warning("[lightmap] object {} has a face lit by more than {} baked lights; "
                  "light {} falls back to flat residual irradiance on it -- shadowed "
                  "still, but with no response to the material's maps and no runtime "
                  "tuning. Merge lights, or set the weakest to Dynamic.",
                  drop.object_uid, LIGHTMAP_LIGHTS_PER_CHART, drop.light_uid);

  set_lightmap_geometry_id(lightmap);
}

} // namespace shared
