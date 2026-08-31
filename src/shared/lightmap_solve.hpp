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

[[nodiscard]] lightmap_pages_t bake_lightmap_pages(
    const map_t &map, const std::vector<lightmap_chart_t> &charts,
    const lightmap_atlas_t &atlas, const lightmap_bake_settings_t &settings,
    const lightmap_solve_settings_t &solve_settings);

[[nodiscard]] bool try_write_lightmap_pages_png(const lightmap_pages_t &pages,
                                                const std::string &path_prefix,
                                                float exposure = 1.f);

} // namespace shared
