#pragma once

#include <cstdint>
#include <vector>

namespace shared
{

struct dither3d_pattern_t
{
  int                  size   = 0;
  int                  layers = 0;
  std::vector<uint8_t> pattern;
  std::vector<uint8_t> ramp;
};

constexpr int DITHER3D_MAX_BAYER_RECURSION = 3;

[[nodiscard]] dither3d_pattern_t build_dither3d_pattern(int bayer_recursion = DITHER3D_MAX_BAYER_RECURSION);

} // namespace shared
