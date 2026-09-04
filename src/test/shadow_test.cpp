// Gate 9, the one piece of shadow-map maths that lives outside the renderer:
// spot_shadow_projection (shared/lighting.hpp). A spot light's frustum IS its
// cone, so the pin is geometric -- the cone's edge lands on the map's edge, the
// range lands on depth 1, and the texel size is the cone's width over the
// resolution. The receiver-side sampling reads the same matrix back out of the
// scene block, so a matrix that passes this is one the shader agrees with.

#include "../shared/lighting.hpp"

#include <cmath>
#include <cstdio>

using linalg::vec3f;
using linalg::vec4;

static int failures = 0;

static void check(bool condition, const char* what)
{
  if (!condition)
  {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

static vec3f project(const linalg::mat4f& matrix, const vec3f& point)
{
  const vec4 clip = matrix * vec4{point.x, point.y, point.z, 1.f};
  return {clip.x / clip.w, clip.y / clip.w, clip.z / clip.w};
}

static bool near(float a, float b, float tolerance = 1e-3f)
{
  return std::abs(a - b) <= tolerance;
}

int main()
{
  constexpr float OUTER_DEGREES = 35.f;
  constexpr uint32_t RESOLUTION = 1024;

  shared::scene_light_t light;
  light.kind      = shared::light_kind_t::Spot;
  light.position  = {10.f, 50.f, -20.f};
  light.forward   = linalg::normalize(vec3f{1.f, -1.f, 0.f});
  light.range     = 512.f;
  light.cos_inner = std::cos(linalg::to_radians(20.f));
  light.cos_outer = std::cos(linalg::to_radians(OUTER_DEGREES));

  const shared::shadow_projection_t projection = shared::spot_shadow_projection(light, RESOLUTION);
  const linalg::mat4f&              matrix     = projection.view_projection;

  // Along the axis: the range is the far plane, the near plane is depth 0.
  const vec3f at_range = project(matrix, light.position + light.forward * light.range);
  check(near(at_range.x, 0.f) && near(at_range.y, 0.f), "the cone axis projects to the map centre");
  check(near(at_range.z, 1.f, 1e-4f), "the range projects to depth 1");

  const vec3f at_near = project(matrix, light.position + light.forward * shared::SHADOW_NEAR_PLANE);
  check(near(at_near.z, 0.f, 1e-4f), "the near plane projects to depth 0");

  // Across the axis: a point exactly on the cone's edge lands on the map's edge,
  // whichever world direction the light's frame put it in.
  const vec3f up    = {0.f, 1.f, 0.f};
  const vec3f right = linalg::normalize(linalg::cross(light.forward, up));
  const float half  = linalg::to_radians(OUTER_DEGREES);
  const vec3f edge_direction = light.forward * std::cos(half) + right * std::sin(half);
  const vec3f at_edge        = project(matrix, light.position + edge_direction * 200.f);
  check(near(std::abs(at_edge.x), 1.f) && near(at_edge.y, 0.f),
        "the cone's edge projects to the map's edge");
  check(at_edge.z > 0.f && at_edge.z < 1.f, "a point inside the range has a depth inside (0, 1)");

  const vec3f inside_direction = light.forward * std::cos(half * 0.5f) + right * std::sin(half * 0.5f);
  const vec3f at_inside        = project(matrix, light.position + inside_direction * 200.f);
  check(std::abs(at_inside.x) < 1.f, "a point inside the cone projects inside the map");

  const vec3f outside_direction = light.forward * std::cos(half * 1.2f) + right * std::sin(half * 1.2f);
  const vec3f at_outside        = project(matrix, light.position + outside_direction * 200.f);
  check(std::abs(at_outside.x) > 1.f, "a point outside the cone projects outside the map");

  // The texel size: the map spans 2 * tan(half) at unit distance.
  check(near(projection.texel_size_at_unit_distance, 2.f * std::tan(half) / (float)RESOLUTION, 1e-6f),
        "texel size is the cone's width at unit distance over the resolution");

  // A cone wider than a perspective map can hold is clamped, not exploded.
  light.cos_outer = std::cos(linalg::to_radians(120.f));
  const shared::shadow_projection_t clamped = shared::spot_shadow_projection(light, RESOLUTION);
  check(std::isfinite(clamped.texel_size_at_unit_distance) &&
            clamped.texel_size_at_unit_distance < 2.f * std::tan(linalg::to_radians(86.f)) / (float)RESOLUTION,
        "a hemisphere cone clamps to the widest map a perspective can hold");

  // A light aimed straight down, where world up is parallel to the cone axis,
  // still gets a frame.
  light.cos_outer = std::cos(linalg::to_radians(OUTER_DEGREES));
  light.forward   = {0.f, -1.f, 0.f};
  const shared::shadow_projection_t down    = shared::spot_shadow_projection(light, RESOLUTION);
  const vec3f                       at_down = project(down.view_projection, light.position + light.forward * 100.f);
  check(near(at_down.x, 0.f) && near(at_down.y, 0.f) && std::isfinite(at_down.z),
        "a straight-down spot light projects its axis to the map centre");

  if (failures == 0)
    std::printf("shadow_test: all checks passed\n");
  return failures == 0 ? 0 : 1;
}
