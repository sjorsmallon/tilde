#pragma once

// Gate 6 step 3: the split-sum environment BRDF table (Karis 2013), the scale
// and bias applied to F0 per (N.V, roughness). Built once at startup; it
// depends on no map and no art.

#include "linalg.hpp"

#include <cstdint>
#include <vector>

namespace shared
{

struct environment_brdf_lut_t
{
  int size = 0;
  std::vector<uint16_t> scale_bias;

  [[nodiscard]] bool empty() const { return scale_bias.empty(); }
  [[nodiscard]] linalg::vec2 load(int normal_dot_view_texel, int roughness_texel) const;
};

[[nodiscard]] environment_brdf_lut_t build_environment_brdf_lut(int size = 64,
                                                                int sample_count = 512);

} // namespace shared
