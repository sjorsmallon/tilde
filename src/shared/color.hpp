#pragma once

#include <cstdint>

#include "linalg.hpp"

// Canonical color type for the whole engine: one byte per channel, RGBA.
//
// This is the ONLY representation call sites should ever construct or pass
// around. The byte order the GPU and ImGui actually want (ABGR) is a detail of
// the renderer boundary -- it is produced by to_abgr() inside renderer.cpp and
// nowhere else. If you find yourself writing a packed hex literal like
// 0xFF00FFFF in a call site, that is the bug this type exists to remove.
struct color_t
{
  uint8_t r = 0;
  uint8_t g = 0;
  uint8_t b = 0;
  uint8_t a = 255;
};

// Pack into 0xAABBGGRR (ABGR byte order, red in the low byte). This is the
// layout the debug-draw shaders and ImGui expect. Keep its use confined to the
// renderer boundary.
inline constexpr uint32_t to_abgr(color_t color)
{
  return (uint32_t(color.a) << 24) | (uint32_t(color.b) << 16) |
         (uint32_t(color.g) << 8) | uint32_t(color.r);
}

// Inverse of to_abgr(). Only needed when an external library hands us a packed
// value (e.g. Jolt's debug renderer).
inline constexpr color_t color_from_abgr(uint32_t packed)
{
  return color_t{uint8_t(packed & 0xFF), uint8_t((packed >> 8) & 0xFF),
                 uint8_t((packed >> 16) & 0xFF), uint8_t((packed >> 24) & 0xFF)};
}

// Return a copy of color with its alpha replaced.
inline constexpr color_t with_alpha(color_t color, uint8_t alpha)
{
  color.a = alpha;
  return color;
}

inline constexpr uint8_t color_channel_from_float(float value)
{
  if (value <= 0.0f)
    return 0;
  if (value >= 1.0f)
    return 255;
  return uint8_t(value * 255.0f + 0.5f);
}

// Float interop (0..1 per channel). Lights, materials and particles store
// colors as vec3f/vec4f; these bridge them to the canonical byte form.
inline color_t color_from_vec3(const linalg::vec3f &rgb, uint8_t alpha = 255)
{
  return color_t{color_channel_from_float(rgb.r),
                 color_channel_from_float(rgb.g),
                 color_channel_from_float(rgb.b), alpha};
}

inline color_t color_from_vec4(const linalg::vec4f &rgba)
{
  return color_t{color_channel_from_float(rgba.r),
                 color_channel_from_float(rgba.g),
                 color_channel_from_float(rgba.b),
                 color_channel_from_float(rgba.a)};
}

inline linalg::vec4f color_to_vec4(color_t color)
{
  return linalg::vec4f{color.r / 255.0f, color.g / 255.0f, color.b / 255.0f,
                       color.a / 255.0f};
}

// Named constants. Use these instead of packed literals.
namespace colors
{
inline constexpr color_t white{255, 255, 255};
inline constexpr color_t black{0, 0, 0};
inline constexpr color_t red{255, 0, 0};
inline constexpr color_t green{0, 255, 0};
inline constexpr color_t blue{0, 0, 255};
inline constexpr color_t yellow{255, 255, 0};
inline constexpr color_t cyan{0, 255, 255};
inline constexpr color_t magenta{255, 0, 255};
inline constexpr color_t orange{255, 165, 0};
inline constexpr color_t gold{255, 204, 0};
inline constexpr color_t pink{255, 0, 136};
inline constexpr color_t hot_pink{255, 0, 203};
inline constexpr color_t grey{68, 68, 68};
} // namespace colors
