#include "../client/camera.hpp"
#include <cmath>
#include <cstdio>
#include <iostream>

static client::camera_t camera_looking_along(float x, float y, float z)
{
  client::camera_t cam;
  client::look_at(cam, {x, y, z});
  return cam;
}

void test_from_view_vector() {
  // Test Case 1: Looking +X
  // Yaw should be 0, Pitch 0
  {
    client::camera_t cam =
        camera_looking_along(1.0f, 0, 0);
    printf("Test +X: Yaw=%f (Expected 0), Pitch=%f (Expected 0)\n", cam.yaw,
           cam.pitch);
    if (std::abs(cam.yaw) > 1e-4f || std::abs(cam.pitch) > 1e-4f) {
      printf("FAIL\n");
      exit(1);
    }
  }

  // Test Case 2: Looking +Z
  // Yaw should be 90 (or -90 depends on coordinate system logic in atan2(z,x))
  // atan2(1, 0) = PI/2 = 90 deg.
  {
    client::camera_t cam =
        camera_looking_along(0, 0, 1.0f);
    printf("Test +Z: Yaw=%f (Expected 90), Pitch=%f (Expected 0)\n", cam.yaw,
           cam.pitch);
    if (std::abs(cam.yaw - 90.0f) > 1e-4f) {
      printf("FAIL\n");
      exit(1);
    }
  }

  // Test Case 3: Looking +Y (Up)
  // Pitch should be 90.
  {
    client::camera_t cam = camera_looking_along(0.0001f, 1.0f, 0.0001f); // approximate Up
    printf("Test +Y: Pitch=%f (Expected ~90)\n", cam.pitch);
    if (std::abs(cam.pitch - 90.0f) >
        1.0f) { // Allow some slack due to rounding
      printf("FAIL\n");
      exit(1);
    }
  }

  printf("PASS\n");
}

int main() {
  test_from_view_vector();
  return 0;
}
