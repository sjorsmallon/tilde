/*
 * Copyright (c) 2025 Rune Skovbo Johansen
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

// A C++ port of Dither3DTextureMaker.cs from https://github.com/runevision/Dither3D.

#include "dither3d_pattern.hpp"

#include "log.hpp"

#include <algorithm>
#include <cmath>

namespace shared
{

namespace
{

constexpr float PI           = 3.14159265359f;
constexpr int   BUCKET_COUNT = 256;

struct bayer_point_t
{
  float x = 0.f;
  float y = 0.f;
};

[[nodiscard]] float wrapped_offset(float offset)
{
  const float shifted = offset + 0.5f;
  return shifted - std::floor(shifted) - 0.5f;
}

} // namespace

dither3d_pattern_t build_dither3d_pattern(int bayer_recursion)
{
  if (bayer_recursion < 0 || bayer_recursion > DITHER3D_MAX_BAYER_RECURSION)
    fatal_error("[dither3d] a Bayer recursion of {} is outside 0 to {}", bayer_recursion,
                DITHER3D_MAX_BAYER_RECURSION);

  std::vector<bayer_point_t> bayer_points = {{0.f, 0.f}, {0.5f, 0.5f}, {0.5f, 0.f}, {0.f, 0.5f}};
  for (int recursion = 0; recursion < bayer_recursion - 1; ++recursion)
  {
    const size_t count  = bayer_points.size();
    const float  offset = std::pow(0.5f, (float)(recursion + 1));
    for (size_t quadrant = 1; quadrant < 4; ++quadrant)
      for (size_t at = 0; at < count; ++at)
      {
        const bayer_point_t point{bayer_points[at].x + bayer_points[quadrant].x * offset,
                                  bayer_points[at].y + bayer_points[quadrant].y * offset};
        bayer_points.push_back(point);
      }
  }

  const int dots_per_side = 1 << bayer_recursion;

  dither3d_pattern_t result;
  result.layers = dots_per_side * dots_per_side;
  result.size   = 16 * dots_per_side;
  result.pattern.resize((size_t)result.size * (size_t)result.size * (size_t)result.layers);

  std::vector<int> brightness_buckets(BUCKET_COUNT, 0);

  // Layer z holds the first z + 1 dots, so each layer only measures the one dot the layer below lacks.
  std::vector<float> nearest_squared((size_t)result.size * (size_t)result.size, INFINITY);

  const float inverse_size = 1.f / (float)result.size;
  for (int z = 0; z < result.layers; ++z)
  {
    const int           dot_count  = z + 1;
    const float         dot_area   = 0.5f / (float)dot_count;
    const float         dot_radius = std::sqrt(dot_area / PI);
    const bayer_point_t new_dot    = bayer_points[(size_t)z];

    for (int y = 0; y < result.size; ++y)
      for (int x = 0; x < result.size; ++x)
      {
        const float offset_x = wrapped_offset(((float)x + 0.5f) * inverse_size - new_dot.x);
        const float offset_y = wrapped_offset(((float)y + 0.5f) * inverse_size - new_dot.y);

        float& nearest = nearest_squared[(size_t)x + (size_t)result.size * (size_t)y];
        nearest        = std::min(nearest, offset_x * offset_x + offset_y * offset_y);

        const float distance = std::sqrt(nearest) / (dot_radius * 2.4f);
        const float value    = std::clamp(1.f - distance, 0.f, 1.f);

        const size_t at = (size_t)x + (size_t)result.size * ((size_t)y + (size_t)result.size * (size_t)z);
        result.pattern[at] = (uint8_t)std::lround(value * 255.f);

        const int bucket = std::clamp((int)(value * (float)BUCKET_COUNT), 0, BUCKET_COUNT - 1);
        ++brightness_buckets[(size_t)bucket];
      }
  }

  std::vector<float> brightness_ramp((size_t)BUCKET_COUNT + 1, 0.f);
  const size_t       pixel_count = result.pattern.size();
  size_t             sum         = 0;
  for (int bucket = 0; bucket < BUCKET_COUNT; ++bucket)
  {
    sum += (size_t)brightness_buckets[(size_t)(BUCKET_COUNT - 1 - bucket)];
    brightness_ramp[(size_t)bucket + 1] = (float)sum / (float)pixel_count;
  }

  // The original never raises the lower end of this interpolation above 0, and the ramps it ships were made that way.
  result.ramp.resize((size_t)result.size);
  int   higher_index            = 1;
  float higher_index_brightness = brightness_ramp[1];
  for (int at = 0; at < result.size; ++at)
  {
    const float desired_brightness = (float)at / (float)(result.size - 1);
    while (higher_index_brightness < desired_brightness && higher_index < BUCKET_COUNT)
    {
      ++higher_index;
      higher_index_brightness = brightness_ramp[(size_t)higher_index];
    }
    const float along = higher_index_brightness > 0.f
                            ? std::clamp(desired_brightness / higher_index_brightness, 0.f, 1.f)
                            : 0.f;
    const float lookup = ((float)(higher_index - 1) + along) / (float)BUCKET_COUNT;
    result.ramp[(size_t)at] = (uint8_t)std::lround(std::clamp(lookup, 0.f, 1.f) * 255.f);
  }

  return result;
}

} // namespace shared
