#pragma once

// The records the bake's SHADING is spelled in. lightmap_gpu_plan.md.
//
// The solve cuts exactly where sample_chart hands back a world position and a
// normal: everything above that line has an identity (a chart, a plane, a face)
// and stays on the CPU; everything below it shades a POINT and is what a compute
// kernel will do one thread per record. These are the buffers that cross that
// line, std430-shaped -- every vec3 sits beside a scalar -- so the CPU solve and
// the GPU one are two implementations of "shade this batch of records" and the
// batching above them is shared.

#include "entity_uid.hpp"
#include "lightmap.hpp"
#include "lightmap_solve.hpp"
#include "lightmap_trace.hpp"
#include "linalg.hpp"
#include "map.hpp"
#include "span.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace shared
{

// One (texel, sub-sample) that sample_chart put ON the surface. `seed` is
// sample_hash(atlas_x, atlas_y, page, sample_index), the one number every
// sampling decision below the solve is derived from; `chart_index` names the
// chart in the BATCH's chart table, which is what the residual dispatch's
// per-chart light mask is keyed by.
struct gpu_sample_t
{
  linalg::vec3 position{0.f, 0.f, 0.f};
  uint32_t chart_index = 0;
  linalg::vec3 normal{0.f, 0.f, 0.f};
  uint32_t seed = 0;
};
static_assert(sizeof(gpu_sample_t) == 32,
              "gpu_sample_t is the std430 struct the kernels read; a padding "
              "change here is a wrong stride there.");
static_assert(offsetof(gpu_sample_t, chart_index) == 12 && offsetof(gpu_sample_t, normal) == 16 &&
              offsetof(gpu_sample_t, seed) == 28);

// Where a record came from, so its answer can be put back. Parallel to the
// sample list and never uploaded: the GPU sees points, not texels.
struct sample_origin_t
{
  int texel_x = 0;
  int texel_y = 0;
};

// The direct term's answer per record. `coverage` and `weight` are sample-major,
// light_count per sample -- what the CPU averages into a texel exactly as the
// inside count did, and what the chart's slot ranking sums.
struct gpu_direct_results_t
{
  std::vector<linalg::vec3> irradiance;
  std::vector<float> coverage;
  std::vector<float> weight;
  size_t light_count = 0;

  void resize(size_t sample_count, size_t lights)
  {
    light_count = lights;
    irradiance.assign(sample_count, {0.f, 0.f, 0.f});
    coverage.assign(sample_count * lights, 0.f);
    weight.assign(sample_count * lights, 0.f);
  }
};

// The indirect term's answer per record: indirect_sh_l1_t, one per sample.
struct gpu_indirect_results_t
{
  std::vector<indirect_sh_l1_t> values;
};

// --- The scene, built once per bake (lightmap_gpu_plan.md step 2) -----------
//
// What the kernels trace against: the map as TRIANGLES, with a material per
// triangle resolved at build time so surface_at's brush-and-face walk never
// runs on the GPU. The material table is PARALLEL to traced_scene_t::materials
// -- index i is map material i, the same table `face_surface_t::material`
// indexes -- with ONE trailing untextured entry (`untextured_material`) for what
// surface_at answers with the untextured grey: a static mesh's triangle, and a
// face naming an index the table does not have.

// An index into gpu_bake_scene_t::textures, or this: reads as
// UNTEXTURED_BOUNCE_ALBEDO for an albedo and as BLACK for an emissive -- the two
// polarities lightmap_trace.cpp's sample_texture already gives its fallbacks.
inline constexpr uint32_t GPU_NO_TEXTURE = 0xffffffffu;

// Indexed by the primitive index a ray query reports. Corner uvs are the
// LAYER-0 material uvs the generated brush mesh draws with, so a bounce reads
// the texture the wall draws with.
struct gpu_triangle_t
{
  uint32_t material = 0;
  uint32_t pad = 0;
  linalg::vec2 uv0{0.f, 0.f};
  linalg::vec2 uv1{0.f, 0.f};
  linalg::vec2 uv2{0.f, 0.f};
};
static_assert(sizeof(gpu_triangle_t) == 32 && offsetof(gpu_triangle_t, uv0) == 8,
              "gpu_triangle_t is the std430 struct the kernels read");

struct gpu_material_t
{
  uint32_t albedo_texture = GPU_NO_TEXTURE;
  uint32_t emissive_texture = GPU_NO_TEXTURE;
  uint32_t pad0 = 0;
  uint32_t pad1 = 0;
};
static_assert(sizeof(gpu_material_t) == 16, "gpu_material_t is the std430 struct the kernels read");

struct gpu_bake_scene_t
{
  // One vertex per triangle CORNER, w unused: stride 16 serves the acceleration
  // structure build and the shader alike. Nothing is welded -- a corner three
  // faces share carries three uvs.
  std::vector<linalg::vec4> vertices;
  std::vector<uint32_t> indices;
  std::vector<gpu_triangle_t> triangles;

  std::vector<gpu_material_t> materials;
  uint32_t untextured_material = 0;

  // Every texture a material names, once, at its NATIVE size -- the asset the
  // renderer draws with and sample_texture reads, so the kernel's texelFetch and
  // the CPU read the same byte (lightmap_gpu_plan.md step 5b). Index i is the
  // descriptor the kernel indexes; the solver uploads each as its own image.
  std::vector<const assets::texture_asset_t *> textures;

  // Which map object each triangle came from. Parallel to `triangles`, never
  // uploaded: the GPU sees triangles, not brushes, and this is for a report.
  std::vector<entity_uid_t> triangle_object_uids;
};

// The union of every brush's generate_brush_mesh output and every static
// mesh's static_mesh_world_triangles, with the materials resolved through the
// TRACED scene's table -- derived from it rather than resolved a second time, so
// the two solves cannot disagree about what material i is made of. Materials
// resolve on the calling thread for the reason build_traced_scene does: a
// resolve LOADS.
//
// A brush face that emits no geometry contributes no triangle here, where it
// is still a solid's face in the occluder BVH. A closed brush is entered and
// left through SOME face either way, so a shadow ray is stopped the same and a
// bounce lands one face further in.
[[nodiscard]] gpu_bake_scene_t build_gpu_bake_scene(const map_t &map,
                                                    const traced_scene_t &traced);

// What a triangle of the scene reflects and emits at a uv -- surface_at over
// the scene's own tables through the SAME sample_texture, so a texel that asks
// both answers the same question byte for byte.
[[nodiscard]] traced_surface_t gpu_surface_at(const gpu_bake_scene_t &scene,
                                              uint32_t triangle_index,
                                              const linalg::vec2 &uv);

// The material uv at a point of a triangle: the corner uvs blended by the
// point's barycentric weights, which is what the kernel does at a hit.
[[nodiscard]] linalg::vec2 gpu_triangle_uv_at(const gpu_bake_scene_t &scene,
                                              uint32_t triangle_index,
                                              const linalg::vec3 &position);

// --- The seam (lightmap_gpu_plan.md step 3) ---------------------------------
//
// Above this line the bake has identities -- a chart, a plane, a face, a mesh.
// Below it there are RECORDS, and a solver is whatever turns a batch of them
// into a batch of answers. bake_lightmap owns the loop: it collects WHOLE charts
// into a batch, dispatches the direct term under an all-zero light mask, ranks
// each chart's slots on the CPU, dispatches the RESIDUAL for the charts that
// dropped a light with the dropped bits set, dispatches the indirect term, and
// reduces every answer into the chart scratch exactly where the reference path's
// chunk loop does. Buffers in, buffers out: shared never learns what a VkBuffer
// is, and the batching, the masks and the residual dance are testable with no
// device.

// The one block of SETTINGS every kernel reads, the twin of the uniform the GPU
// solver uploads. Translated ONCE out of the solve settings, and the tracer's
// settings are derived from THIS rather than from the solve settings a second
// time, so the direct term and the bounce cannot read two different biases.
struct gpu_bake_settings_t
{
  lightmap_solve_mode_t mode = lightmap_solve_mode_t::Direct_Light;
  int32_t rays_per_sample = 0;
  int32_t bounces_before_roulette = 2;
  int32_t max_bounces = 16;
  float ray_bias = 0.25f;
  float shadow_ray_bias = 0.25f;
  int32_t soft_shadow_samples = 8;
  float directional_shadow_distance = 100000.f;
};
static_assert(sizeof(gpu_bake_settings_t) == 32,
              "gpu_bake_settings_t is the std430 uniform block the kernels read");

[[nodiscard]] gpu_bake_settings_t
gpu_bake_settings_from(const lightmap_solve_settings_t &solve_settings);
[[nodiscard]] indirect_trace_settings_t
indirect_trace_settings_from(const gpu_bake_settings_t &settings);

// What shading COST: rays counted where the rule that spends them is applied,
// chains where they are fired. What the bake's report line carries.
struct shade_statistics_t
{
  size_t direct_rays = 0;
  size_t chains = 0;

  void add(const shade_statistics_t &other)
  {
    direct_rays += other.direct_rays;
    chains += other.chains;
  }
};

// The CPU shade of ONE record, per term: the reference every kernel is compared
// against, and what cpu_batch_solver_t runs over a batch. Neither knows what a
// texel is; the averaging is the caller's.
//
// The direct term answers three things per light -- its visibility (the
// coverage), what it delivers (the ranking weight) and, for the lights
// `irradiance_light_mask` admits, its irradiance. The mask is all zero on a first
// pass, because which lights may sum is exactly what the ranking has not decided
// yet, and the dropped lights' bits on the residual one.
void shade_sample_direct(const gpu_sample_t &sample, Span<const baked_light_t> lights,
                         const Bounding_Volume_Hierarchy &bvh,
                         const gpu_bake_settings_t &settings, uint64_t irradiance_light_mask,
                         linalg::vec3 &out_irradiance, Span<float> out_coverage,
                         Span<float> out_weight, shade_statistics_t &statistics);

// The indirect term shares the record's position, normal and seed with the
// direct one rather than walking the chart a second time, so the two terms are
// answers about the same places on the face. The geometric normal survives here
// for ONE thing, choosing which hemisphere to fire into: nothing multiplies by
// N.L, since the texel stores what arrives and the shader applies the cosine.
[[nodiscard]] indirect_sh_l1_t shade_sample_indirect(const gpu_sample_t &sample,
                                                     const traced_scene_t &scene,
                                                     Span<const baked_light_t> lights,
                                                     const indirect_trace_settings_t &settings,
                                                     shade_statistics_t &statistics);

// A chart's residual mask is one bit per light of the resolve table, so a bake
// serves at most this many baked lights. Not a new limit: the runtime's light
// array is slot-indexed over the same table and holds MAX_LIGHTS (64), so a
// 65th baked light already had no slot to be read from.
inline constexpr size_t LIGHT_MASK_BITS = 64;

// Everything a solver shades against, handed over ONCE per bake. The world has
// two spellings here on purpose: the triangle scene is what a kernel traces, the
// BVH plus the traced scene is what the CPU shade traces, and a solver reads the
// one it is written against. Both are derived from the same map in the same
// call -- the_gpu_scene_is_made_of_what_the_tracer_sees is the pin that they
// agree, and it runs with no device.
struct batch_solver_scene_t
{
  const gpu_bake_scene_t *gpu_scene = nullptr;
  const Bounding_Volume_Hierarchy *bvh = nullptr;
  const traced_scene_t *traced = nullptr;
  Span<const baked_light_t> lights;
  gpu_bake_settings_t settings;
};

struct batch_solve_statistics_t
{
  shade_statistics_t shade;
  size_t direct_dispatches = 0;
  size_t indirect_dispatches = 0;
};

struct lightmap_batch_solver_t
{
  virtual ~lightmap_batch_solver_t() = default;

  // For the report line, which says which path a bake took.
  [[nodiscard]] virtual const char *name() const = 0;

  // How many result FLOATS a batch may produce -- a record's direct answer is
  // 3 + 2 * light_count of them and its indirect answer 12. A batch is as many
  // WHOLE charts as fit, whole because a chart's ranking must see every sample
  // of it before the residual dispatch; a chart bigger than the whole budget is
  // still one batch, on its own. The solver names the number because it is the
  // solver's memory.
  [[nodiscard]] virtual size_t result_budget_in_floats() const = 0;

  virtual void upload_scene(const batch_solver_scene_t &scene) = 0;

  // `chart_light_masks` is indexed by gpu_sample_t::chart_index. The solver
  // sizes `out` itself: one answer per record, sample-major, light_count per
  // sample.
  virtual void solve_direct(Span<const gpu_sample_t> samples,
                            Span<const uint64_t> chart_light_masks,
                            gpu_direct_results_t &out) = 0;
  virtual void solve_indirect(Span<const gpu_sample_t> samples,
                              gpu_indirect_results_t &out) = 0;

  [[nodiscard]] virtual batch_solve_statistics_t statistics() const = 0;
};

// The CPU shade behind the seam: the two shade functions above over a batch,
// the records cut into slices across the hardware threads -- a record's answer
// depends on nothing but the record, so where a slice boundary falls moves no
// result. In shared rather than in the test because it is what a GPU solver is
// COMPARED against: the same batch loop under two solvers isolates the shading
// from everything around it.
struct cpu_batch_solver_t final : lightmap_batch_solver_t
{
  // Public so a test can force a batch boundary between two charts by lowering
  // it. 64 MB of result floats; the GPU solver's budget is its device's.
  size_t result_budget = (size_t)1 << 24;

  // Zero is every hardware thread.
  unsigned int worker_count = 0;

  [[nodiscard]] const char *name() const override { return "the CPU batch solver"; }
  [[nodiscard]] size_t result_budget_in_floats() const override { return result_budget; }
  void upload_scene(const batch_solver_scene_t &scene) override;
  void solve_direct(Span<const gpu_sample_t> samples, Span<const uint64_t> chart_light_masks,
                    gpu_direct_results_t &out) override;
  void solve_indirect(Span<const gpu_sample_t> samples, gpu_indirect_results_t &out) override;
  [[nodiscard]] batch_solve_statistics_t statistics() const override { return accumulated; }

private:
  batch_solver_scene_t scene;
  indirect_trace_settings_t indirect;
  batch_solve_statistics_t accumulated;
};

// Every record the bake would shade, over every packed chart in order, with
// chart_index the chart's position in the packed list -- `charts[chart_index]`
// is its index into lightmap.charts -- and `origins` parallel to `samples`, so
// an answer can be put back on its texel. What the ray query pin and the
// indirect comparison fire from, so both probe exactly the points a bake
// shades. The BVH is the occluder set a sample is tested against for being
// buried inside a solid, which is what excludes it -- the same BVH the bake's
// own collection uses.
struct lightmap_sample_set_t
{
  std::vector<gpu_sample_t> samples;
  std::vector<sample_origin_t> origins;
  std::vector<size_t> charts;
};

[[nodiscard]] lightmap_sample_set_t
collect_lightmap_sample_set(const lightmap_t &lightmap,
                            const lightmap_solve_settings_t &solve_settings,
                            const Bounding_Volume_Hierarchy &bvh);

[[nodiscard]] std::vector<gpu_sample_t>
collect_lightmap_samples(const lightmap_t &lightmap,
                         const lightmap_solve_settings_t &solve_settings,
                         const Bounding_Volume_Hierarchy &bvh);

// --- Step 4's pin: a ray that hits something ---------------------------------
//
// One ray per record, from position + normal * ray_bias along the normal, no
// further than max_distance: the hit distance, or -1 for a miss. This is the
// CPU half, through the same occluder BVH the bake's shadow rays walk; the
// Vulkan solver's probe_rays answers the same rays through its TLAS, and
// compare_probe_rays says where the two differ. It is where a wrong stride, a
// wrong winding or an unbuilt acceleration structure shows up as a NUMBER
// rather than as a dark bake three steps later.
[[nodiscard]] std::vector<float> probe_ray_distances(const Bounding_Volume_Hierarchy &bvh,
                                                     Span<const gpu_sample_t> samples,
                                                     float ray_bias, float max_distance);

// The two structures answer ONE ray differently by construction: the BVH holds
// SOLIDS and reports an origin inside one as a hit at distance zero, where the
// TLAS holds SURFACES and lets that ray fly through the interior to the far
// wall. A blockout buries faces inside neighbouring brushes routinely, so those
// rays are counted as their own category rather than as disagreements, and the
// distance statistics are over the rest.
struct probe_ray_report_t
{
  size_t sample_count = 0;
  size_t both_hit = 0;
  size_t both_missed = 0;
  size_t reference_only_hit = 0;
  size_t candidate_only_hit = 0;
  size_t reference_started_inside_a_solid = 0;
  size_t hits_outside_tolerance = 0;
  size_t candidate_farther = 0;
  size_t candidate_nearer = 0;
  float largest_distance_error = 0.f;
  float mean_distance_error = 0.f;
  // The record with the largest error, or -1 when every compared hit agreed.
  int64_t worst_sample = -1;

  [[nodiscard]] bool agrees() const
  {
    return reference_only_hit == 0 && candidate_only_hit == 0 && hits_outside_tolerance == 0;
  }
};

// `distance_tolerance` is absolute, in world units; a hit's error is the
// difference of the two distances. Fatal on lists of different length -- they
// came from one sample list, so that is a caller bug and not a disagreement.
[[nodiscard]] probe_ray_report_t compare_probe_rays(Span<const float> reference,
                                                    Span<const float> candidate,
                                                    float distance_tolerance);

// --- The pins of steps 5 and 6: a kernel against the CPU shade -----------------
//
// The two solves draw the same random numbers from the same seeds, so they agree
// far tighter than "in expectation" -- but a ray landing on the other side of an
// edge by float rounding changes one chain's whole answer, or one spiral ray's,
// so never bit for bit. The comparison is therefore a PAIRED test per chart: the
// mean of (candidate - reference) over the chart's records, per coefficient,
// against the standard error of that mean. A difference beyond
// RECORD_COMPARISON_SIGMA standard errors is a bug; inside it is the estimator.
// A systematic error -- a wrong albedo, a missing PI, a wrong falloff -- moves
// every record the same way and fails by miles; a handful of edge rays moves a
// few records and passes.
//
// FIVE sigma, not three, because a map is thousands of tests: 300 charts by 12
// coefficients at three sigma flags ten charts a bake by chance alone (the first
// press flagged three, largest 3.3, signs both ways). At five the expected false
// positive count per bake is a few thousandths, and a real error is tens of
// sigma on most charts anyway.
//
// A record's answer is a list of COEFFICIENTS, and which they are is the term's:
// the indirect term's twelve are indirect_sh_l1_t in order, the direct term's are
// the irradiance rgb then a coverage and a weight per light. Each coefficient
// belongs to a SCALE GROUP, and a mean difference below
// RECORD_COMPARISON_RELATIVE_FLOOR of the group's mean magnitude is float noise
// whatever its standard error says -- a chart of one record, or a chart whose
// every record differs by one ulp, has a standard error of zero and would
// otherwise fail on nothing. Groups exist because a coverage is a fraction and
// an irradiance is in radiance units, and one floor cannot serve both.

inline constexpr size_t SH_L1_COEFFICIENT_COUNT = 12;
inline constexpr float RECORD_COMPARISON_SIGMA = 5.f;
inline constexpr float RECORD_COMPARISON_RELATIVE_FLOOR = 1e-3f;

struct record_chart_comparison_t
{
  size_t chart = 0;
  size_t record_count = 0;
  std::vector<float> reference_mean;
  std::vector<float> candidate_mean;
  std::vector<float> difference_standard_error;
  // max over coefficients of |mean difference| / standard error, and which one.
  float largest_sigma = 0.f;
  int largest_sigma_coefficient = -1;
};

struct record_comparison_report_t
{
  size_t record_count = 0;
  size_t chart_count = 0;
  size_t coefficient_count = 0;
  size_t charts_beyond_tolerance = 0;
  // Records whose reference answer is not all zero, and records where the two
  // answers differ in ANY coefficient by any amount: "0 charts flagged" over a
  // map where nothing is lit, or where the two agree bit for bit, reads the
  // same as a real agreement, and these two counts tell the three apart.
  size_t reference_nonzero_records = 0;
  size_t differing_records = 0;
  // Per coefficient, over every record: the mean of the reference, the mean of
  // |candidate - reference| and the largest |candidate - reference|.
  std::vector<float> reference_mean;
  std::vector<float> mean_absolute_difference;
  std::vector<float> largest_absolute_difference;
  // Per scale group, the mean |reference| its floor is relative to.
  std::vector<float> group_scale;
  // Every chart with at least one record, worst first; among charts at the
  // same sigma the BRIGHTEST first, so a report that agrees everywhere shows
  // the charts whose numbers mean something rather than its unlit faces.
  std::vector<record_chart_comparison_t> charts;

  [[nodiscard]] bool agrees() const { return charts_beyond_tolerance == 0; }

  // The two per-coefficient means averaged over a run of coefficients -- the
  // indirect report's "L0 mean" is the first three.
  [[nodiscard]] float reference_mean_over(size_t first, size_t count) const;
  [[nodiscard]] float mean_absolute_difference_over(size_t first, size_t count) const;
  [[nodiscard]] float largest_absolute_difference_over(size_t first, size_t count) const;
};

// `charts` is lightmap_sample_set_t::charts: what each chart_index names. Fatal
// on lists of different length -- they came from one sample list.
//
// Coefficients in indirect_sh_l1_t's order: l0 rgb, then l1[0], l1[1], l1[2]
// rgb each. Two scale groups: L0 and L1, because L1 is signed and smaller.
[[nodiscard]] record_comparison_report_t
compare_indirect_results(Span<const gpu_sample_t> samples, Span<const size_t> charts,
                         Span<const indirect_sh_l1_t> reference,
                         Span<const indirect_sh_l1_t> candidate);

// Coefficients: irradiance rgb, then coverage per light, then weight per light
// -- 3 + 2 * light_count, and direct_coefficient_name says which is which.
// Three scale groups, one per kind. Fatal on two results over different light
// counts.
[[nodiscard]] record_comparison_report_t
compare_direct_results(Span<const gpu_sample_t> samples, Span<const size_t> charts,
                       const gpu_direct_results_t &reference,
                       const gpu_direct_results_t &candidate);

inline constexpr size_t DIRECT_COEFFICIENT_NAME_CAPACITY = 32;

// "irradiance.r", "coverage[3]", "weight[3]" into caller storage; fatal on a
// coefficient the light count does not have. Returns what it wrote.
[[nodiscard]] std::string_view direct_coefficient_name(size_t coefficient, size_t light_count,
                                                       Span<char> storage);

// The per-texel mean of one value per record over the records that fell on it,
// at the record's atlas texel, into Rgb9e5 pages. No gutter and no ranking: the
// picture of what ONE solver answered, so two of them can be looked at side by
// side and their difference written the way the debug pages already are. The
// indirect comparison hands it the L0s, the direct one the irradiances.
[[nodiscard]] lightmap_pages_t reduce_record_values_to_pages(const lightmap_t &lightmap,
                                                             const lightmap_sample_set_t &set,
                                                             Span<const linalg::vec3> values);

// |a - b| per texel, into pages shaped like `a`. Fatal on pages of a different
// shape.
[[nodiscard]] lightmap_pages_t absolute_difference_pages(const lightmap_pages_t &a,
                                                         const lightmap_pages_t &b);

} // namespace shared
