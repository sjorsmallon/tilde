#include "lightmap_solve.hpp"

#include "collision_detection.hpp"
#include "lighting.hpp"
#include "lightmap_lights.hpp"
#include "lightmap_probes.hpp"
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

  int channel_count = FIRST_LIGHT_CHANNEL;
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

  void reset(int width, int height, int light_count)
  {
    channel_count = FIRST_LIGHT_CHANNEL + light_count;
    const size_t count = (size_t)width * (size_t)height;
    values.assign(count * (size_t)channel_count, 0.f);
    written.assign(count, 0);
    neighbour_total.assign((size_t)channel_count, 0.f);
    light_weight.assign((size_t)light_count, 0.f);
    discarded_weight.assign((size_t)light_count, 0.f);
    sums_into_irradiance.assign((size_t)light_count, 0);
    ranked_lights.clear();
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
  indirect_trace_settings_t indirect;
};

// What one texel is worth: the NxN stratified samples of its footprint that land
// on the face, averaged. A sample outside the face is EXCLUDED rather than summed
// as zero -- outside the face there is no surface to be dark, and counting it
// would darken every chart edge by the fraction of the texel that hangs off it.
// No sample inside means the texel is the gutter pass's problem, not the solve's.
//
// `out_indirect` is null on the pass that must not re-trace: a chain is the
// expensive half of a texel and its answer does not depend on which lights the
// ranking kept.
bool solve_texel(const lightmap_chart_t &chart, const solve_inputs_t &in, int texel_x,
                 int texel_y, Span<const uint8_t> sums_into_irradiance,
                 linalg::vec3 &out_value, indirect_sh_l1_t *out_indirect,
                 Span<float> out_coverage, Span<float> out_light_weight)
{
  const lightmap_bake_settings_t &settings = in.settings;
  const lightmap_solve_settings_t &solve_settings = in.solve_settings;
  const std::vector<baked_light_t> &lights = in.lights;
  const Bounding_Volume_Hierarchy &bvh = in.bvh;

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
  indirect_sh_l1_t indirect_total;
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

      const texel_sample_t sample = sample_chart(chart, chart_space);
      if (!sample.on_surface) continue;
      ++inside_count;

      const linalg::vec3 world_position = sample.position;

      linalg::vec3 irradiance{0.f, 0.f, 0.f};
      for (uint32_t slot = 0; slot < (uint32_t)lights.size(); ++slot)
      {
        const scene_light_t &light = lights[slot].light;
        const light_arrival_t arrival =
            arrival_at(light, world_position, sample.normal,
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
        const float visibility = light_visibility(
            bvh, world_position, sample.normal, arrival,
            solve_settings.shadow_ray_bias, solve_settings.soft_shadow_samples,
            hash_mix(hash, slot));
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

      // The INDIRECT half, and it shares this sample's position, normal and hash
      // rather than walking the chart a second time -- one set of sample points,
      // so the two terms are answers about the same places on the face.
      //
      // The geometric normal survives here for ONE thing, choosing which
      // hemisphere to fire into. Nothing multiplies by N.L: the texel stores
      // what arrives and the shader applies the cosine against the shaded
      // normal, which is what makes a normal map move the bounce.
      if (out_indirect && in.traced_scene)
      {
        const indirect_sh_l1_t traced = trace_indirect_light(
            *in.traced_scene, lights, world_position, sample.normal, in.indirect,
            hash_mix(hash, 0x1b873593u));

        indirect_total.l0 = indirect_total.l0 + traced.l0;
        for (int axis = 0; axis < SH_L1_LAYERS_PER_PAGE; ++axis)
          indirect_total.l1[axis] = indirect_total.l1[axis] + traced.l1[axis];
      }
    }

  if (inside_count == 0) return false;

  out_value = total * (1.f / (float)inside_count);
  if (out_indirect)
  {
    const float inverse = 1.f / (float)inside_count;
    out_indirect->l0 = indirect_total.l0 * inverse;
    for (int axis = 0; axis < SH_L1_LAYERS_PER_PAGE; ++axis)
      out_indirect->l1[axis] = indirect_total.l1[axis] * inverse;
  }
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

// The scratch's indirect block and indirect_sh_l1_t are one layout written twice,
// so the conversion is a pair rather than two open-coded loops -- a store that
// unpacked in a different order than the solve packed is a bake tinted along
// whichever axis got the wrong channel.
void write_indirect_channels(Span<float> channels, const indirect_sh_l1_t &value)
{
  const int at = chart_scratch_t::INDIRECT_CHANNEL;
  channels[at + 0] = value.l0.x;
  channels[at + 1] = value.l0.y;
  channels[at + 2] = value.l0.z;
  for (int axis = 0; axis < SH_L1_LAYERS_PER_PAGE; ++axis)
  {
    channels[at + 3 + axis * 3 + 0] = value.l1[axis].x;
    channels[at + 3 + axis * 3 + 1] = value.l1[axis].y;
    channels[at + 3 + axis * 3 + 2] = value.l1[axis].z;
  }
}

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
  scratch.reset(width, height, (int)lights.size());

  const int covered_width = chart_covered_width(chart, settings);
  const int covered_height = chart_covered_height(chart, settings);

  // Indirect is a MODE of the direct solve in exactly the sense the visibility
  // one is not: it rides the same sample points and the same walk, so it is a
  // flag rather than a pass. Visibility mode is out because its irradiance
  // channels are a picture of the shadow rays and not a term anything composes.
  const bool trace_indirect =
      out.indirect_l0_pages && out.indirect_l1_pages && in.traced_scene &&
      solve_settings.mode != lightmap_solve_mode_t::Visibility;

  // PASS ONE: the coverage of every light and what each of them delivers. No
  // light sums into the irradiance yet, because which ones may is exactly what
  // the ranking below has not decided.
  const auto solve_covered_texels = [&](bool with_indirect) {
    for (int texel_y = 0; texel_y < covered_height; ++texel_y)
      for (int texel_x = 0; texel_x < covered_width; ++texel_x)
      {
        const size_t index =
            (size_t)(texel_y + settings.gutter_in_texels) * (size_t)width +
            (size_t)(texel_x + settings.gutter_in_texels);
        const Span<float> channels = scratch.at(index);

        linalg::vec3 value{0.f, 0.f, 0.f};
        indirect_sh_l1_t indirect;
        if (!solve_texel(chart, in, texel_x, texel_y,
                         Span<const uint8_t>(scratch.sums_into_irradiance.data(),
                                             (uint32_t)scratch.sums_into_irradiance.size()),
                         value, with_indirect ? &indirect : nullptr,
                         channels.subspan(chart_scratch_t::FIRST_LIGHT_CHANNEL),
                         Span<float>(scratch.light_weight.data(),
                                     (uint32_t)scratch.light_weight.size())))
          continue;

        // Marked written even when it is BLACK, which is what makes the fill
        // correct: a texel in shadow is a surface that got no light, and dilating
        // into it would smear the lit side of a shadow edge back over it.
        channels[chart_scratch_t::IRRADIANCE_CHANNEL + 0] = value.x;
        channels[chart_scratch_t::IRRADIANCE_CHANNEL + 1] = value.y;
        channels[chart_scratch_t::IRRADIANCE_CHANNEL + 2] = value.z;
        if (with_indirect) write_indirect_channels(channels, indirect);
        scratch.written[index] = 1;
      }
  };

  solve_covered_texels(trace_indirect);

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

    // With NO indirect: a chain's answer does not depend on which lights the
    // ranking kept, and it is the expensive half of a texel.
    std::swap(scratch.light_weight, scratch.discarded_weight);
    solve_covered_texels(false);
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
      for (uint32_t slot = 0; slot < (uint32_t)lights.size(); ++slot)
        out.masks->coverage[out.masks->index_of(slot, chart.page, atlas_x, atlas_y)] =
            channels[chart_scratch_t::FIRST_LIGHT_CHANNEL + slot];
    }
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

  // Translated at the CALL SITE rather than handed the whole solve settings: the
  // tracer wants values, and three of these are the same shadow rules the direct
  // term uses, which is what keeps the bounce on one lighting model.
  indirect_trace_settings_t indirect;
  indirect.rays_per_sample = solve_settings.indirect_rays_per_sample;
  indirect.bounces_before_roulette = solve_settings.indirect_bounces_before_roulette;
  indirect.max_bounces = solve_settings.indirect_max_bounces;
  indirect.ray_bias = solve_settings.shadow_ray_bias;
  indirect.shadow_ray_bias = solve_settings.shadow_ray_bias;
  indirect.soft_shadow_samples = solve_settings.soft_shadow_samples;
  indirect.directional_shadow_distance = solve_settings.directional_shadow_distance;

  // Built once, on this thread: resolving a material LOADS a texture, and the
  // workers below must find every one of them already in the pool.
  const bool bake_probes = solve_settings.bake_probes &&
                           solve_settings.mode != lightmap_solve_mode_t::Visibility;
  const traced_scene_t traced_scene = (trace_indirect || bake_probes)
                                          ? build_traced_scene(map, bvh)
                                          : traced_scene_t{};

  if (trace_indirect)
    log_terminal("[lightmap] tracing indirect light: {} chain(s) per texel sample, "
                 "roulette after {} bounce(s).",
                 indirect.rays_per_sample, indirect.bounces_before_roulette);

  const solve_inputs_t inputs{settings,
                              solve_settings,
                              lights,
                              bvh,
                              trace_indirect ? &traced_scene : nullptr,
                              indirect};
  const solve_outputs_t outputs{lightmap.irradiance_pages,
                                lightmap.visibility_pages,
                                trace_indirect ? &lightmap.indirect_l0_pages : nullptr,
                                trace_indirect ? &lightmap.indirect_l1_pages : nullptr,
                                out_masks};

  unsigned int worker_count = std::thread::hardware_concurrency();
  if (worker_count == 0) worker_count = 1;
  worker_count = std::min((unsigned int)packed_charts.size(), worker_count);

  std::atomic<size_t> next_chart{0};
  std::vector<std::vector<dropped_light_t>> dropped_per_worker(worker_count);

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
                  dropped_per_worker[worker_index]);

      progress.solved_texels.fetch_add(texels, std::memory_order_relaxed);
      progress.solved_charts.fetch_add(1, std::memory_order_relaxed);
      report_progress_if_it_is_time(progress);
    }
  };

  std::vector<std::thread> workers;
  workers.reserve(worker_count - 1);
  for (unsigned int i = 1; i < worker_count; ++i)
    workers.emplace_back(solve_until_done, i);

  // The calling thread is one of the workers, so a one-chart bake spawns nothing.
  solve_until_done(0);

  for (std::thread &worker : workers) worker.join();

  lightmap.probes = {};
  if (bake_probes)
  {
    const std::optional<probe_grid_t> grid =
        try_build_probe_grid(map, settings.probe_spacing_in_world_units);
    if (!grid)
      log_error("[lightmap] no probe grid could be built at spacing {}; the bake carries "
                "no probes.",
                settings.probe_spacing_in_world_units);
    else
    {
      indirect_trace_settings_t probe_settings = indirect;
      if (!trace_indirect) probe_settings.rays_per_sample = 0;
      lightmap.probes = bake_probe_volume(*grid, bvh, traced_scene, lights, probe_settings);
    }
  }

  // Always, however short: "how long does a bake take on this map" is the
  // question every setting in the panel is really about, and the progress lines
  // above deliberately say nothing on a fast one.
  // The suffix is what tells a traced bake from an untraced one AFTERWARDS. They
  // are otherwise indistinguishable from the outside -- the direct pages look the
  // same and the indirect ones simply are not there -- and a bake is long enough
  // that "did I have it on?" is a real question by the time it finishes.
  log_terminal("[lightmap] baked {} chart(s) over {} texel(s) on {} thread(s) in "
               "{:.2f}s{}.",
               packed_charts.size(), progress.total_texels, worker_count,
               (double)progress.elapsed_milliseconds() / 1000.0,
               trace_indirect
                   ? std::format(", {} indirect chain(s) per texel sample",
                                 indirect.rays_per_sample)
                   : std::string(" -- NO indirect light, \"Trace indirect light\" was off"));

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
