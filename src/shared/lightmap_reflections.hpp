#pragma once

// Gate 6 steps 1 and 2, lighting_def.md decision L: the reflection capture set
// and its traced cubes.

#include "collision_detection.hpp"
#include "lightmap.hpp"
#include "lightmap_gpu.hpp"
#include "lightmap_lights.hpp"
#include "lightmap_trace.hpp"
#include "map.hpp"
#include "span.hpp"

#include <cstdint>
#include <vector>

namespace shared
{

struct reflection_capture_settings_t
{
  float spacing_in_world_units = 512.f;
  float open_face_extent = 100000.f;
};

[[nodiscard]] reflection_capture_set_t
build_reflection_captures(const map_t &map, const probe_grid_t &grid,
                          Span<const uint8_t> inside,
                          const Bounding_Volume_Hierarchy &occluders,
                          const reflection_capture_settings_t &settings);

// One axis ray per face, on purpose and after trying the alternative: a fan
// over a cone with a percentile reach measures a ROOM correctly and sees past
// a prop to the wall, but on a map with no walls it opens every face to
// infinity, and an open box places nothing -- the reflections of the block and
// the cube vanished. One ray ends the box at whatever it hits, which places
// that one thing correctly by accident; past it the shader reads uncorrected,
// which for an object near the capture is nearly right. What places objects
// rather than walls is a distance per capture texel, the next step; the box
// only has to bound its march.
[[nodiscard]] aabb_bounds_t measure_reflection_box(const Bounding_Volume_Hierarchy &occluders,
                                                   const linalg::vec3 &position,
                                                   float open_face_extent,
                                                   uint8_t &out_open_faces);

// Trilinear over the eight corners of the lattice cell `point` is in, the
// weights normalised over the corners that hold a capture; a point outside the
// lattice clamps to its nearest face. The shader's pick_reflection_captures is
// this arithmetic exactly.
[[nodiscard]] reflection_capture_pick_t
find_captures_for(const reflection_capture_set_t &set, const reflection_lattice_t &lattice,
                  const linalg::vec3 &point);

// Step 6, the inspector's question about a Reflection_Volume_Entity: how many
// captures its CURRENT bounds cover, and how many of those the set already
// carries with exactly that box -- the two differ when the volume moved or
// resized since the bake ran.
struct reflection_volume_coverage_t
{
  size_t covered = 0;
  size_t overridden_as_placed = 0;
};
[[nodiscard]] reflection_volume_coverage_t
reflection_volume_coverage_of(const reflection_capture_set_t &set, const aabb_bounds_t &bounds);

// One record per (capture, face, texel): `normal` the texel's direction,
// `chart_index` capture-major, `seed` sample_hash(capture, face, texel, tag).
[[nodiscard]] std::vector<gpu_sample_t>
collect_capture_samples(const reflection_capture_set_t &set, int size_in_texels);

[[nodiscard]] inline size_t capture_index_of_record(uint32_t chart_index, int size_in_texels)
{
  return chart_index / ((size_t)REFLECTION_CUBE_FACE_COUNT * (size_t)size_in_texels *
                        (size_t)size_in_texels);
}

// Shades mip 0 of every capture, then prefilters each chain.
void bake_reflection_captures(reflection_capture_set_t &set, const traced_scene_t &scene,
                              Span<const baked_light_t> lights,
                              const indirect_trace_settings_t &settings,
                              int size_in_texels, lightmap_batch_solver_t *solver = nullptr);

// Step 3, split-sum (Karis 2013): fills mips 1.. of `cube` from its mip 0 by
// importance-sampling the GGX lobe at roughness m / (mip_count - 1), each
// sample fetched from the box-averaged level its solid angle implies. Mip 0
// is untouched.
inline constexpr int REFLECTION_PREFILTER_SAMPLES = 64;
void prefilter_reflection_cube(reflection_cube_t &cube);

} // namespace shared
