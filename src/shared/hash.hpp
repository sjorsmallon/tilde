#pragma once

#include <cstdint>

namespace shared
{

// Randomness that is DERIVED, never drawn from shared/rng.hpp's global state: the same key gives the same
// number on every thread, every run and every side. FNV-1a over value's four bytes; the bake's GLSL ports it verbatim.
[[nodiscard]] constexpr uint32_t hash_mix(uint32_t hash, uint32_t value)
{
  for (int byte = 0; byte < 4; ++byte)
  {
    hash ^= (value >> (byte * 8)) & 0xffu;
    hash *= 16777619u;
  }
  return hash;
}

// A uniform float in [0, 1) out of a hash.
[[nodiscard]] constexpr float unit_float_from(uint32_t bits)
{
  return (float)(bits >> 8) * (1.f / 16777216.f);
}

} // namespace shared
