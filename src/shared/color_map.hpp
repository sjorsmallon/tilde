#pragma once

// A colour map is its stops, evenly spaced over 0..1; between two stops the colour is a blend of both.

#include "array.hpp"
#include "linalg.hpp"

#include <algorithm>
#include <cassert>

namespace shared
{

inline constexpr Array<linalg::vec3f, 7> RAINBOW_COLORS = {{{1.f, 0.1f, 0.1f},
                                                            {1.f, 0.5f, 0.f},
                                                            {1.f, 0.9f, 0.1f},
                                                            {0.1f, 0.85f, 0.2f},
                                                            {0.1f, 0.35f, 1.f},
                                                            {0.3f, 0.1f, 0.8f},
                                                            {0.65f, 0.2f, 1.f}}};

[[nodiscard]] inline linalg::vec3f sample_color_map(Span<const linalg::vec3f> stops, float fraction)
{
  assert(!stops.empty());
  const uint32_t last   = stops.size() - 1;
  const float    scaled = std::clamp(fraction, 0.f, 1.f) * static_cast<float>(last);
  const uint32_t lower  = std::min(static_cast<uint32_t>(scaled), last);
  const uint32_t upper  = std::min(lower + 1, last);
  return linalg::mix(stops[lower], stops[upper], scaled - static_cast<float>(lower));
}

} // namespace shared
