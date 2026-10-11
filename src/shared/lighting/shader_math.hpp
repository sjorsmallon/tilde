#pragma once

// The C++ half of lighting_def.md's decision I: the shared lighting maths is ONE
// TEXT, compiled twice, rather than two copies pinned by a test. A test can
// evaluate the C++ side across a table of sample points and catch an edit to the
// C++; it is blind to an edit to the GLSL, which is the copy more likely to be
// edited by whoever is already looking at a shader.
//
// resources/shaders/light_falloff.glsl is included BELOW, inside this namespace,
// with GLSL's scalar builtins defined onto their C++ equivalents first. That is
// the whole shim -- it is small because the shared file is deliberately
// scalar-only, and the moment something in it wants a vec3 or a sampler it
// belongs in pbr_lighting.glsl instead, which nothing here reads.
//
// glslc gets the same file through `-I resources/shaders`; the C++ side gets it
// through the same directory on game_shared's include path, and the compiler's
// own header dependency tracking is what CMake's DEPENDS list is doing for the
// SPIR-V. Editing light_falloff.glsl rebuilds both.

namespace shared::shader_math
{

inline float max(float a, float b)
{
  return a > b ? a : b;
}

inline float clamp(float value, float low, float high)
{
  return max(low, value < high ? value : high);
}

#define INLINE inline
#include "light_falloff.glsl"
#undef INLINE

} // namespace shared::shader_math
