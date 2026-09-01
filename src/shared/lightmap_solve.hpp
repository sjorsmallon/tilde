#pragma once

// The solve: charts and an atlas in, pixels out. ONE path, and the two things it
// can produce are a MODE of it rather than two implementations -- see
// lightmap_solve_mode_t below for why that split and not the other one.

#include "lightmap.hpp"
#include "map.hpp"

#include <string>
#include <vector>

namespace shared
{

enum class lightmap_solve_mode_t : uint32_t
{
  // Sum of every light's radiance * attenuation * N.L. What ships.
  Direct_Light,
  // no falloff, just texel -> worldspace -> light
  Visibility,
};

struct lightmap_solve_settings_t
{
  lightmap_solve_mode_t mode = lightmap_solve_mode_t::Direct_Light;
  float shadow_ray_bias = 0.25f;
  float directional_shadow_distance = 100000.f;

  // N, of an NxN stratified grid of samples inside each texel's footprint -- so 1
  // is the one-sample-at-the-centre solve and costs exactly what it used to. A
  // texel is an AREA and a shadow edge crossing it is a coverage fraction; one
  // sample can only answer yes or no, which is what makes a hard shadow
  // stair-step along the texel grid.
  int samples_per_texel_edge = 2;

  // Whether the pass that fills a chart's gutter from its own covered texels
  // runs. It is not a quality knob -- without it the gutter stays at the zero it
  // was allocated with and bilinear filtering pulls black in at every chart edge,
  // which is a dark seam on every face boundary in the level. It is a setting so
  // a debug image can be looked at with the coverage still visible.
  bool dilate_into_the_gutter = true;
};

// Per-light shadow-ray coverage: for every light the bake used, the fraction of a
// texel's samples that light's ray reached. The DEBUG view of what the bake
// stores, and it differs in the one way that makes it worth having -- it carries
// EVERY light, where a chart's visibility pages carry the four it kept, so this
// is the only picture a dropped light appears in. Float, and in no sidecar.
//
// Gated on range and the cone, and deliberately NOT on N.L against the flat face
// plane. The runtime shades with a normal-mapped normal, which at a grazing angle
// faces a light the geometric normal does not; a flat-plane N.L frozen in here
// kills exactly those texels, and does it invisibly.
struct lightmap_visibility_masks_t
{
  int size_in_texels = 0;
  int page_count = 0;

  // One per slot, naming the light entity that slot's coverage is of -- the same
  // pairing the sidecar's resolve table will store. scene_light_t deliberately
  // does not carry a uid: the other two folds have no use for one yet.
  std::vector<entity_uid_t> light_uids;

  // Slot-major, so one slot's pages are contiguous and writing one as an image
  // needs no stride.
  std::vector<float> coverage;

  [[nodiscard]] bool empty() const { return coverage.empty(); }
  [[nodiscard]] size_t slot_count() const { return light_uids.size(); }
  [[nodiscard]] size_t index_of(size_t slot, int page, int x, int y) const;

  void allocate(const lightmap_atlas_t &atlas, std::vector<entity_uid_t> uids);
};

// The solve, over a lightmap whose charts are already built and PACKED --
// `charts`, `atlas` and `settings` are the input, and everything a pixel decides
// is the output: the irradiance pages, the visibility pages, the resolve table,
// and each chart's light slots. One call rather than four because no two of
// those mean anything apart -- a slot index is nonsense without the table it
// indexes, and a visibility channel is nonsense without the slot that names it.
//
// `out_masks` is the optional DEBUG output, the deserialize_entity pattern: null
// asks for none. It carries every light rather than the four a chart kept, which
// is what makes it the view that shows a light being dropped.
void bake_lightmap(const map_t &map, lightmap_t &lightmap,
                   const lightmap_solve_settings_t &solve_settings,
                   lightmap_visibility_masks_t *out_masks = nullptr);

[[nodiscard]] bool try_write_lightmap_pages_png(const lightmap_pages_t &pages,
                                                const std::string &path_prefix,
                                                float exposure = 1.f);

// One RGBA PNG per page of the STORED visibility, "<path_prefix>_page<N>.png" --
// one slot per channel, in each chart's own slot order.
[[nodiscard]] bool
try_write_lightmap_visibility_pages_png(const lightmap_pages_t &pages,
                                        const std::string &path_prefix);

// One PNG per light per page, "<path_prefix>_light<uid>_page<N>.png". Coverage is
// a FRACTION and not a colour, so it is written straight to a byte with no tone
// map and no sRGB encode: half the samples arriving reads as 128.
[[nodiscard]] bool
try_write_lightmap_visibility_png(const lightmap_visibility_masks_t &masks,
                                  const std::string &path_prefix);

} // namespace shared
