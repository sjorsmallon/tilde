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
#include "lightmap_trace.hpp"
#include "linalg.hpp"
#include "map.hpp"

#include <cstddef>
#include <cstdint>
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

// A layer index into one of the two texture arrays, or this: reads as
// UNTEXTURED_BOUNCE_ALBEDO for an albedo and as BLACK for an emissive -- the two
// polarities lightmap_trace.cpp's sample_texture already gives its fallbacks.
inline constexpr uint32_t GPU_NO_LAYER = 0xffffffffu;

// Every texture is resampled to one fixed layer size so the two arrays need no
// descriptor indexing. A bounce integrates hundreds of samples over a surface,
// so what the resample loses is averaged away exactly as the nearest fetch is.
inline constexpr int GPU_BAKE_TEXTURE_LAYER_SIZE = 256;

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
  uint32_t albedo_layer = GPU_NO_LAYER;
  uint32_t emissive_layer = GPU_NO_LAYER;
  uint32_t pad0 = 0;
  uint32_t pad1 = 0;
};
static_assert(sizeof(gpu_material_t) == 16, "gpu_material_t is the std430 struct the kernels read");

// RGBA8, sRGB-ENCODED, layer-major, every layer GPU_BAKE_TEXTURE_LAYER_SIZE
// square: the bytes of an sRGB-format sampler2DArray, so the sampler decodes
// exactly as srgb_byte_to_linear does on the CPU.
struct gpu_texture_array_t
{
  std::vector<uint8_t> pixels;

  [[nodiscard]] size_t layer_count() const
  {
    return pixels.size() /
           ((size_t)GPU_BAKE_TEXTURE_LAYER_SIZE * (size_t)GPU_BAKE_TEXTURE_LAYER_SIZE * 4);
  }
};

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

  gpu_texture_array_t albedo;
  gpu_texture_array_t emissive;

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

// LINEAR reflectance back to the sRGB-encoded byte the texture arrays hold;
// srgb_byte_to_linear's inverse, and a byte survives the round trip exactly.
[[nodiscard]] uint8_t linear_to_srgb_byte(float linear);

// The CPU twin of the kernel's fetch: nearest and wrapped over the resampled
// layer, decoded, with the caller's fallback for GPU_NO_LAYER. What the pin
// compares against surface_at.
[[nodiscard]] linalg::vec3 sample_gpu_texture(const gpu_texture_array_t &array, uint32_t layer,
                                              const linalg::vec2 &uv,
                                              const linalg::vec3 &fallback);

// What a triangle of the scene reflects and emits at a uv -- surface_at over
// the scene's own tables, so a texel that asks both answers the same question.
[[nodiscard]] traced_surface_t gpu_surface_at(const gpu_bake_scene_t &scene,
                                              uint32_t triangle_index,
                                              const linalg::vec2 &uv);

// The material uv at a point of a triangle: the corner uvs blended by the
// point's barycentric weights, which is what the kernel does at a hit.
[[nodiscard]] linalg::vec2 gpu_triangle_uv_at(const gpu_bake_scene_t &scene,
                                              uint32_t triangle_index,
                                              const linalg::vec3 &position);

} // namespace shared
