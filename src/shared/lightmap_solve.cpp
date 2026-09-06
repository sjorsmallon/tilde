#include "lightmap_solve.hpp"

#include "collision_detection.hpp"
#include "lighting.hpp"
#include "lightmap_gpu.hpp"
#include "lightmap_lights.hpp"
#include "lightmap_probes.hpp"
#include "lightmap_reflections.hpp"
#include "lightmap_trace.hpp"
#include "log.hpp"
#include "map_geometry.hpp"
#include "span.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>
#include <cmath>
#include <thread>

namespace shared
{

namespace
{

// A light a chart could not keep. Collected per worker and logged after the join
// rather than from inside it: the log has no lock, and a line interleaved with
// three others is a line nobody reads.
struct dropped_light_t
{
  entity_uid_t object_uid = 0;
  entity_uid_t light_uid = 0;
};

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
// two dilation passes are two things free to disagree about a chart edge. The
// three constants below are the whole layout, and the indirect block is in it
// for the same reason the coverage is -- one gutter fill, one seam rule.
//
// The indirect block is TWELVE channels: the four SH L1 coefficients of three
// colour channels each, in indirect_sh_l1_t's own order (l0, then l1 by world
// axis). Raw floats, and the bias encoding happens at STORE time -- dilating
// encoded values would average two numbers whose scale is a third one, which is
// wrong in a way no picture would show.
struct chart_scratch_t
{
  static constexpr int IRRADIANCE_CHANNEL = 0;
  static constexpr int INDIRECT_CHANNEL = 3;
  static constexpr int INDIRECT_CHANNEL_COUNT = 12;
  static constexpr int FIRST_LIGHT_CHANNEL = INDIRECT_CHANNEL + INDIRECT_CHANNEL_COUNT;

  int width = 0;
  int height = 0;
  int channel_count = FIRST_LIGHT_CHANNEL;
  std::vector<float> values;
  std::vector<uint8_t> written;
  std::vector<uint8_t> written_at_round_start;
  std::vector<float> neighbour_total;

  // What each light DELIVERS to this chart, summed over its samples, and the
  // ranking that picks the LIGHTMAP_LIGHTS_PER_CHART strongest out of it.
  std::vector<float> light_weight;
  std::vector<uint32_t> ranked_lights;

  // Which lights sum into the IRRADIANCE, one bit per resolve-table slot, which
  // after lighting_def.md ss14 step 6 is exactly the ones this chart could not
  // keep. The runtime shades every slot a chart kept analytically against its
  // baked visibility, so summing one here as well is the ss2 double-count; what
  // is left for the atlas to hold is the residual, and this is the set of it.
  // A bitmask because it is what crosses the seam per chart of a batch.
  uint64_t irradiance_light_mask = 0;

  // The origin of each of the chart's records and how many landed in each
  // texel -- the divisor that turns the sums in `values` into averages. The
  // records themselves and the two result buffers are the REFERENCE path's,
  // which shades a chart on its own a chunk at a time; a batch keeps its records
  // in the batch and leaves only the origins here.
  std::vector<gpu_sample_t> samples;
  std::vector<sample_origin_t> origins;
  std::vector<int> inside_count;
  gpu_direct_results_t direct_results;
  gpu_indirect_results_t indirect_results;

  void reset(int new_width, int new_height, int light_count)
  {
    width = new_width;
    height = new_height;
    channel_count = FIRST_LIGHT_CHANNEL + light_count;
    const size_t count = (size_t)width * (size_t)height;
    values.assign(count * (size_t)channel_count, 0.f);
    written.assign(count, 0);
    inside_count.assign(count, 0);
    neighbour_total.assign((size_t)channel_count, 0.f);
    light_weight.assign((size_t)light_count, 0.f);
    irradiance_light_mask = 0;
    ranked_lights.clear();
    samples.clear();
    origins.clear();
  }

  [[nodiscard]] Span<float> at(size_t index)
  {
    return Span<float>(values.data() + index * (size_t)channel_count,
                       (uint32_t)channel_count);
  }
};

// Everything a chart solve READS. Bundled because they travel together to every
// chart and none of them is the chart's own. `traced_scene` is null on a bake
// nobody asked indirect light of, which is what gates the tracer.
struct solve_inputs_t
{
  const lightmap_bake_settings_t &settings;
  const lightmap_solve_settings_t &solve_settings;
  const std::vector<baked_light_t> &lights;
  const Bounding_Volume_Hierarchy &bvh;
  const traced_scene_t *traced_scene = nullptr;
  gpu_bake_settings_t shade_settings;
  indirect_trace_settings_t indirect;
};

// What a bake COST, per worker and summed after the join: the numbers
// lightmap_gpu_plan.md step 0 asks for, and what says which of its kernels
// mattered. Rays are counted where the rule that spends them is applied
// (shadow_ray_count), chains where they are fired. The two clocks are
// thread-seconds -- summed across workers, not wall time.
struct solve_statistics_t
{
  size_t samples = 0;
  shade_statistics_t shade;
  int64_t direct_nanoseconds = 0;
  int64_t indirect_nanoseconds = 0;

  void add(const solve_statistics_t &other)
  {
    samples += other.samples;
    shade.add(other.shade);
    direct_nanoseconds += other.direct_nanoseconds;
    indirect_nanoseconds += other.indirect_nanoseconds;
  }
};

int64_t nanoseconds_since(std::chrono::steady_clock::time_point started)
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() -
                                                              started)
      .count();
}

// How many records are shaded before their answers are reduced into texels. A
// budget of result FLOATS rather than a count of records, because every record's
// answer carries two floats per light; at 64 lights this is ~8000 records and 4
// MB per worker. Where the GPU batch loop grows from.
constexpr size_t RESULT_BUDGET_IN_FLOATS = 1u << 20;

// Every sample of the chart that sample_chart puts ON the surface, as records:
// the NxN stratified points of each covered texel's footprint. A sample outside
// the face is EXCLUDED rather than recorded as dark -- outside the face there is
// no surface to be dark, and counting it would darken every chart edge by the
// fraction of the texel that hangs off it. A texel with no sample inside is the
// gutter pass's problem, not the solve's.
//
// A sample BURIED inside a neighbouring solid is excluded for the same reason: a
// wall's face runs on below the floor it stands in, and a point inside the floor
// is not a surface either. Shading it made it BLACK -- the BVH answers an origin
// inside a solid as a hit at zero distance, which every shadow ray reads as
// occluded -- and a texel straddling the seam averaged that black in and
// bilinear filtering dragged it up the wall: the dark fringe where a wall meets
// a floor. Dropped, the texel is filled from its exposed neighbour by the gutter
// pass, which is what every lightmapper does with a luxel in solid. The test is
// on the RAY ORIGIN, the position nudged off its own face, because a point on a
// face counts as inside to bvh_point_is_inside_solid and every sample is on one.
// It is also what makes the ray query pin exact: a GPU tracing SURFACES cannot
// see a solid it starts inside, and after this it never has to.
//
// This is the whole of what the CPU keeps of a texel once the shading has moved
// off it: the records go to whichever solver shades them, the origin list stays
// here and is what puts an answer back. APPENDS, so a batch collects several
// charts into one record list; the two lists grow by the same count.
void collect_chart_samples(const lightmap_chart_t &chart,
                           const lightmap_bake_settings_t &settings,
                           const lightmap_solve_settings_t &solve_settings,
                           const Bounding_Volume_Hierarchy &bvh, uint32_t chart_index,
                           std::vector<gpu_sample_t> &out_samples,
                           std::vector<sample_origin_t> &out_origins)
{
  const int samples_per_edge = std::max(solve_settings.samples_per_texel_edge, 1);
  const float ray_bias = solve_settings.shadow_ray_bias;
  const int covered_width = chart_covered_width(chart, settings);
  const int covered_height = chart_covered_height(chart, settings);

  for (int texel_y = 0; texel_y < covered_height; ++texel_y)
    for (int texel_x = 0; texel_x < covered_width; ++texel_x)
    {
      const int atlas_x = chart.atlas_rect.min_x + settings.gutter_in_texels + texel_x;
      const int atlas_y = chart.atlas_rect.min_y + settings.gutter_in_texels + texel_y;

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

          const texel_sample_t sample = sample_chart(chart, chart_space);
          if (!sample.on_surface) continue;
          if (bvh_point_is_inside_solid(bvh, sample.position + sample.normal * ray_bias))
            continue;

          out_samples.push_back({sample.position, chart_index, sample.normal, hash});
          out_origins.push_back({texel_x, texel_y});
        }
    }
}

} // namespace

// The DIRECT term at ONE record. Declared in lightmap_gpu.hpp beside the seam,
// because it is the CPU twin of the direct kernel and what the CPU batch solver
// runs; defined here because it IS the solve.
void shade_sample_direct(const gpu_sample_t &sample, Span<const baked_light_t> lights,
                         const Bounding_Volume_Hierarchy &bvh,
                         const gpu_bake_settings_t &settings, uint64_t irradiance_light_mask,
                         linalg::vec3 &out_irradiance, Span<float> out_coverage,
                         Span<float> out_weight, shade_statistics_t &statistics)
{
  const bool binary = settings.mode == lightmap_solve_mode_t::Visibility;

  if (out_coverage.size() != lights.size() || out_weight.size() != lights.size())
    fatal_error("[lightmap] a coverage span of {} and a weight span of {} for {} lights.",
                out_coverage.size(), out_weight.size(), lights.size());
  for (float &coverage : out_coverage) coverage = 0.f;
  for (float &weight : out_weight) weight = 0.f;

  linalg::vec3 irradiance{0.f, 0.f, 0.f};
  for (uint32_t slot = 0; slot < lights.size(); ++slot)
  {
    const scene_light_t &light = lights[slot].light;
    const light_arrival_t arrival = arrival_at(light, sample.position, sample.normal,
                                               settings.directional_shadow_distance);
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
    statistics.direct_rays += (size_t)shadow_ray_count(arrival, settings.soft_shadow_samples);
    const float visibility =
        light_visibility(bvh, sample.position, sample.normal, arrival, settings.shadow_ray_bias,
                         settings.soft_shadow_samples, hash_mix(sample.seed, slot));
    if (visibility <= 0.f) continue;

    out_coverage[slot] = visibility;

    // What the chart's four slots are ranked by, and it is deliberately the
    // light's DELIVERY rather than its coverage: a dim lamp lighting the
    // whole face has coverage 1 everywhere and is not what the face is lit
    // by. N.L is left out for the same reason it is left out of the mask.
    out_weight[slot] = visibility * arrival.attenuation * luminance_of(light.radiance);

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
    if (slot >= LIGHT_MASK_BITS || !((irradiance_light_mask >> slot) & 1u)) continue;

    irradiance = irradiance + light.radiance * (arrival.attenuation *
                                                arrival.normal_dot_light * visibility);
  }

  out_irradiance = irradiance;
}

indirect_sh_l1_t shade_sample_indirect(const gpu_sample_t &sample, const traced_scene_t &scene,
                                       Span<const baked_light_t> lights,
                                       const indirect_trace_settings_t &settings,
                                       shade_statistics_t &statistics)
{
  statistics.chains += (size_t)std::max(settings.rays_per_sample, 0);
  return trace_indirect_light(scene, lights, sample.position, sample.normal, settings,
                              hash_mix(sample.seed, 0x1b873593u));
}

namespace
{

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
void fill_the_gutter(chart_scratch_t &scratch)
{
  const int width = scratch.width;
  const int height = scratch.height;
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

// The scratch's indirect block and indirect_sh_l1_t are one layout written twice,
// so the reduction in solve_chart and this read are a pair -- a store that
// unpacked in a different order than the solve packed is a bake tinted along
// whichever axis got the wrong channel.
indirect_sh_l1_t read_indirect_channels(Span<const float> channels)
{
  const int at = chart_scratch_t::INDIRECT_CHANNEL;
  indirect_sh_l1_t value;
  value.l0 = {channels[at + 0], channels[at + 1], channels[at + 2]};
  for (int axis = 0; axis < SH_L1_LAYERS_PER_PAGE; ++axis)
    value.l1[axis] = {channels[at + 3 + axis * 3 + 0], channels[at + 3 + axis * 3 + 1],
                      channels[at + 3 + axis * 3 + 2]};
  return value;
}

static_assert(chart_scratch_t::INDIRECT_CHANNEL_COUNT == 3 + SH_L1_LAYERS_PER_PAGE * 3,
              "The indirect block is indirect_sh_l1_t flattened. A fifth coefficient "
              "grows both, and the pair above is where.");

// Everything one WRITES. The four page sets are the bake; the masks are the one
// debug view and may be null. The indirect pair is null on a bake that was not
// asked to trace, which is what gates the tracer -- and the two of them are one
// decision, since an L1 direction has no meaning without the L0 it is normalized
// against.
struct solve_outputs_t
{
  lightmap_pages_t &irradiance_pages;
  lightmap_pages_t &visibility_pages;
  lightmap_pages_t *indirect_l0_pages = nullptr;
  lightmap_pages_t *indirect_l1_pages = nullptr;
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

size_t texel_index_of(const chart_scratch_t &scratch, int gutter, const sample_origin_t &origin)
{
  return (size_t)(origin.texel_y + gutter) * (size_t)scratch.width +
         (size_t)(origin.texel_x + gutter);
}

// A run of direct answers back into the texels their records came from: records
// [first_record, first_record + count) of the chart, answered at results
// [first_result, first_result + count). The reference path's chunk and a chart's
// slice of a batch reduce through this one loop, and the sum is in record order
// either way -- which is what makes a chunk or a batch boundary move no pixel.
//
// On the first pass the coverage, the ranking weight and the per-texel sample
// count are gathered as well; the RESIDUAL pass adds only the irradiance -- the
// coverage it recomputed is the same number, and the ranking is already fixed.
void reduce_direct(chart_scratch_t &scratch, int gutter, size_t first_record, size_t count,
                   const gpu_direct_results_t &results, size_t first_result,
                   bool residual_pass)
{
  const size_t light_count = results.light_count;
  for (size_t k = 0; k < count; ++k)
  {
    const size_t texel = texel_index_of(scratch, gutter, scratch.origins[first_record + k]);
    const size_t at = first_result + k;
    const Span<float> channels = scratch.at(texel);

    const linalg::vec3 &irradiance = results.irradiance[at];
    channels[chart_scratch_t::IRRADIANCE_CHANNEL + 0] += irradiance.x;
    channels[chart_scratch_t::IRRADIANCE_CHANNEL + 1] += irradiance.y;
    channels[chart_scratch_t::IRRADIANCE_CHANNEL + 2] += irradiance.z;
    if (residual_pass) continue;

    ++scratch.inside_count[texel];
    for (size_t slot = 0; slot < light_count; ++slot)
    {
      channels[(uint32_t)(chart_scratch_t::FIRST_LIGHT_CHANNEL + slot)] +=
          results.coverage[at * light_count + slot];
      scratch.light_weight[slot] += results.weight[at * light_count + slot];
    }
  }
}

// A run of indirect answers, into the twelve indirect channels of their texels.
void reduce_indirect(chart_scratch_t &scratch, int gutter, size_t first_record, size_t count,
                     const std::vector<indirect_sh_l1_t> &results, size_t first_result)
{
  for (size_t k = 0; k < count; ++k)
  {
    const Span<float> channels =
        scratch.at(texel_index_of(scratch, gutter, scratch.origins[first_record + k]));
    const indirect_sh_l1_t &value = results[first_result + k];
    const int at = chart_scratch_t::INDIRECT_CHANNEL;
    channels[at + 0] += value.l0.x;
    channels[at + 1] += value.l0.y;
    channels[at + 2] += value.l0.z;
    for (int axis = 0; axis < SH_L1_LAYERS_PER_PAGE; ++axis)
    {
      channels[at + 3 + axis * 3 + 0] += value.l1[axis].x;
      channels[at + 3 + axis * 3 + 1] += value.l1[axis].y;
      channels[at + 3 + axis * 3 + 2] += value.l1[axis].z;
    }
  }
}

// The sums above become the texel's AVERAGE over the samples that landed in it.
// The irradiance is normalized after each direct pass; everything else once,
// since the residual pass touches nothing else.
void normalize_texels(chart_scratch_t &scratch, size_t light_count, bool trace_indirect,
                      bool irradiance_only)
{
  const size_t texel_count = (size_t)scratch.width * (size_t)scratch.height;
  for (size_t index = 0; index < texel_count; ++index)
  {
    const int count = scratch.inside_count[index];
    if (count == 0) continue;
    const float inverse = 1.f / (float)count;
    const Span<float> channels = scratch.at(index);

    for (int channel = 0; channel < 3; ++channel)
      channels[chart_scratch_t::IRRADIANCE_CHANNEL + channel] *= inverse;
    if (irradiance_only) continue;

    if (trace_indirect)
      for (int channel = 0; channel < chart_scratch_t::INDIRECT_CHANNEL_COUNT; ++channel)
        channels[chart_scratch_t::INDIRECT_CHANNEL + channel] *= inverse;
    for (size_t slot = 0; slot < light_count; ++slot)
      channels[(uint32_t)(chart_scratch_t::FIRST_LIGHT_CHANNEL + slot)] *= inverse;

    // Marked written even when it is BLACK, which is what makes the fill
    // correct: a texel in shadow is a surface that got no light, and dilating
    // into it would smear the lit side of a shadow edge back over it.
    scratch.written[index] = 1;
  }
}

void clear_irradiance(chart_scratch_t &scratch)
{
  const size_t texel_count = (size_t)scratch.width * (size_t)scratch.height;
  for (size_t index = 0; index < texel_count; ++index)
  {
    const Span<float> channels = scratch.at(index);
    for (int channel = 0; channel < 3; ++channel)
      channels[chart_scratch_t::IRRADIANCE_CHANNEL + channel] = 0.f;
  }
}

// The lights a ranked chart DROPPED, as the mask the residual pass sums under:
// zero for a chart that kept everything that reached it.
uint64_t residual_light_mask(const chart_scratch_t &scratch)
{
  uint64_t mask = 0;
  for (size_t rank = LIGHTMAP_LIGHTS_PER_CHART; rank < scratch.ranked_lights.size(); ++rank)
    mask |= (uint64_t)1 << scratch.ranked_lights[rank];
  return mask;
}

// Indirect is a MODE of the direct solve in exactly the sense the visibility one
// is not: it rides the same sample points and the same walk, so it is a flag
// rather than a pass. Visibility mode is out because its irradiance channels are
// a picture of the shadow rays and not a term anything composes.
bool traces_indirect(const solve_inputs_t &in, const solve_outputs_t &out)
{
  return out.indirect_l0_pages && out.indirect_l1_pages && in.traced_scene &&
         in.solve_settings.mode != lightmap_solve_mode_t::Visibility;
}

// The solved chart into the pages: the last step of both paths.
void store_chart(const lightmap_chart_t &chart, chart_scratch_t &scratch,
                 const solve_outputs_t &out, size_t light_count)
{
  for (int y = 0; y < scratch.height; ++y)
    for (int x = 0; x < scratch.width; ++x)
    {
      const size_t index = (size_t)y * (size_t)scratch.width + (size_t)x;
      if (!scratch.written[index]) continue;

      const int atlas_x = chart.atlas_rect.min_x + x;
      const int atlas_y = chart.atlas_rect.min_y + y;
      const Span<float> channels = scratch.at(index);
      out.irradiance_pages.store(
          chart.page, atlas_x, atlas_y,
          {channels[chart_scratch_t::IRRADIANCE_CHANNEL + 0],
           channels[chart_scratch_t::IRRADIANCE_CHANNEL + 1],
           channels[chart_scratch_t::IRRADIANCE_CHANNEL + 2]});

      if (out.indirect_l0_pages && out.indirect_l1_pages)
      {
        const indirect_sh_l1_t indirect = read_indirect_channels(channels);
        out.indirect_l0_pages->store(chart.page, atlas_x, atlas_y, indirect.l0);
        out.indirect_l1_pages->store_l1(chart.page, atlas_x, atlas_y, indirect.l0,
                                        indirect.l1);
      }

      // A slot no light claimed stores ZERO, which reads as fully occluded --
      // the same answer an unwritten texel gives, and the safe one: a channel
      // defaulting to 1 is a light nobody baked shining through every wall.
      Array<float, LIGHTMAP_LIGHTS_PER_CHART> coverage;
      for (uint32_t slot = 0; slot < LIGHTMAP_LIGHTS_PER_CHART; ++slot)
      {
        const int16_t light = chart.light_slots[slot];
        if (light == LIGHTMAP_NO_LIGHT_SLOT) continue;
        coverage[slot] =
            channels[(uint32_t)(chart_scratch_t::FIRST_LIGHT_CHANNEL + light)];
      }
      out.visibility_pages.store_visibility(chart.page, atlas_x, atlas_y, coverage);

      if (!out.masks) continue;
      for (uint32_t slot = 0; slot < (uint32_t)light_count; ++slot)
        out.masks->coverage[out.masks->index_of(slot, chart.page, atlas_x, atlas_y)] =
            channels[chart_scratch_t::FIRST_LIGHT_CHANNEL + slot];
    }
}

// The REFERENCE path: one chart, shaded on the calling worker a chunk at a time.
void solve_chart(lightmap_chart_t &chart, const solve_inputs_t &in,
                 const solve_outputs_t &out, chart_scratch_t &scratch,
                 std::vector<dropped_light_t> &dropped, solve_statistics_t &statistics)
{
  const lightmap_bake_settings_t &settings = in.settings;
  const lightmap_solve_settings_t &solve_settings = in.solve_settings;
  const std::vector<baked_light_t> &lights = in.lights;
  const size_t light_count = lights.size();
  const int gutter = settings.gutter_in_texels;

  const int width = chart.atlas_rect.width;
  const int height = chart.atlas_rect.height;
  if (width <= 0 || height <= 0) return;

  // The coverage rides the same scratch as the irradiance, as extra CHANNELS,
  // so ONE gutter dilation covers both -- a second pass over the masks is two
  // things free to disagree about a chart edge.
  scratch.reset(width, height, (int)light_count);

  // Every record of the chart, once; both direct passes and the indirect one
  // shade the same list, so the three terms are answers about the same points.
  collect_chart_samples(chart, settings, solve_settings, in.bvh, 0, scratch.samples,
                        scratch.origins);
  statistics.samples += scratch.samples.size();

  const bool trace_indirect = traces_indirect(in, out);

  // Records are shaded a CHUNK at a time and each chunk's answers reduced into the
  // texels they came from before the next. The reduction is a plain sum in record
  // order, so where a chunk boundary falls changes nothing -- which is what lets a
  // batch replace a chunk without moving a pixel.
  const size_t chunk_capacity =
      std::max<size_t>(1, RESULT_BUDGET_IN_FLOATS / (3 + 2 * std::max<size_t>(light_count, 1)));

  const auto shade_direct = [&](bool residual_pass) {
    for (size_t begin = 0; begin < scratch.samples.size(); begin += chunk_capacity)
    {
      const size_t end = std::min(begin + chunk_capacity, scratch.samples.size());
      gpu_direct_results_t &results = scratch.direct_results;
      results.resize(end - begin, light_count);

      const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
      for (size_t i = begin; i < end; ++i)
      {
        const size_t at = i - begin;
        shade_sample_direct(scratch.samples[i], lights, in.bvh, in.shade_settings,
                            scratch.irradiance_light_mask, results.irradiance[at],
                            Span<float>(results.coverage.data() + at * light_count,
                                        (uint32_t)light_count),
                            Span<float>(results.weight.data() + at * light_count,
                                        (uint32_t)light_count),
                            statistics.shade);
      }
      statistics.direct_nanoseconds += nanoseconds_since(started);

      reduce_direct(scratch, gutter, begin, end - begin, results, 0, residual_pass);
    }
  };

  const auto shade_indirect = [&]() {
    for (size_t begin = 0; begin < scratch.samples.size(); begin += chunk_capacity)
    {
      const size_t end = std::min(begin + chunk_capacity, scratch.samples.size());
      std::vector<indirect_sh_l1_t> &results = scratch.indirect_results.values;
      results.assign(end - begin, indirect_sh_l1_t{});

      const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
      for (size_t i = begin; i < end; ++i)
        results[i - begin] = shade_sample_indirect(scratch.samples[i], *in.traced_scene, lights,
                                                   in.indirect, statistics.shade);
      statistics.indirect_nanoseconds += nanoseconds_since(started);

      reduce_indirect(scratch, gutter, begin, end - begin, results, 0);
    }
  };

  // PASS ONE: the coverage of every light and what each of them delivers. No
  // light sums into the irradiance yet, because which ones may is exactly what
  // the ranking below has not decided.
  shade_direct(false);
  if (trace_indirect) shade_indirect();
  normalize_texels(scratch, light_count, trace_indirect, false);

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
  // the shadow rays for EVERY light and not a term anything composes. With NO
  // indirect: a chain's answer does not depend on which lights the ranking kept,
  // and it is the expensive half of a texel.
  if (solve_settings.mode != lightmap_solve_mode_t::Visibility)
  {
    const uint64_t mask = residual_light_mask(scratch);
    if (mask != 0)
    {
      scratch.irradiance_light_mask = mask;
      clear_irradiance(scratch);
      shade_direct(true);
      normalize_texels(scratch, light_count, trace_indirect, true);
    }
  }

  if (solve_settings.dilate_into_the_gutter) fill_the_gutter(scratch);
  store_chart(chart, scratch, out, light_count);
}

// --- Progress ----------------------------------------------------------------
//
// A bake was one blocking call that said nothing until it was done, which was
// fine while it was seconds and is not now that a texel can cost hundreds of
// path-traced rays. The main thread is INSIDE this call, so an editor progress
// bar is not available to report to -- the terminal is what can be written to
// while the window is frozen, and that is what this is.
//
// Weighted by TEXELS rather than by charts: a chart is one unit of the work
// queue and anything from a handful of texels to a quarter of a million, so a
// chart count reads 90% done while the largest face in the level has not been
// started. Charts are still what the line COUNTS, because that is what an
// author can relate to a level.
struct bake_progress_t
{
  std::chrono::steady_clock::time_point started_at = std::chrono::steady_clock::now();

  size_t total_texels = 0;
  size_t total_charts = 0;

  std::atomic<size_t> solved_texels{0};
  std::atomic<size_t> solved_charts{0};

  // The report SLOT, claimed by whichever worker finished a chart first after
  // the interval elapsed. A compare-exchange rather than a lock, and rather than
  // letting only the calling thread report: worker 0 can be halfway through the
  // one chart that dominates the bake, and a progress line that stops for thirty
  // seconds is worse than none. Claiming it is what keeps two lines from
  // interleaving, which is the reason the dropped-light warnings wait for the
  // join.
  std::atomic<int64_t> next_report_at_milliseconds{0};

  [[nodiscard]] int64_t elapsed_milliseconds() const
  {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - started_at)
        .count();
  }
};

// Nothing at all until a bake has been running this long, so a fast direct bake
// is exactly as quiet as it always was, and then one line at this interval.
constexpr int64_t FIRST_PROGRESS_REPORT_IN_MILLISECONDS = 2000;
constexpr int64_t PROGRESS_REPORT_INTERVAL_IN_MILLISECONDS = 2000;

void report_progress_if_it_is_time(bake_progress_t &progress)
{
  const int64_t elapsed = progress.elapsed_milliseconds();

  int64_t due = progress.next_report_at_milliseconds.load(std::memory_order_relaxed);
  if (elapsed < due) return;

  // One winner takes the slot and moves it forward; everyone else keeps solving.
  if (!progress.next_report_at_milliseconds.compare_exchange_strong(
          due, elapsed + PROGRESS_REPORT_INTERVAL_IN_MILLISECONDS,
          std::memory_order_relaxed))
    return;

  const size_t solved = progress.solved_texels.load(std::memory_order_relaxed);
  const double fraction =
      progress.total_texels > 0 ? (double)solved / (double)progress.total_texels : 1.0;

  const double elapsed_seconds = (double)elapsed / 1000.0;

  // An estimate from the work done so far, which is what makes it an estimate:
  // the charts left are not the charts done, and the residual pass solves some of
  // them twice. Still the difference between "it is working" and "it is stuck".
  const double remaining_seconds =
      fraction > 0.0 ? elapsed_seconds / fraction - elapsed_seconds : 0.0;

  log_terminal("[lightmap] {:.0f}% solved ({} of {} charts), {:.1f}s elapsed, "
               "~{:.0f}s remaining.",
               fraction * 100.0, progress.solved_charts.load(std::memory_order_relaxed),
               progress.total_charts, elapsed_seconds, remaining_seconds);
}

// --- The batched path (lightmap_gpu_plan.md step 3) -------------------------

// One chart's place in a batch: where its records start in the batch's record
// list, and which scratch holds its texels.
struct batch_entry_t
{
  size_t chart = 0;
  size_t scratch = 0;
  size_t first_sample = 0;
  size_t sample_count = 0;
};

// Every packed chart, in batches of WHOLE charts, through a solver. The same
// passes solve_chart runs, in the same order per chart, over batch-wide record
// lists: collect, the direct dispatch under an all-zero mask, the indirect
// dispatch, normalize and rank each chart, the RESIDUAL dispatch for the charts
// that dropped a light, then the gutter and the store. Serial on the calling
// thread -- the solver is where the parallelism is -- and the pixels are the
// reference's bit for bit, which lightmap_bake_test pins.
void solve_charts_in_batches(std::vector<lightmap_chart_t> &charts,
                             Span<const size_t> packed_charts, const solve_inputs_t &in,
                             const solve_outputs_t &out, lightmap_batch_solver_t &solver,
                             bake_progress_t &progress, std::vector<dropped_light_t> &dropped,
                             solve_statistics_t &statistics, size_t &out_batch_count)
{
  const lightmap_bake_settings_t &settings = in.settings;
  const lightmap_solve_settings_t &solve_settings = in.solve_settings;
  const std::vector<baked_light_t> &lights = in.lights;
  const size_t light_count = lights.size();
  const int gutter = settings.gutter_in_texels;
  const bool trace_indirect = traces_indirect(in, out);
  const size_t strata = (size_t)std::max(solve_settings.samples_per_texel_edge, 1);

  // A record's answer is 3 + 2 * light_count floats direct and 12 indirect, and
  // the budget covers the larger. A batch is cut by the UPPER bound of what a
  // chart can add -- covered texels times the strata, before any sample is
  // excluded for missing the face -- so the boundary is decided before anything
  // is collected and never has to give a record back.
  const size_t floats_per_sample = std::max<size_t>(3 + 2 * light_count, 12);
  const size_t capacity_in_samples =
      std::max<size_t>(1, solver.result_budget_in_floats() / floats_per_sample);

  std::vector<chart_scratch_t> scratches;
  std::vector<batch_entry_t> entries;
  std::vector<gpu_sample_t> samples;
  std::vector<uint64_t> masks;
  gpu_direct_results_t direct_results;
  gpu_indirect_results_t indirect_results;

  std::vector<batch_entry_t> residual_entries;
  std::vector<gpu_sample_t> residual_samples;
  std::vector<uint64_t> residual_masks;

  const auto check_direct_results = [&](size_t sample_count) {
    if (direct_results.irradiance.size() != sample_count ||
        direct_results.light_count != light_count ||
        direct_results.coverage.size() != sample_count * light_count ||
        direct_results.weight.size() != sample_count * light_count)
      fatal_error("[lightmap] {} answered {} record(s) for {} light(s) with {} irradiance, "
                  "{} coverage and {} weight value(s) at a light count of {}.",
                  solver.name(), sample_count, light_count, direct_results.irradiance.size(),
                  direct_results.coverage.size(), direct_results.weight.size(),
                  direct_results.light_count);
  };

  size_t cursor = 0;
  while (cursor < packed_charts.size())
  {
    entries.clear();
    samples.clear();
    size_t batch_texels = 0;

    // Whole charts until the next would not fit; the FIRST always does.
    while (cursor < packed_charts.size())
    {
      const size_t chart_index = packed_charts[(uint32_t)cursor];
      const lightmap_chart_t &chart = charts[chart_index];
      const size_t texels = (size_t)chart_covered_width(chart, settings) *
                            (size_t)chart_covered_height(chart, settings);
      if (!entries.empty() && samples.size() + texels * strata * strata > capacity_in_samples)
        break;
      ++cursor;

      if (chart.atlas_rect.width <= 0 || chart.atlas_rect.height <= 0) continue;

      if (entries.size() >= scratches.size()) scratches.emplace_back();
      chart_scratch_t &scratch = scratches[entries.size()];
      scratch.reset(chart.atlas_rect.width, chart.atlas_rect.height, (int)light_count);

      const size_t first_sample = samples.size();
      collect_chart_samples(chart, settings, solve_settings, in.bvh, (uint32_t)entries.size(),
                            samples, scratch.origins);
      entries.push_back(
          {chart_index, entries.size(), first_sample, samples.size() - first_sample});
      batch_texels += texels;
    }
    if (entries.empty()) continue;

    statistics.samples += samples.size();
    ++out_batch_count;

    // PASS ONE: the coverage of every light and what each of them delivers. No
    // light sums into the irradiance yet, because which ones may is exactly what
    // the ranking below has not decided.
    masks.assign(entries.size(), 0);
    {
      const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
      solver.solve_direct(samples, masks, direct_results);
      statistics.direct_nanoseconds += nanoseconds_since(started);
    }
    check_direct_results(samples.size());
    for (const batch_entry_t &entry : entries)
      reduce_direct(scratches[entry.scratch], gutter, 0, entry.sample_count, direct_results,
                    entry.first_sample, false);

    if (trace_indirect)
    {
      const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
      solver.solve_indirect(samples, indirect_results);
      statistics.indirect_nanoseconds += nanoseconds_since(started);
      if (indirect_results.values.size() != samples.size())
        fatal_error("[lightmap] {} answered {} record(s) with {} indirect value(s).",
                    solver.name(), samples.size(), indirect_results.values.size());
      for (const batch_entry_t &entry : entries)
        reduce_indirect(scratches[entry.scratch], gutter, 0, entry.sample_count,
                        indirect_results.values, entry.first_sample);
    }

    for (const batch_entry_t &entry : entries)
    {
      normalize_texels(scratches[entry.scratch], light_count, trace_indirect, false);
      choose_chart_lights(charts[entry.chart], scratches[entry.scratch], lights, dropped);
    }

    // PASS TWO, for the charts that could not keep every light that reached
    // them: their records again, under the mask of what each dropped. ONE
    // dispatch for all of them, each record naming its chart in the residual
    // batch's own table. Skipped in Visibility mode for solve_chart's reason.
    if (solve_settings.mode != lightmap_solve_mode_t::Visibility)
    {
      residual_entries.clear();
      residual_samples.clear();
      residual_masks.clear();
      for (const batch_entry_t &entry : entries)
      {
        chart_scratch_t &scratch = scratches[entry.scratch];
        const uint64_t mask = residual_light_mask(scratch);
        if (mask == 0) continue;
        scratch.irradiance_light_mask = mask;
        clear_irradiance(scratch);

        const size_t first_sample = residual_samples.size();
        for (size_t k = 0; k < entry.sample_count; ++k)
        {
          gpu_sample_t sample = samples[entry.first_sample + k];
          sample.chart_index = (uint32_t)residual_masks.size();
          residual_samples.push_back(sample);
        }
        residual_masks.push_back(mask);
        residual_entries.push_back(
            {entry.chart, entry.scratch, first_sample, entry.sample_count});
      }

      if (!residual_samples.empty())
      {
        const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
        solver.solve_direct(residual_samples, residual_masks, direct_results);
        statistics.direct_nanoseconds += nanoseconds_since(started);
        check_direct_results(residual_samples.size());
        for (const batch_entry_t &entry : residual_entries)
        {
          reduce_direct(scratches[entry.scratch], gutter, 0, entry.sample_count,
                        direct_results, entry.first_sample, true);
          normalize_texels(scratches[entry.scratch], light_count, trace_indirect, true);
        }
      }
    }

    for (const batch_entry_t &entry : entries)
    {
      if (solve_settings.dilate_into_the_gutter) fill_the_gutter(scratches[entry.scratch]);
      store_chart(charts[entry.chart], scratches[entry.scratch], out, light_count);
    }

    progress.solved_texels.fetch_add(batch_texels, std::memory_order_relaxed);
    progress.solved_charts.fetch_add(entries.size(), std::memory_order_relaxed);
    report_progress_if_it_is_time(progress);
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

lightmap_sample_set_t collect_lightmap_sample_set(const lightmap_t &lightmap,
                                                 const lightmap_solve_settings_t &solve_settings,
                                                 const Bounding_Volume_Hierarchy &bvh)
{
  lightmap_sample_set_t set;
  for (size_t chart_index = 0; chart_index < lightmap.charts.size(); ++chart_index)
  {
    const lightmap_chart_t &chart = lightmap.charts[chart_index];
    if (chart.page < 0) continue;
    if (chart.atlas_rect.width > 0 && chart.atlas_rect.height > 0)
      collect_chart_samples(chart, lightmap.settings, solve_settings, bvh,
                            (uint32_t)set.charts.size(), set.samples, set.origins);
    set.charts.push_back(chart_index);
  }
  return set;
}

std::vector<gpu_sample_t> collect_lightmap_samples(const lightmap_t &lightmap,
                                                   const lightmap_solve_settings_t &solve_settings,
                                                   const Bounding_Volume_Hierarchy &bvh)
{
  return std::move(collect_lightmap_sample_set(lightmap, solve_settings, bvh).samples);
}

void bake_lightmap(const map_t &map, lightmap_t &lightmap,
                   const lightmap_solve_settings_t &solve_settings,
                   lightmap_batch_solver_t *solver, lightmap_visibility_masks_t *out_masks)
{
  const lightmap_atlas_t &atlas = lightmap.atlas;
  const lightmap_bake_settings_t &settings = lightmap.settings;
  std::vector<lightmap_chart_t> &charts = lightmap.charts;

  // Everything a solve decides is cleared first, so a failed bake leaves no half
  // of an older one behind -- a stale slot naming a light this run never looked
  // at is exactly the disagreement the resolve table exists to prevent.
  lightmap.irradiance_pages = {};
  lightmap.visibility_pages = {};
  lightmap.indirect_l0_pages = {};
  lightmap.indirect_l1_pages = {};
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
  if (lights.size() > LIGHT_MASK_BITS)
  {
    log_error("[lightmap] the map holds {} baked lights; a chart's residual light mask "
              "holds {} bits, which is also every slot the runtime's light array has. "
              "Set the excess to Dynamic.",
              lights.size(), LIGHT_MASK_BITS);
    return;
  }

  const bool trace_indirect =
      solve_settings.trace_indirect_light &&
      solve_settings.mode != lightmap_solve_mode_t::Visibility;

  lightmap.irradiance_pages.allocate(atlas, lightmap_pixel_format_t::Rgb9e5);
  lightmap.visibility_pages.allocate(atlas, lightmap_pixel_format_t::Unorm8x4);
  if (trace_indirect)
  {
    lightmap.indirect_l0_pages.allocate(atlas, lightmap_pixel_format_t::Rgb9e5);
    lightmap.indirect_l1_pages.allocate(atlas, lightmap_pixel_format_t::Unorm8x4,
                                        SH_L1_LAYERS_PER_PAGE);
  }

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

  // Translated ONCE at the CALL SITE rather than handed the whole solve settings:
  // the shade wants values, and the tracer's are derived from the same block so
  // three of them are the same shadow rules the direct term uses -- which is what
  // keeps the bounce on one lighting model.
  gpu_bake_settings_t shade_settings = gpu_bake_settings_from(solve_settings);
  // A bake that does not trace fires no chain ANYWHERE -- not for a texel, not
  // for a probe -- and that is decided here, once, before a solver takes its
  // copy of the settings: the probe half shades under the solver's copy.
  if (!trace_indirect) shade_settings.rays_per_sample = 0;
  const indirect_trace_settings_t indirect = indirect_trace_settings_from(shade_settings);

  // Built once, on this thread: resolving a material LOADS a texture, and the
  // workers below must find every one of them already in the pool. A solver
  // always gets it, because the triangle scene it traces is derived from it.
  const bool bake_probes = solve_settings.bake_probes &&
                           solve_settings.mode != lightmap_solve_mode_t::Visibility;
  const traced_scene_t traced_scene = (trace_indirect || bake_probes || solver)
                                          ? build_traced_scene(map, bvh)
                                          : traced_scene_t{};

  const gpu_bake_scene_t gpu_scene =
      solver ? build_gpu_bake_scene(map, traced_scene) : gpu_bake_scene_t{};
  if (solver)
    solver->upload_scene({&gpu_scene, &bvh, &traced_scene, Span<const baked_light_t>(lights),
                          shade_settings});

  if (trace_indirect)
    log_terminal("[lightmap] tracing indirect light: {} chain(s) per texel sample, "
                 "roulette after {} bounce(s).",
                 indirect.rays_per_sample, indirect.bounces_before_roulette);

  const solve_inputs_t inputs{settings,
                              solve_settings,
                              lights,
                              bvh,
                              trace_indirect ? &traced_scene : nullptr,
                              shade_settings,
                              indirect};
  const solve_outputs_t outputs{lightmap.irradiance_pages,
                                lightmap.visibility_pages,
                                trace_indirect ? &lightmap.indirect_l0_pages : nullptr,
                                trace_indirect ? &lightmap.indirect_l1_pages : nullptr,
                                out_masks};

  unsigned int worker_count = std::thread::hardware_concurrency();
  if (worker_count == 0) worker_count = 1;
  worker_count = std::min((unsigned int)packed_charts.size(), worker_count);
  // The batched path runs the chart loop on this thread; the solver is where the
  // parallelism is.
  if (solver) worker_count = 1;

  std::atomic<size_t> next_chart{0};
  std::vector<std::vector<dropped_light_t>> dropped_per_worker(worker_count);
  std::vector<solve_statistics_t> statistics_per_worker(worker_count);

  bake_progress_t progress;
  progress.total_charts = packed_charts.size();
  progress.next_report_at_milliseconds = FIRST_PROGRESS_REPORT_IN_MILLISECONDS;
  for (const size_t at : packed_charts)
    progress.total_texels +=
        (size_t)chart_covered_width(charts[at], settings) *
        (size_t)chart_covered_height(charts[at], settings);

  const auto solve_until_done = [&](unsigned int worker_index) {
    chart_scratch_t scratch;
    for (;;)
    {
      const size_t at = next_chart.fetch_add(1, std::memory_order_relaxed);
      if (at >= packed_charts.size()) return;

      const lightmap_chart_t &chart = charts[packed_charts[at]];
      const size_t texels = (size_t)chart_covered_width(chart, settings) *
                            (size_t)chart_covered_height(chart, settings);

      solve_chart(charts[packed_charts[at]], inputs, outputs, scratch,
                  dropped_per_worker[worker_index], statistics_per_worker[worker_index]);

      progress.solved_texels.fetch_add(texels, std::memory_order_relaxed);
      progress.solved_charts.fetch_add(1, std::memory_order_relaxed);
      report_progress_if_it_is_time(progress);
    }
  };

  size_t batch_count = 0;
  if (solver)
  {
    solve_charts_in_batches(charts, Span<const size_t>(packed_charts), inputs, outputs, *solver,
                            progress, dropped_per_worker[0], statistics_per_worker[0],
                            batch_count);
  }
  else
  {
    std::vector<std::thread> workers;
    workers.reserve(worker_count - 1);
    for (unsigned int i = 1; i < worker_count; ++i)
      workers.emplace_back(solve_until_done, i);

    // The calling thread is one of the workers, so a one-chart bake spawns nothing.
    solve_until_done(0);

    for (std::thread &worker : workers) worker.join();
  }

  solve_statistics_t statistics;
  for (const solve_statistics_t &worker : statistics_per_worker) statistics.add(worker);
  // The solver counted its rays where it spent them; the batch loop saw records.
  if (solver) statistics.shade = solver->statistics().shade;

  lightmap.probes = {};
  lightmap.reflections = {};
  const bool bake_captures = solve_settings.bake_reflection_captures && trace_indirect &&
                             solve_settings.mode != lightmap_solve_mode_t::Visibility;
  if (bake_probes || bake_captures)
  {
    const std::optional<probe_grid_t> grid =
        try_build_probe_grid(map, settings.probe_spacing_in_world_units);
    if (!grid)
      log_error("[lightmap] no probe grid could be built at spacing {}; the bake carries "
                "no probes and no reflection captures.",
                settings.probe_spacing_in_world_units);
    else
    {
      if (bake_probes)
        lightmap.probes = bake_probe_volume(*grid, bvh, traced_scene, lights, indirect, solver);
      if (bake_captures)
      {
        const std::vector<uint8_t> inside = classify_probes_inside_solid(*grid, bvh);
        const reflection_capture_settings_t capture_settings{
            settings.reflection_spacing_in_world_units, indirect.directional_shadow_distance};
        lightmap.reflections =
            build_reflection_captures(map, *grid, inside, bvh, capture_settings);
        bake_reflection_captures(lightmap.reflections, traced_scene, lights, indirect,
                                 settings.reflection_size_in_texels, solver);
      }
    }
  }

  // Always, however short: "how long does a bake take on this map" is the
  // question every setting in the panel is really about, and the progress lines
  // above deliberately say nothing on a fast one.
  //
  // The breakdown is lightmap_gpu_plan.md step 0: what was shaded, what it cost
  // in rays and chains, and where the thread-time went -- the number that says
  // which term is worth moving and, afterwards, whether moving it helped. The
  // two times are summed across workers, so on a busy bake they add up to about
  // the wall time times the thread count.
  //
  // The indirect clause is what tells a traced bake from an untraced one
  // AFTERWARDS. They are otherwise indistinguishable from the outside -- the
  // direct pages look the same and the indirect ones simply are not there --
  // and a bake is long enough that "did I have it on?" is a real question by
  // the time it finishes.
  //
  // Under a solver the two times are WALL seconds spent inside its dispatches,
  // and the line says which solver, so a bake that took the batched path can be
  // told from one that did not.
  const std::string shaded_by =
      solver ? std::format("by {} in {} batch(es)", solver->name(), batch_count)
             : std::format("on {} thread(s)", worker_count);
  const char *seconds_unit = solver ? "s" : "thread-s";

  // Gate 6 step 6: the captures clause, on the same rule as the indirect one --
  // a bake with captures and one without look the same from outside, so the
  // line says which this was, and why when it was not.
  std::string captures_clause;
  if (!solve_settings.bake_reflection_captures)
    captures_clause = "off";
  else if (!trace_indirect)
    captures_clause = "NOT baked, they are traced and \"Trace indirect light\" was off";
  else if (solve_settings.mode == lightmap_solve_mode_t::Visibility)
    captures_clause = "NOT baked in Visibility mode";
  else if (lightmap.reflections.captures.empty())
    captures_clause = "NONE placed (no probe grid, or every lattice point buried)";
  else
  {
    const size_t capture_texels = lightmap.reflections.captures.size() *
                                  lightmap.reflections.captures.front().cube.texels_in_mip(0);
    captures_clause = std::format("{} at {}x{} a face, {} texel(s), {} chain(s)",
                                  lightmap.reflections.captures.size(),
                                  settings.reflection_size_in_texels,
                                  settings.reflection_size_in_texels, capture_texels,
                                  capture_texels * (size_t)indirect.rays_per_sample);
  }

  log_terminal("[lightmap] baked {} chart(s), {} texel(s), {} sample(s) {} in {:.2f}s wall: "
               "direct {} shadow ray(s) in {:.1f} {}; indirect {}; captures {}.",
               packed_charts.size(), progress.total_texels, statistics.samples, shaded_by,
               (double)progress.elapsed_milliseconds() / 1000.0, statistics.shade.direct_rays,
               (double)statistics.direct_nanoseconds * 1e-9, seconds_unit,
               trace_indirect
                   ? std::format("{} chain(s) ({} per sample) in {:.1f} {}",
                                 statistics.shade.chains, indirect.rays_per_sample,
                                 (double)statistics.indirect_nanoseconds * 1e-9, seconds_unit)
                   : std::string("NOT traced, \"Trace indirect light\" was off"),
               captures_clause);

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

  // The opposite failure, and the quieter one: a light the bake saw that no
  // face kept has no slot on any vertex, so a Baked one draws NOTHING at
  // runtime, and a Mixed one keeps only its analytic half. Its cone or its range
  // reaches no face, or every face it reaches is occluded.
  for (size_t slot = 0; slot < lightmap.light_uids.size(); ++slot)
    if (count_charts_keeping_light(lightmap, (int16_t)slot) == 0)
      log_warning("[lightmap] light {} delivered nothing to any face: no chart kept it, "
                  "so a Baked light here lights nothing. Check its range, its cone and "
                  "what stands between it and the geometry.",
                  lightmap.light_uids[slot]);

  set_lightmap_geometry_id(lightmap);
}

} // namespace shared
