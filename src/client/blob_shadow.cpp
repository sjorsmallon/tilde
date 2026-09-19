#include "blob_shadow.hpp"

#include "../shared/asset_types.hpp"
#include "../shared/log.hpp"

#include <algorithm>
#include <cmath>

namespace client
{

namespace
{

constexpr int32_t BLOB_TEXTURE_SIZE      = 64;
constexpr float   RAY_START_ABOVE_FEET   = 2.0f;
constexpr float   LIFT_OFF_SURFACE       = 0.25f;

struct blob_shadow_resources_t
{
  renderer::mesh_handle_t     mesh;
  renderer::material_handle_t material;
};

assets::texture_asset_t make_blob_texture()
{
  assets::texture_asset_t texture{};
  texture.width    = BLOB_TEXTURE_SIZE;
  texture.height   = BLOB_TEXTURE_SIZE;
  texture.channels = 4;
  texture.alpha    = assets::alpha_mode_t::blend;
  texture.pixels.resize((size_t)BLOB_TEXTURE_SIZE * BLOB_TEXTURE_SIZE * 4);

  for (int32_t y = 0; y < BLOB_TEXTURE_SIZE; ++y)
    for (int32_t x = 0; x < BLOB_TEXTURE_SIZE; ++x)
    {
      const float u        = ((float)x + 0.5f) / BLOB_TEXTURE_SIZE * 2.0f - 1.0f;
      const float v        = ((float)y + 0.5f) / BLOB_TEXTURE_SIZE * 2.0f - 1.0f;
      const float distance = std::clamp(std::sqrt(u * u + v * v), 0.0f, 1.0f);
      const float falloff  = 1.0f - distance * distance * (3.0f - 2.0f * distance);

      uint8_t* pixel = &texture.pixels[((size_t)y * BLOB_TEXTURE_SIZE + x) * 4];
      pixel[0] = 0;
      pixel[1] = 0;
      pixel[2] = 0;
      pixel[3] = (uint8_t)std::lround(falloff * 255.0f);
    }
  return texture;
}

assets::mesh_asset_t make_unit_quad()
{
  assets::mesh_asset_t quad{};
  quad.vertices = {
      {{-1.f, 0.f, -1.f}, {0.f, 1.f, 0.f}, {0.f, 0.f}},
      {{ 1.f, 0.f, -1.f}, {0.f, 1.f, 0.f}, {1.f, 0.f}},
      {{ 1.f, 0.f,  1.f}, {0.f, 1.f, 0.f}, {1.f, 1.f}},
      {{-1.f, 0.f,  1.f}, {0.f, 1.f, 0.f}, {0.f, 1.f}},
  };
  quad.indices = {0, 2, 1, 0, 3, 2};
  return quad;
}

const blob_shadow_resources_t& blob_shadow_resources()
{
  static const blob_shadow_resources_t resources = []
  {
    blob_shadow_resources_t result{};
    result.mesh = renderer::register_mesh(make_unit_quad());

    renderer::material_t material{};
    material.pipeline_state = {.shader      = renderer::shader_t::unlit,
                               .blend_mode  = renderer::blend_mode_t::alpha,
                               .cull_mode   = renderer::cull_mode_t::none,
                               .depth_test  = true,
                               .depth_write = false};
    material.parameters.maps.albedo = renderer::register_texture(make_blob_texture(), true);
    result.material = renderer::register_material(material);
    return result;
  }();
  return resources;
}

} // namespace

void draw_blob_shadow(pass_builder_t& scene, const Bounding_Volume_Hierarchy& bvh,
                      Span<const uint8_t> disabled_geometry, const vec3f& feet,
                      const blob_shadow_settings_t& settings)
{
  if (settings.radius <= 0.0f || settings.opacity <= 0.0f || settings.max_distance <= 0.0f)
    return;

  const vec3f      origin = feet + vec3f{0.f, RAY_START_ABOVE_FEET, 0.f};
  ray_hit_result_t hit{};
  if (!bvh_intersect_ray(bvh, origin, {0.f, -1.f, 0.f}, hit, disabled_geometry) || !hit.hit ||
      hit.t < 0.0f)
    return;

  const float drop = hit.t - RAY_START_ABOVE_FEET;
  if (drop > settings.max_distance)
    return;

  const float closeness = 1.0f - std::clamp(drop / settings.max_distance, 0.0f, 1.0f);
  const float radius    = settings.radius * (0.5f + 0.5f * closeness);
  const float opacity   = std::clamp(settings.opacity * closeness, 0.0f, 1.0f);

  const vec3f normal    = linalg::normalize(hit.normal);
  const vec3f reference = std::abs(normal.x) < 0.9f ? vec3f{1.f, 0.f, 0.f} : vec3f{0.f, 0.f, 1.f};
  const vec3f tangent   = linalg::normalize(linalg::cross(reference, normal));
  const vec3f bitangent = linalg::cross(normal, tangent);
  const vec3f center    = origin + vec3f{0.f, -hit.t, 0.f} + normal * LIFT_OFF_SURFACE;

  linalg::mat4f transform = linalg::mat4f::identity();
  transform[0] = {tangent.x * radius, tangent.y * radius, tangent.z * radius, 0.f};
  transform[1] = {normal.x, normal.y, normal.z, 0.f};
  transform[2] = {bitangent.x * radius, bitangent.y * radius, bitangent.z * radius, 0.f};
  transform[3] = {center.x, center.y, center.z, 1.f};

  const blob_shadow_resources_t& resources = blob_shadow_resources();
  if (!resources.mesh.valid() || !resources.material.valid())
  {
    log_error("draw_blob_shadow: the blob quad or its material failed to register");
    return;
  }

  renderer::mesh_draw_t draw{};
  draw.mesh               = resources.mesh;
  draw.transform          = transform;
  draw.material_overrides = {&resources.material, 1};
  draw.tint               = with_alpha(colors::white, (uint8_t)std::lround(opacity * 255.0f));
  draw.shadow_caster      = renderer::shadow_caster_t::none;
  scene.meshes.push_back(draw);
}

} // namespace client
