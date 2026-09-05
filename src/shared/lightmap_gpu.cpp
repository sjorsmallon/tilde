#include "lightmap_gpu.hpp"

#include "asset.hpp"
#include "log.hpp"
#include "map_geometry.hpp"

#include <algorithm>
#include <cmath>

namespace shared
{

namespace
{

constexpr size_t LAYER_SIZE = (size_t)GPU_BAKE_TEXTURE_LAYER_SIZE;
constexpr size_t LAYER_BYTES = LAYER_SIZE * LAYER_SIZE * 4;

// Box-filtered in LINEAR space and re-encoded: averaging encoded bytes darkens
// every edge between a bright and a dark texel. A source smaller than a layer
// replicates, so a 1x1 texture resamples to exactly its one byte per channel.
void append_resampled_layer(gpu_texture_array_t &array, const assets::texture_asset_t &texture)
{
  const size_t first = array.pixels.size();
  array.pixels.resize(first + LAYER_BYTES, 255);

  const auto source_range = [](size_t destination, size_t source_size, size_t &begin,
                               size_t &end) {
    begin = destination * source_size / LAYER_SIZE;
    end = std::max(begin + 1, ((destination + 1) * source_size + LAYER_SIZE - 1) / LAYER_SIZE);
    end = std::min(end, source_size);
  };

  const size_t width = (size_t)texture.width;
  const size_t height = (size_t)texture.height;
  const size_t channels = (size_t)texture.channels;

  for (size_t y = 0; y < LAYER_SIZE; ++y)
  {
    size_t source_y_begin = 0;
    size_t source_y_end = 0;
    source_range(y, height, source_y_begin, source_y_end);

    for (size_t x = 0; x < LAYER_SIZE; ++x)
    {
      size_t source_x_begin = 0;
      size_t source_x_end = 0;
      source_range(x, width, source_x_begin, source_x_end);

      linalg::vec3 sum{0.f, 0.f, 0.f};
      float count = 0.f;
      for (size_t source_y = source_y_begin; source_y < source_y_end; ++source_y)
        for (size_t source_x = source_x_begin; source_x < source_x_end; ++source_x)
        {
          const size_t at = (source_y * width + source_x) * channels;
          sum = sum + linalg::vec3{srgb_byte_to_linear(texture.pixels[at]),
                                   srgb_byte_to_linear(texture.pixels[at + 1]),
                                   srgb_byte_to_linear(texture.pixels[at + 2])};
          count += 1.f;
        }
      if (count > 0.f) sum = sum * (1.f / count);

      uint8_t *out = &array.pixels[first + (y * LAYER_SIZE + x) * 4];
      out[0] = linear_to_srgb_byte(sum.x);
      out[1] = linear_to_srgb_byte(sum.y);
      out[2] = linear_to_srgb_byte(sum.z);
      out[3] = 255;
    }
  }
}

// The same usability test sample_texture applies before it reads a pixel: a
// texture failing it is the fallback colour there and GPU_NO_LAYER here.
bool texture_is_usable(const assets::texture_asset_t *texture)
{
  return texture && texture->width > 0 && texture->height > 0 && texture->channels >= 3 &&
         texture->pixels.size() >=
             (size_t)texture->width * (size_t)texture->height * (size_t)texture->channels;
}

struct layer_table_t
{
  std::vector<const assets::texture_asset_t *> sources;
  gpu_texture_array_t *array = nullptr;

  uint32_t layer_for(const assets::texture_asset_t *texture)
  {
    if (!texture_is_usable(texture)) return GPU_NO_LAYER;
    for (size_t layer = 0; layer < sources.size(); ++layer)
      if (sources[layer] == texture) return (uint32_t)layer;
    sources.push_back(texture);
    append_resampled_layer(*array, *texture);
    return (uint32_t)(sources.size() - 1);
  }
};

void append_triangle(gpu_bake_scene_t &scene, entity_uid_t uid, const linalg::vec3 &a,
                     const linalg::vec3 &b, const linalg::vec3 &c, const linalg::vec2 &uv_a,
                     const linalg::vec2 &uv_b, const linalg::vec2 &uv_c, uint32_t material)
{
  const uint32_t base = (uint32_t)scene.vertices.size();
  scene.vertices.push_back({a.x, a.y, a.z, 0.f});
  scene.vertices.push_back({b.x, b.y, b.z, 0.f});
  scene.vertices.push_back({c.x, c.y, c.z, 0.f});
  scene.indices.push_back(base);
  scene.indices.push_back(base + 1);
  scene.indices.push_back(base + 2);

  gpu_triangle_t triangle;
  triangle.material = material;
  triangle.uv0 = uv_a;
  triangle.uv1 = uv_b;
  triangle.uv2 = uv_c;
  scene.triangles.push_back(triangle);
  scene.triangle_object_uids.push_back(uid);
}

void append_brush(gpu_bake_scene_t &scene, const map_t &map, entity_uid_t uid,
                  const brush_geometry_t &brush)
{
  const assets::mesh_asset_t mesh = generate_brush_mesh(brush, map.materials);

  // Keyed by PLANE, the face's identity, through the same find_face_surface the
  // tracer's surface_at asks at a hit -- so the material a triangle carries is
  // the one the CPU would have resolved there. A brush whose face_surfaces are
  // empty answers with material 0, the map default, exactly as surface_at does.
  const face_surface_t brush_default;

  for (size_t first = 0; first + 2 < mesh.indices.size(); first += 3)
  {
    const vertex_xnu &a = mesh.vertices[mesh.indices[first]];
    const vertex_xnu &b = mesh.vertices[mesh.indices[first + 1]];
    const vertex_xnu &c = mesh.vertices[mesh.indices[first + 2]];

    linalg::vec3 normal = linalg::cross(b.position - a.position, c.position - a.position);
    const float length = linalg::length(normal);
    if (length <= 1e-6f) continue;
    normal = normal * (1.f / length);

    const face_surface_t *matched = find_face_surface(brush, Plane{a.position, normal});
    const face_surface_t &face = matched ? *matched : brush_default;
    const uint32_t material = face.material < scene.untextured_material
                                  ? (uint32_t)face.material
                                  : scene.untextured_material;

    append_triangle(scene, uid, a.position, b.position, c.position, a.uv, b.uv, c.uv,
                    material);
  }
}

// A static mesh is untextured to the tracer -- surface_at resolves brushes only
// -- and it is untextured here, carrying its real uvs so the day a mesh material
// arrives it is one table entry and not a new record.
void append_static_mesh(gpu_bake_scene_t &scene, entity_uid_t uid,
                        const static_mesh_geometry_t &static_mesh)
{
  const std::vector<world_triangle_t> triangles = static_mesh_world_triangles(static_mesh);
  const assets::mesh_asset_t *asset = assets::get(resolve_surface_mesh(static_mesh.surface));
  if (triangles.empty() || !asset)
  {
    log_warning("[lightmap] static mesh {} names no mesh that resolves; the GPU scene "
                "has no triangles for it.", uid);
    return;
  }

  for (size_t triangle = 0; triangle < triangles.size(); ++triangle)
  {
    const world_triangle_t &world = triangles[triangle];
    if (world.is_degenerate()) continue;

    const size_t first = triangle * 3;
    const linalg::vec2 uv_a = asset->vertices[asset->indices[first]].uv;
    const linalg::vec2 uv_b = asset->vertices[asset->indices[first + 1]].uv;
    const linalg::vec2 uv_c = asset->vertices[asset->indices[first + 2]].uv;

    append_triangle(scene, uid, world.corners[0], world.corners[1], world.corners[2], uv_a,
                    uv_b, uv_c, scene.untextured_material);
  }
}

} // namespace

uint8_t linear_to_srgb_byte(float linear)
{
  const float clamped = std::clamp(linear, 0.f, 1.f);
  const float encoded = clamped <= 0.0031308f
                            ? clamped * 12.92f
                            : 1.055f * std::pow(clamped, 1.f / 2.4f) - 0.055f;
  return (uint8_t)std::lround(encoded * 255.f);
}

gpu_bake_scene_t build_gpu_bake_scene(const map_t &map, const traced_scene_t &traced)
{
  gpu_bake_scene_t scene;

  layer_table_t albedo_layers{{}, &scene.albedo};
  layer_table_t emissive_layers{{}, &scene.emissive};

  scene.materials.reserve(traced.materials.size() + 1);
  for (const traced_scene_t::material_t &material : traced.materials)
    scene.materials.push_back({albedo_layers.layer_for(material.albedo),
                               emissive_layers.layer_for(material.emissive), 0, 0});

  scene.untextured_material = (uint32_t)scene.materials.size();
  scene.materials.push_back({GPU_NO_LAYER, GPU_NO_LAYER, 0, 0});

  for (const map_geometry_t &entry : map.geometry)
  {
    if (const brush_geometry_t *brush = std::get_if<brush_geometry_t>(&entry.value))
      append_brush(scene, map, entry.uid, *brush);
    else if (const static_mesh_geometry_t *static_mesh =
                 std::get_if<static_mesh_geometry_t>(&entry.value))
      append_static_mesh(scene, entry.uid, *static_mesh);
  }

  return scene;
}

linalg::vec3 sample_gpu_texture(const gpu_texture_array_t &array, uint32_t layer,
                                const linalg::vec2 &uv, const linalg::vec3 &fallback)
{
  if (layer == GPU_NO_LAYER || layer >= array.layer_count()) return fallback;

  const auto wrap = [](float coordinate) {
    int texel = (int)std::floor(coordinate * (float)LAYER_SIZE);
    texel %= (int)LAYER_SIZE;
    if (texel < 0) texel += (int)LAYER_SIZE;
    return (size_t)texel;
  };

  const size_t at = (size_t)layer * LAYER_BYTES + (wrap(uv.y) * LAYER_SIZE + wrap(uv.x)) * 4;
  return {srgb_byte_to_linear(array.pixels[at]), srgb_byte_to_linear(array.pixels[at + 1]),
          srgb_byte_to_linear(array.pixels[at + 2])};
}

traced_surface_t gpu_surface_at(const gpu_bake_scene_t &scene, uint32_t triangle_index,
                                const linalg::vec2 &uv)
{
  constexpr linalg::vec3 untextured{UNTEXTURED_BOUNCE_ALBEDO, UNTEXTURED_BOUNCE_ALBEDO,
                                    UNTEXTURED_BOUNCE_ALBEDO};
  if (triangle_index >= scene.triangles.size()) return {untextured, {}};

  const uint32_t material_index = scene.triangles[triangle_index].material;
  if (material_index >= scene.materials.size()) return {untextured, {}};
  const gpu_material_t &material = scene.materials[material_index];

  return {sample_gpu_texture(scene.albedo, material.albedo_layer, uv, untextured),
          sample_gpu_texture(scene.emissive, material.emissive_layer, uv, {0.f, 0.f, 0.f})};
}

linalg::vec2 gpu_triangle_uv_at(const gpu_bake_scene_t &scene, uint32_t triangle_index,
                                const linalg::vec3 &position)
{
  if (triangle_index >= scene.triangles.size()) return {0.f, 0.f};
  const gpu_triangle_t &triangle = scene.triangles[triangle_index];

  const auto corner = [&](size_t offset) {
    const linalg::vec4 &vertex =
        scene.vertices[scene.indices[(size_t)triangle_index * 3 + offset]];
    return linalg::vec3{vertex.x, vertex.y, vertex.z};
  };
  const linalg::vec3 a = corner(0);
  const linalg::vec3 b = corner(1);
  const linalg::vec3 c = corner(2);

  const linalg::vec3 edge_ab = b - a;
  const linalg::vec3 edge_ac = c - a;
  const linalg::vec3 to_point = position - a;
  const float dot_bb = linalg::dot(edge_ab, edge_ab);
  const float dot_bc = linalg::dot(edge_ab, edge_ac);
  const float dot_cc = linalg::dot(edge_ac, edge_ac);
  const float dot_pb = linalg::dot(to_point, edge_ab);
  const float dot_pc = linalg::dot(to_point, edge_ac);
  const float denominator = dot_bb * dot_cc - dot_bc * dot_bc;
  if (std::abs(denominator) <= 1e-12f) return triangle.uv0;

  const float weight_b = (dot_cc * dot_pb - dot_bc * dot_pc) / denominator;
  const float weight_c = (dot_bb * dot_pc - dot_bc * dot_pb) / denominator;
  const float weight_a = 1.f - weight_b - weight_c;

  return {triangle.uv0.x * weight_a + triangle.uv1.x * weight_b + triangle.uv2.x * weight_c,
          triangle.uv0.y * weight_a + triangle.uv1.y * weight_b + triangle.uv2.y * weight_c};
}

} // namespace shared
