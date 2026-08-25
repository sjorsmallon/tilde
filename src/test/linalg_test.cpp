#include "shared/linalg.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <type_traits>

using namespace linalg;

void test_vec3() {
  // Aggregate initialization (braced-init-list)
  vec3 v1 = {1.0f, 2.0f, 3.0f};
  assert(v1.x == 1.0f);
  assert(v1.y == 2.0f);
  assert(v1.z == 3.0f);
  assert(v1.r == 1.0f); // punning check
  assert(v1[0] == 1.0f);

  // Designated initializers check (C++20 feature, but with anonymous structs
  // inside unions it might rely on extensions) GCC/Clang often support this.
  // Let's try.
  vec3 v2 = {.x = 4.0f, .y = 5.0f, .z = 6.0f};
  assert(v2.x == 4.0f);
  assert(v2.y == 5.0f);
  assert(v2.z == 6.0f);

  vec3 v3 = v1 + v2;
  assert(v3.x == 5.0f);
  assert(v3.y == 7.0f);
  assert(v3.z == 9.0f);

  float d = dot(v1, v2);
  // 1*4 + 2*5 + 3*6 = 4 + 10 + 18 = 32
  assert(std::abs(d - 32.0f) < 1e-6f);

  vec3 c = cross(vec3{1, 0, 0}, vec3{0, 1, 0});
  assert(c.z == 1.0f);

  std::cout << "test_vec3 passed" << std::endl;
}

void test_mat4() {
  mat4f id = mat4f::identity();
  vec4 v = {1, 2, 3, 1};
  vec4 res = id * v;
  assert(res.x == 1.0f);
  assert(res.y == 2.0f);
  assert(res.z == 3.0f);
  assert(res.w == 1.0f);

  // Translation matrix
  mat4f t = mat4f::identity();
  t[3] = {10, 20, 30, 1};

  // Check multiply
  vec4 p = {0, 0, 0, 1};
  vec4 p_prime = t * p;
  assert(p_prime.x == 10.0f);
  assert(p_prime.y == 20.0f);
  assert(p_prime.z == 30.0f);

  std::cout << "test_mat4 passed" << std::endl;
}

void test_vec2() {
  vec2 v1 = {1.0f, 2.0f};
  assert(v1.x == 1.0f);
  assert(v1.y == 2.0f);
  assert(v1.u == 1.0f); // punning check

  vec2 v2 = {.x = 3.0f, .y = 4.0f};
  assert(v2.x == 3.0f);
  assert(v2.y == 4.0f);

  std::cout << "test_vec2 passed" << std::endl;
}

void test_math() {
  float deg = 180.0f;
  float rad = to_radians(deg);
  assert(std::abs(rad - PI) < 1e-5f);

  float val = mix(0.0f, 10.0f, 0.5f);
  assert(std::abs(val - 5.0f) < 1e-5f);

  vec3 a = {0, 0, 0};
  vec3 b = {10, 10, 10};
  vec3 c = mix(a, b, 0.5f);
  assert(c.x == 5.0f);

  std::cout << "test_math passed" << std::endl;
}

// The Vulkan conventions the projections bake in, checked rather than trusted:
// clip Z spans [0, 1] (not OpenGL's [-1, 1]) and clip Y points down.
void test_projection_matrices() {
  constexpr float NEAR_PLANE = 1.0f;
  constexpr float FAR_PLANE  = 100.0f;

  const mat4f proj = perspective(to_radians(90.0f), 16.0f / 9.0f, NEAR_PLANE, FAR_PLANE);

  // A point ON the near plane lands at depth 0, one on the far plane at depth 1.
  // Looking down -Z, so the plane at distance d sits at z = -d.
  const vec4 at_near = proj * vec4{0, 0, -NEAR_PLANE, 1};
  const vec4 at_far  = proj * vec4{0, 0, -FAR_PLANE, 1};
  assert(std::abs(at_near.z / at_near.w - 0.0f) < 1e-4f);
  assert(std::abs(at_far.z / at_far.w - 1.0f) < 1e-4f);

  // World +Y must come out as clip -Y.
  const vec4 above = proj * vec4{0, 1, -NEAR_PLANE, 1};
  assert(above.y / above.w < 0.0f);

  const mat4f ortho = orthographic(-10, 10, -5, 5, NEAR_PLANE, FAR_PLANE);
  const vec4 ortho_near = ortho * vec4{0, 0, -NEAR_PLANE, 1};
  const vec4 ortho_far  = ortho * vec4{0, 0, -FAR_PLANE, 1};
  assert(std::abs(ortho_near.z - 0.0f) < 1e-4f);
  assert(std::abs(ortho_far.z - 1.0f) < 1e-4f);

  std::cout << "test_projection_matrices passed" << std::endl;
}

void test_look_at() {
  const vec3f eye    = {5, 3, -2};
  const vec3f target = {0, 3, -2};

  const mat4f view = look_at(eye, target, {0, 1, 0});

  // The eye maps to the view-space origin, and the target sits straight ahead
  // down -Z at exactly its world distance.
  const vec4 eye_in_view = view * vec4{eye.x, eye.y, eye.z, 1};
  assert(length(vec3{eye_in_view.x, eye_in_view.y, eye_in_view.z}) < 1e-4f);

  const vec4 target_in_view = view * vec4{target.x, target.y, target.z, 1};
  assert(std::abs(target_in_view.x) < 1e-4f);
  assert(std::abs(target_in_view.y) < 1e-4f);
  assert(std::abs(target_in_view.z + 5.0f) < 1e-4f);

  // Composed with a projection, a point on the view axis is at the NDC origin.
  const mat4f view_projection = perspective(to_radians(90.0f), 1.0f, 1.0f, 100.0f) * view;
  const vec4  clip            = view_projection * vec4{target.x, target.y, target.z, 1};
  assert(std::abs(clip.x / clip.w) < 1e-4f);
  assert(std::abs(clip.y / clip.w) < 1e-4f);

  std::cout << "test_look_at passed" << std::endl;
}

void test_compose_transform_euler() {
  const vec3f translation = {7, -2, 13};
  const vec3f scale       = {2, 3, 4};

  // With no rotation the euler form must agree with the quaternion form exactly.
  const mat4f from_euler = compose_transform_euler(translation, {0, 0, 0}, scale);
  const mat4f from_quat  = compose_transform(translation, quatf{0, 0, 0, 1}, scale);
  for (int column = 0; column < 4; ++column)
    for (int row = 0; row < 4; ++row)
      assert(std::abs(from_euler[column][row] - from_quat[column][row]) < 1e-5f);

  // 90 degrees about Y sends +X to -Z (yaw sweeps +X toward +Z, so the inverse
  // rotation of the basis vector goes the other way).
  const mat4f yawed = compose_transform_euler({0, 0, 0}, {0, 90, 0}, {1, 1, 1});
  const vec4  x_axis = yawed * vec4{1, 0, 0, 0};
  assert(std::abs(x_axis.x) < 1e-5f);
  assert(std::abs(x_axis.z + 1.0f) < 1e-5f);

  // Translation must survive scaling untouched.
  const vec4 origin = from_euler * vec4{0, 0, 0, 1};
  assert(std::abs(origin.x - translation.x) < 1e-5f);
  assert(std::abs(origin.y - translation.y) < 1e-5f);
  assert(std::abs(origin.z - translation.z) < 1e-5f);

  std::cout << "test_compose_transform_euler passed" << std::endl;
}

// Ray-to-segment distance, checked against a brute-force scan of the segment
// rather than against hand-worked expectations.
//
// The reason it is checked that way: the closed form is a sign error away from
// a function that still returns plausible small numbers, and the editor gizmo
// shipped with exactly that. A negated segment parameter clamps to 0, so every
// query answered with the distance to the segment's START -- which reads as a
// handle whose grabbable part is much shorter than the arrow drawn for it, and
// as nothing at all when read.
void test_ray_segment_distance() {
  auto brute_force = [](const vec3 &origin, const vec3 &direction, const vec3 &start,
                        const vec3 &end) {
    float best = 1e30f;
    const int samples = 4000;
    for (int i = 0; i <= samples; ++i) {
      const vec3 point = start + (end - start) * ((float)i / (float)samples);
      float ray_parameter = dot(point - origin, direction) / dot(direction, direction);
      ray_parameter = std::max(0.0f, ray_parameter);
      best = std::min(best, length(point - (origin + direction * ray_parameter)));
    }
    return best;
  };

  // A ray straight down onto the middle of a segment: the answer is zero, and
  // it is the case the sign error got wrong by half the segment's length.
  assert(distance_from_ray_to_segment({0.5f, 10.f, 0.f}, {0.f, -1.f, 0.f}, {0.f, 0.f, 0.f},
                                      {1.f, 0.f, 0.f}) < 1e-5f);

  // The far end must answer like the near end. This is the gizmo symptom.
  assert(distance_from_ray_to_segment({0.95f, 10.f, 0.f}, {0.f, -1.f, 0.f}, {0.f, 0.f, 0.f},
                                      {1.f, 0.f, 0.f}) < 1e-5f);

  // Past the end, the segment stops: the answer is the distance to the endpoint.
  assert(std::abs(distance_from_ray_to_segment({3.f, 0.f, 0.f}, {0.f, -1.f, 0.f},
                                               {0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}) -
                  2.0f) < 1e-5f);

  // Parallel: no unique closest pair, and the constant separation is the answer.
  assert(std::abs(distance_from_ray_to_segment({0.f, 4.f, 0.f}, {1.f, 0.f, 0.f},
                                               {0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}) -
                  4.0f) < 1e-5f);

  // Degenerate segment: a point, and the ray still has a distance to it.
  assert(std::abs(distance_from_ray_to_segment({0.f, 0.f, 0.f}, {1.f, 0.f, 0.f},
                                               {5.f, 3.f, 0.f}, {5.f, 3.f, 0.f}) -
                  3.0f) < 1e-5f);

  // The whole domain, including the rays whose closest approach is BEHIND the
  // eye -- the case a single clamping pass gets wrong.
  uint32_t state = 1u;
  auto next_float = [&state](float low, float high) {
    state = state * 1664525u + 1013904223u;
    return low + (high - low) * ((float)((state >> 8) & 0xFFFFFF) / (float)0xFFFFFF);
  };

  for (int trial = 0; trial < 400; ++trial) {
    const vec3 origin = {next_float(-50.f, 50.f), next_float(-50.f, 50.f),
                         next_float(-50.f, 50.f)};
    const vec3 raw = {next_float(-1.f, 1.f), next_float(-1.f, 1.f), next_float(-1.f, 1.f)};
    if (length(raw) < 1e-3f)
      continue;
    const vec3 direction = normalize(raw);
    const vec3 start = {next_float(-10.f, 10.f), next_float(-10.f, 10.f),
                        next_float(-10.f, 10.f)};
    const vec3 end = start + vec3{next_float(-10.f, 10.f), next_float(-10.f, 10.f),
                                  next_float(-10.f, 10.f)};

    const float reference = brute_force(origin, direction, start, end);
    const float measured = distance_from_ray_to_segment(origin, direction, start, end);
    assert(std::abs(measured - reference) < 1e-3f);
  }

  std::cout << "ray-segment distance passed." << std::endl;
}

int main() {
  test_vec3();
  test_vec2();
  test_mat4();
  test_math();
  test_projection_matrices();
  test_look_at();
  test_compose_transform_euler();
  test_ray_segment_distance();

  // Size checks
  static_assert(sizeof(vec3) == 3 * sizeof(float), "vec3 size mismatch");
  static_assert(sizeof(vec4) == 4 * sizeof(float), "vec4 size mismatch");
  static_assert(sizeof(mat4f) == 16 * sizeof(float), "mat4f size mismatch");

  // Triviality checks
  static_assert(std::is_trivially_constructible_v<vec3>,
                "vec3 not trivially constructible");
  static_assert(std::is_trivially_copyable_v<vec3>,
                "vec3 not trivially copyable");
  static_assert(std::is_aggregate_v<vec3>, "vec3 is not an aggregate");

  std::cout << "All linalg tests passed!" << std::endl;
  return 0;
}
