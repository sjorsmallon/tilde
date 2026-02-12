#pragma once

#include "shared/linalg.hpp"

namespace components {

struct position_t {
  linalg::vec3 value;
};

struct orientation_t {
  linalg::vec4 value; // quaternion
};

} // namespace components
