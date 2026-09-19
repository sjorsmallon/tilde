#pragma once

#include "../shared/collision_detection.hpp"
#include "../shared/span.hpp"
#include "frame_builder.hpp"

namespace client
{

struct blob_shadow_settings_t
{
  float radius       = 20.0f;
  float opacity      = 0.6f;
  float max_distance = 1024.0f;
};

void draw_blob_shadow(pass_builder_t& scene, const Bounding_Volume_Hierarchy& bvh,
                      Span<const uint8_t> disabled_geometry, const vec3f& feet,
                      const blob_shadow_settings_t& settings);

} // namespace client
