#pragma once

#include <cstdint>

namespace game {

// Simple, fast, deterministic RNG (PCG-like or SplitMix64 variant)
// We rely on a global/thread-local state for simplicity as requested.
// This allows free functions `random_float()` without passing state around.

namespace detail {
inline uint64_t &get_global_rng_state() {
  // Initial seed default.
  static uint64_t state = 0xCAFEBABE;
  return state;
}
} // namespace detail

// Seeds the global RNG state.
inline void seed_rng(uint64_t seed) { detail::get_global_rng_state() = seed; }

// Gets the current internal state (for snapshots).
inline uint64_t get_rng_state() { return detail::get_global_rng_state(); }

// Sets the current internal state (from snapshots).
inline void set_rng_state(uint64_t state) {
  detail::get_global_rng_state() = state;
}

// Generates the next random uint64 in the sequence.
inline uint64_t random_uint64() {
  uint64_t x = detail::get_global_rng_state();
  // SplitMix64/PCG-like step
  x ^= x >> 30;
  x *= 0xbf58476d1ce4e5b9ULL;
  x ^= x >> 27;
  x *= 0x94d049bb133111ebULL;
  x ^= x >> 31;
  detail::get_global_rng_state() = x + 0x9e3779b97f4a7c15ULL; // different step
  // Actually, a standard LCG or SplitMix is better for simple state evolution.
  // Let's use a standard SplitMix64 style state update + output mix.
  //
  // Actually simpler:
  // State update: s = s * 6364136223846793005ULL + 1442695040888963407ULL;
  // Output: xorshifted state
  //
  // Let's stick to a very simple LCG for state update, then mix for output.
  // This is what PCG does basically.

  uint64_t old_state = detail::get_global_rng_state();
  // LCG update
  detail::get_global_rng_state() =
      old_state * 6364136223846793005ULL + 1442695040888963407ULL;

  // Output function (XSH-RR) - simplified
  uint64_t word = ((old_state >> 18u) ^ old_state) >> 27u;
  uint32_t rot = old_state >> 59u;
  return (word >> rot) | (word << ((-rot) & 31));
}

// Generates a deterministic float in [0.0, 1.0].
inline float random_float() {
  // Generate 24 bits of randomness for mantissa
  // (float has 23 bits mantissa, but we can just map uint32 to float via
  // division) Standard canonical integer to float conversion: (random_uint >>
  // (64 - 24)) * (1.0f / (1 << 24)) Or simpler: generate uint32, divide by
  // MAX_UINT32.

  // Implementation using standard idiom for [0, 1) or [0, 1]
  // Let's use the uint32 -> float conversion.
  uint32_t val = (uint32_t)random_uint64();
  return (float)val / (float)0xFFFFFFFF;
}

} // namespace game
