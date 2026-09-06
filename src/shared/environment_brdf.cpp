#include "environment_brdf.hpp"

#include "log.hpp"

#include <algorithm>
#include <cmath>

namespace shared
{

namespace
{

constexpr float PI = 3.14159265359f;

[[nodiscard]] float radical_inverse(uint32_t bits)
{
  bits = (bits << 16u) | (bits >> 16u);
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
  return (float)bits * 2.3283064365386963e-10f;
}

[[nodiscard]] float visibility_smith_ggx_correlated(float n_dot_v, float n_dot_l, float alpha)
{
  const float alpha_squared = alpha * alpha;
  const float lambda_v = n_dot_l * std::sqrt(n_dot_v * n_dot_v * (1.f - alpha_squared) + alpha_squared);
  const float lambda_l = n_dot_v * std::sqrt(n_dot_l * n_dot_l * (1.f - alpha_squared) + alpha_squared);
  return 0.5f / (lambda_v + lambda_l);
}

} // namespace

linalg::vec2 environment_brdf_lut_t::load(int normal_dot_view_texel, int roughness_texel) const
{
  if (normal_dot_view_texel < 0 || normal_dot_view_texel >= size || roughness_texel < 0 ||
      roughness_texel >= size)
    fatal_error("[environment-brdf] texel ({}, {}) of a {}x{} table", normal_dot_view_texel,
                roughness_texel, size, size);
  const size_t at = ((size_t)roughness_texel * (size_t)size + (size_t)normal_dot_view_texel) * 2;
  return {(float)scale_bias[at] / 65535.f, (float)scale_bias[at + 1] / 65535.f};
}

environment_brdf_lut_t build_environment_brdf_lut(int size, int sample_count)
{
  if (size <= 0 || sample_count <= 0)
    fatal_error("[environment-brdf] a {}x{} table over {} sample(s)", size, size, sample_count);

  environment_brdf_lut_t lut;
  lut.size = size;
  lut.scale_bias.resize((size_t)size * (size_t)size * 2);

  for (int roughness_texel = 0; roughness_texel < size; ++roughness_texel)
  {
    const float roughness = ((float)roughness_texel + 0.5f) / (float)size;
    const float alpha = std::max(roughness, 0.001f);
    const float alpha_squared = alpha * alpha;

    for (int view_texel = 0; view_texel < size; ++view_texel)
    {
      const float n_dot_v = ((float)view_texel + 0.5f) / (float)size;
      const linalg::vec3 view{std::sqrt(std::max(0.f, 1.f - n_dot_v * n_dot_v)), 0.f, n_dot_v};

      float scale = 0.f;
      float bias = 0.f;
      for (int i = 0; i < sample_count; ++i)
      {
        const float xi_x = ((float)i + 0.5f) / (float)sample_count;
        const float xi_y = radical_inverse((uint32_t)i);
        const float phi = 2.f * PI * xi_x;
        const float cos_theta =
            std::sqrt((1.f - xi_y) / (1.f + (alpha_squared - 1.f) * xi_y));
        const float sin_theta = std::sqrt(std::max(0.f, 1.f - cos_theta * cos_theta));
        const linalg::vec3 half{sin_theta * std::cos(phi), sin_theta * std::sin(phi), cos_theta};

        const float v_dot_h = linalg::dot(view, half);
        const linalg::vec3 light = half * (2.f * v_dot_h) - view;

        const float n_dot_l = std::max(light.z, 0.f);
        const float n_dot_h = std::max(half.z, 0.f);
        const float clamped_v_dot_h = std::max(v_dot_h, 0.f);
        if (n_dot_l <= 0.f || n_dot_h <= 0.f) continue;

        const float term = 4.f * visibility_smith_ggx_correlated(n_dot_v, n_dot_l, alpha) *
                           n_dot_l * clamped_v_dot_h / n_dot_h;
        const float fresnel_curve = std::pow(1.f - clamped_v_dot_h, 5.f);
        scale += (1.f - fresnel_curve) * term;
        bias += fresnel_curve * term;
      }
      scale /= (float)sample_count;
      bias /= (float)sample_count;

      const size_t at = ((size_t)roughness_texel * (size_t)size + (size_t)view_texel) * 2;
      lut.scale_bias[at] = (uint16_t)std::lround(std::clamp(scale, 0.f, 1.f) * 65535.f);
      lut.scale_bias[at + 1] = (uint16_t)std::lround(std::clamp(bias, 0.f, 1.f) * 65535.f);
    }
  }
  return lut;
}

} // namespace shared
