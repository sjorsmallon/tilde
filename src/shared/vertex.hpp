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

// How many material layers a blended surface composes, and the ONE place that
// number is written down. geometry_def.md ss4 argues two on cost grounds rather
// than on principle, so this is the door: the weights below are stored for
// layers 1..N-1 and layer 0's is implied as 1 - their sum, which is the general
// scheme evaluated at N = 2. Raising it widens vertex_blend_t, the face's
// stored weights and the blend shader's loop, and moves no call site.
inline constexpr int BLEND_LAYER_COUNT = 2;

// Per-vertex blend weights, a PARALLEL array to mesh_asset_t::vertices for the
// same reason skin influences are (skeleton.hpp argues it): Vulkan takes it as
// its own vertex binding, so blending is an added pipeline rather than an edit
// to the vertex every mesh in the engine already uses.
struct vertex_blend_t
{
  float weight[BLEND_LAYER_COUNT - 1] = {};
};
