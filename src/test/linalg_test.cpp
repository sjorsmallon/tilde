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

int main() {
  test_vec3();
  test_vec2();
  test_mat4();
  test_math();

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
