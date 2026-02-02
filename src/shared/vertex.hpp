#pragma once

#include "linalg.hpp"

using namespace linalg;

struct vertex_xnc
{
  vec3f position;
  vec3f normal;
  vec3f color;
};

struct vertex_xnu
{
  vec3f position;
  vec3f normal;
  vec2f uv;
};

struct vertex_xnuu
{
  vec3f position;
  vec3f normal;
  vec2f uv;
  vec2f atlas_uv;
};
