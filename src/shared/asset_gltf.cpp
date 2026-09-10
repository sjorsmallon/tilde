#include "asset.hpp"

#include "log.hpp"
#include "tinygltf/tiny_gltf_configured.hpp"
#include "world_units.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace assets
{

namespace
{

using linalg::mat4f;
using linalg::quatf;
using linalg::vec2f;
using linalg::vec3f;
using linalg::vec4f;

struct glb_import_t
{
  const tinygltf::Model&                       model;
  const char*                                  key;
  mesh_asset_t&                                mesh;
  std::vector<asset_handle_t<texture_asset_t>> image_handles;
  std::vector<std::string>                     ignored_features;
  int32_t                                      fallback_material_slot = -1;
};

void note_ignored(glb_import_t& import, const std::string& feature)
{
  if (std::find(import.ignored_features.begin(), import.ignored_features.end(), feature) ==
      import.ignored_features.end())
    import.ignored_features.push_back(feature);
}

// glTF is metres, Y-up, front +Z; the engine is inches, Y-up, front +X, as blender_export.py writes.
mat4f engine_from_gltf()
{
  const float scale = shared::WORLD_UNITS_PER_METRE;
  mat4f result      = mat4f::identity();
  result[0]         = {0.0f, 0.0f, -scale, 0.0f};
  result[1]         = {0.0f, scale, 0.0f, 0.0f};
  result[2]         = {scale, 0.0f, 0.0f, 0.0f};
  return result;
}

mat4f local_transform_of(const tinygltf::Node& node)
{
  if (node.matrix.size() == 16)
  {
    mat4f result{};
    for (int column = 0; column < 4; ++column)
      result[column] = {(float)node.matrix[column * 4 + 0], (float)node.matrix[column * 4 + 1],
                        (float)node.matrix[column * 4 + 2], (float)node.matrix[column * 4 + 3]};
    return result;
  }

  vec3f translation = {0.0f, 0.0f, 0.0f};
  quatf rotation    = quatf::identity();
  vec3f scale       = {1.0f, 1.0f, 1.0f};
  if (node.translation.size() == 3)
    translation = {(float)node.translation[0], (float)node.translation[1],
                   (float)node.translation[2]};
  if (node.rotation.size() == 4)
    rotation = {(float)node.rotation[0], (float)node.rotation[1], (float)node.rotation[2],
                (float)node.rotation[3]};
  if (node.scale.size() == 3)
    scale = {(float)node.scale[0], (float)node.scale[1], (float)node.scale[2]};
  return linalg::compose_transform(translation, rotation, scale);
}

vec3f column_of(const mat4f& transform, int column)
{
  return {transform[column].x, transform[column].y, transform[column].z};
}

vec3f transform_point(const mat4f& transform, const vec3f& point)
{
  const vec4f moved = transform * vec4f{point.x, point.y, point.z, 1.0f};
  return {moved.x, moved.y, moved.z};
}

vec3f normalize_or_zero(const vec3f& vector)
{
  const float length = linalg::length(vector);
  return length > 0.0f ? vector * (1.0f / length) : vec3f{0.0f, 0.0f, 0.0f};
}

// --- Accessors --------------------------------------------------------------

const tinygltf::Accessor& accessor_at(const glb_import_t& import, int accessor_index,
                                      const std::string& role)
{
  if (accessor_index < 0 || accessor_index >= (int)import.model.accessors.size())
    fatal_error("mesh '{}' {} names accessor {}, which does not exist", import.key, role,
                accessor_index);
  return import.model.accessors[accessor_index];
}

const tinygltf::BufferView& buffer_view_at(const glb_import_t& import, int view_index,
                                           const std::string& role)
{
  if (view_index < 0 || view_index >= (int)import.model.bufferViews.size())
    fatal_error("mesh '{}' {} names buffer view {}, which does not exist", import.key, role,
                view_index);
  return import.model.bufferViews[view_index];
}

const uint8_t* bytes_of_view(const glb_import_t& import, const tinygltf::BufferView& view,
                             size_t byte_offset, size_t needed, const std::string& role)
{
  if (view.buffer < 0 || view.buffer >= (int)import.model.buffers.size())
    fatal_error("mesh '{}' {} names buffer {}, which does not exist", import.key, role,
                view.buffer);
  const tinygltf::Buffer& buffer = import.model.buffers[view.buffer];
  if (byte_offset + needed > view.byteLength ||
      view.byteOffset + view.byteLength > buffer.data.size())
    fatal_error("mesh '{}' {} reads {} bytes at offset {} of a {}-byte buffer view", import.key,
                role, needed, byte_offset, view.byteLength);
  return buffer.data.data() + view.byteOffset + byte_offset;
}

uint32_t read_index(const uint8_t* at, int component_type, const glb_import_t& import,
                    const std::string& role)
{
  switch (component_type)
  {
  case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
    return at[0];
  case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
  {
    uint16_t value;
    std::memcpy(&value, at, sizeof(value));
    return value;
  }
  case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
  {
    uint32_t value;
    std::memcpy(&value, at, sizeof(value));
    return value;
  }
  default:
    break;
  }
  fatal_error("mesh '{}' {} stores indices as component type {}, which glTF does not allow",
              import.key, role, component_type);
}

float read_float_component(const uint8_t* at, int component_type, bool normalized,
                           const glb_import_t& import, const std::string& role)
{
  switch (component_type)
  {
  case TINYGLTF_COMPONENT_TYPE_FLOAT:
  {
    float value;
    std::memcpy(&value, at, sizeof(value));
    return value;
  }
  case TINYGLTF_COMPONENT_TYPE_BYTE:
  {
    const float value = (float)(int8_t)at[0];
    return normalized ? std::max(value / 127.0f, -1.0f) : value;
  }
  case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
  {
    const float value = (float)at[0];
    return normalized ? value / 255.0f : value;
  }
  case TINYGLTF_COMPONENT_TYPE_SHORT:
  {
    int16_t raw;
    std::memcpy(&raw, at, sizeof(raw));
    const float value = (float)raw;
    return normalized ? std::max(value / 32767.0f, -1.0f) : value;
  }
  case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
  {
    uint16_t raw;
    std::memcpy(&raw, at, sizeof(raw));
    const float value = (float)raw;
    return normalized ? value / 65535.0f : value;
  }
  default:
    break;
  }
  fatal_error("mesh '{}' {} stores component type {}, which a vertex attribute cannot use",
              import.key, role, component_type);
}

template <typename Visit_T>
void for_each_accessor_element(const glb_import_t& import, const tinygltf::Accessor& accessor,
                               const std::string& role, Visit_T&& visit)
{
  const int32_t component_size  = tinygltf::GetComponentSizeInBytes((uint32_t)accessor.componentType);
  const int32_t component_count = tinygltf::GetNumComponentsInType((uint32_t)accessor.type);
  if (component_size <= 0 || component_count <= 0)
    fatal_error("mesh '{}' {} has component type {} and type {}, which glTF does not define",
                import.key, role, accessor.componentType, accessor.type);
  const size_t element_size = (size_t)component_size * (size_t)component_count;

  if (accessor.bufferView >= 0)
  {
    const tinygltf::BufferView& view   = buffer_view_at(import, accessor.bufferView, role);
    const int                   stride = accessor.ByteStride(view);
    if (stride <= 0)
      fatal_error("mesh '{}' {} has an invalid byte stride", import.key, role);
    const size_t needed =
        accessor.count == 0 ? 0 : (accessor.count - 1) * (size_t)stride + element_size;
    const uint8_t* first = bytes_of_view(import, view, accessor.byteOffset, needed, role);
    for (size_t element = 0; element < accessor.count; ++element)
      visit(element, first + element * (size_t)stride, component_size);
  }

  if (!accessor.sparse.isSparse)
    return;

  const size_t  sparse_count = (size_t)accessor.sparse.count;
  const int32_t index_size =
      tinygltf::GetComponentSizeInBytes((uint32_t)accessor.sparse.indices.componentType);
  if (index_size <= 0)
    fatal_error("mesh '{}' {} has sparse indices of component type {}", import.key, role,
                accessor.sparse.indices.componentType);

  const uint8_t* indices = bytes_of_view(
      import, buffer_view_at(import, accessor.sparse.indices.bufferView, role),
      accessor.sparse.indices.byteOffset, sparse_count * (size_t)index_size, role);
  const uint8_t* values = bytes_of_view(
      import, buffer_view_at(import, accessor.sparse.values.bufferView, role),
      accessor.sparse.values.byteOffset, sparse_count * element_size, role);

  for (size_t at = 0; at < sparse_count; ++at)
  {
    const uint32_t element = read_index(indices + at * (size_t)index_size,
                                        accessor.sparse.indices.componentType, import, role);
    if (element >= accessor.count)
      fatal_error("mesh '{}' {} replaces element {} of {}", import.key, role, element,
                  accessor.count);
    visit((size_t)element, values + at * element_size, component_size);
  }
}

std::vector<float> read_attribute(const glb_import_t& import, int accessor_index,
                                  int32_t expected_components, const std::string& role)
{
  const tinygltf::Accessor& accessor        = accessor_at(import, accessor_index, role);
  const int32_t             component_count = tinygltf::GetNumComponentsInType((uint32_t)accessor.type);
  if (component_count != expected_components)
    fatal_error("mesh '{}' {} has {} components per element where {} are required", import.key,
                role, component_count, expected_components);

  std::vector<float> values(accessor.count * (size_t)component_count, 0.0f);
  for_each_accessor_element(
      import, accessor, role,
      [&](size_t element, const uint8_t* at, int32_t component_size)
      {
        for (int32_t component = 0; component < component_count; ++component)
          values[element * (size_t)component_count + (size_t)component] = read_float_component(
              at + component * component_size, accessor.componentType, accessor.normalized,
              import, role);
      });
  return values;
}

std::vector<uint32_t> read_corners(const glb_import_t& import, const tinygltf::Primitive& primitive,
                                   size_t vertex_count, const std::string& role)
{
  std::vector<uint32_t> corners;
  if (primitive.indices < 0)
  {
    corners.resize(vertex_count);
    for (size_t vertex = 0; vertex < vertex_count; ++vertex)
      corners[vertex] = (uint32_t)vertex;
    return corners;
  }

  const std::string         index_role = role + " indices";
  const tinygltf::Accessor& accessor   = accessor_at(import, primitive.indices, index_role);
  if (accessor.type != TINYGLTF_TYPE_SCALAR)
    fatal_error("mesh '{}' {} are not scalars", import.key, index_role);

  corners.resize(accessor.count);
  for_each_accessor_element(import, accessor, index_role,
                            [&](size_t element, const uint8_t* at, int32_t)
                            { corners[element] = read_index(at, accessor.componentType, import, index_role); });

  for (uint32_t corner : corners)
    if (corner >= vertex_count)
      fatal_error("mesh '{}' {} name vertex {} of {}", import.key, index_role, corner,
                  vertex_count);
  return corners;
}

std::vector<uint32_t> triangle_list_from(const std::vector<uint32_t>& corners, int mode,
                                         const glb_import_t& import, const std::string& role)
{
  std::vector<uint32_t> triangles;
  if (mode == TINYGLTF_MODE_TRIANGLES)
  {
    if (corners.size() % 3 != 0)
      fatal_error("mesh '{}' {} is a triangle list of {} corners", import.key, role,
                  corners.size());
    triangles = corners;
  }
  else if (mode == TINYGLTF_MODE_TRIANGLE_STRIP)
  {
    for (size_t at = 0; at + 2 < corners.size(); ++at)
    {
      const bool odd = (at % 2) == 1;
      triangles.push_back(corners[at]);
      triangles.push_back(corners[at + (odd ? 2 : 1)]);
      triangles.push_back(corners[at + (odd ? 1 : 2)]);
    }
  }
  else
  {
    for (size_t at = 1; at + 1 < corners.size(); ++at)
    {
      triangles.push_back(corners[at]);
      triangles.push_back(corners[at + 1]);
      triangles.push_back(corners[0]);
    }
  }
  return triangles;
}

// --- Materials --------------------------------------------------------------

bool keep_encoded_image_bytes(tinygltf::Image* image, const int, std::string*, std::string*, int,
                              int, const unsigned char* bytes, int size, void*)
{
  image->image.assign(bytes, bytes + size);
  image->as_is = true;
  return true;
}

asset_handle_t<texture_asset_t> image_handle(glb_import_t& import, int image_index)
{
  if (image_index < 0 || image_index >= (int)import.model.images.size())
    fatal_error("mesh '{}' names image {}, which does not exist", import.key, image_index);

  asset_handle_t<texture_asset_t>& cached = import.image_handles[image_index];
  if (cached.valid())
    return cached;

  const tinygltf::Image& image = import.model.images[image_index];
  if (!image.image.empty())
  {
    const std::string image_key = std::string(import.key) + "#image" + std::to_string(image_index);
    cached                      = find_texture_in_cache(image_key);
    if (!cached.valid())
      cached = register_dynamic_texture(
          image_key, decode_image(Span<const uint8_t>(image.image.data(), (uint32_t)image.image.size()),
                                  image_key.c_str()));
    return cached;
  }

  if (!image.uri.empty())
  {
    const std::string sibling =
        (std::filesystem::path(import.key).parent_path() / image.uri).generic_string();
    cached = load_texture(sibling.c_str());
    return cached;
  }

  fatal_error("mesh '{}' image {} carries neither bytes nor a uri", import.key, image_index);
}

asset_handle_t<texture_asset_t> texture_handle(glb_import_t& import, int texture_index,
                                               int texcoord_set)
{
  if (texture_index < 0)
    return {};
  if (texture_index >= (int)import.model.textures.size())
    fatal_error("mesh '{}' names texture {}, which does not exist", import.key, texture_index);
  if (texcoord_set != 0)
    note_ignored(import, "TEXCOORD_1 and up (sampled with TEXCOORD_0)");

  const tinygltf::Texture& texture = import.model.textures[texture_index];
  if (texture.source < 0)
    fatal_error("mesh '{}' texture {} names no image", import.key, texture_index);
  return image_handle(import, texture.source);
}

uint8_t nearest_texel(const texture_asset_t& texture, int32_t channel, int32_t x, int32_t y,
                      int32_t width, int32_t height)
{
  const size_t source_x = (size_t)((int64_t)x * texture.width / width);
  const size_t source_y = (size_t)((int64_t)y * texture.height / height);
  return texture.pixels[(source_y * (size_t)texture.width + source_x) * 4 + (size_t)channel];
}

uint8_t to_byte(float value)
{
  return (uint8_t)std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f);
}

float srgb_to_linear(float encoded)
{
  return encoded <= 0.04045f ? encoded / 12.92f : std::pow((encoded + 0.055f) / 1.055f, 2.4f);
}

float linear_to_srgb(float linear)
{
  return linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

asset_handle_t<texture_asset_t> occlusion_roughness_metallic_of(glb_import_t&             import,
                                                                const tinygltf::Material& material,
                                                                size_t material_index)
{
  const tinygltf::PbrMetallicRoughness& pbr = material.pbrMetallicRoughness;
  const asset_handle_t<texture_asset_t> metallic_roughness =
      texture_handle(import, pbr.metallicRoughnessTexture.index, pbr.metallicRoughnessTexture.texCoord);
  const asset_handle_t<texture_asset_t> occlusion =
      texture_handle(import, material.occlusionTexture.index, material.occlusionTexture.texCoord);
  const float roughness_factor   = (float)pbr.roughnessFactor;
  const float metallic_factor    = (float)pbr.metallicFactor;
  const float occlusion_strength = (float)material.occlusionTexture.strength;

  if (!metallic_roughness.valid() && !occlusion.valid() && roughness_factor == 1.0f &&
      metallic_factor == 0.0f)
    return {};

  if (metallic_roughness.valid() && occlusion.index == metallic_roughness.index &&
      roughness_factor == 1.0f && metallic_factor == 1.0f && occlusion_strength == 1.0f)
    return metallic_roughness;

  const std::string composed_key =
      std::string(import.key) + "#material" + std::to_string(material_index) + ".orm";
  const asset_handle_t<texture_asset_t> cached = find_texture_in_cache(composed_key);
  if (cached.valid())
    return cached;

  const texture_asset_t* metallic_roughness_texels =
      metallic_roughness.valid() ? get(metallic_roughness) : nullptr;
  const texture_asset_t* occlusion_texels = occlusion.valid() ? get(occlusion) : nullptr;
  const texture_asset_t* size_source =
      metallic_roughness_texels ? metallic_roughness_texels : occlusion_texels;

  texture_asset_t composed;
  composed.width    = size_source ? size_source->width : 1;
  composed.height   = size_source ? size_source->height : 1;
  composed.channels = 4;
  composed.pixels.resize((size_t)composed.width * (size_t)composed.height * 4);

  for (int32_t y = 0; y < composed.height; ++y)
    for (int32_t x = 0; x < composed.width; ++x)
    {
      const float occlusion_value =
          occlusion_texels
              ? 1.0f + occlusion_strength *
                           (nearest_texel(*occlusion_texels, 0, x, y, composed.width, composed.height) / 255.0f - 1.0f)
              : 1.0f;
      const float roughness_value =
          roughness_factor *
          (metallic_roughness_texels
               ? nearest_texel(*metallic_roughness_texels, 1, x, y, composed.width, composed.height) / 255.0f
               : 1.0f);
      const float metallic_value =
          metallic_factor *
          (metallic_roughness_texels
               ? nearest_texel(*metallic_roughness_texels, 2, x, y, composed.width, composed.height) / 255.0f
               : 1.0f);

      const size_t at      = ((size_t)y * (size_t)composed.width + (size_t)x) * 4;
      composed.pixels[at]     = to_byte(occlusion_value);
      composed.pixels[at + 1] = to_byte(roughness_value);
      composed.pixels[at + 2] = to_byte(metallic_value);
      composed.pixels[at + 3] = 255;
    }

  return register_dynamic_texture(composed_key, std::move(composed));
}

asset_handle_t<texture_asset_t> emissive_of(glb_import_t& import, const tinygltf::Material& material,
                                            size_t material_index)
{
  const asset_handle_t<texture_asset_t> emissive =
      texture_handle(import, material.emissiveTexture.index, material.emissiveTexture.texCoord);

  float factor[3] = {0.0f, 0.0f, 0.0f};
  for (size_t channel = 0; channel < 3 && channel < material.emissiveFactor.size(); ++channel)
    factor[channel] = (float)material.emissiveFactor[channel];

  if (factor[0] == 0.0f && factor[1] == 0.0f && factor[2] == 0.0f)
    return {};
  if (emissive.valid() && factor[0] == 1.0f && factor[1] == 1.0f && factor[2] == 1.0f)
    return emissive;

  const std::string composed_key =
      std::string(import.key) + "#material" + std::to_string(material_index) + ".emissive";
  const asset_handle_t<texture_asset_t> cached = find_texture_in_cache(composed_key);
  if (cached.valid())
    return cached;

  const texture_asset_t* texels = emissive.valid() ? get(emissive) : nullptr;

  texture_asset_t composed;
  composed.width    = texels ? texels->width : 1;
  composed.height   = texels ? texels->height : 1;
  composed.channels = 4;
  composed.pixels.resize((size_t)composed.width * (size_t)composed.height * 4);

  for (int32_t y = 0; y < composed.height; ++y)
    for (int32_t x = 0; x < composed.width; ++x)
    {
      const size_t at = ((size_t)y * (size_t)composed.width + (size_t)x) * 4;
      for (int32_t channel = 0; channel < 3; ++channel)
      {
        const float encoded =
            texels ? nearest_texel(*texels, channel, x, y, composed.width, composed.height) / 255.0f
                   : 1.0f;
        composed.pixels[at + (size_t)channel] =
            to_byte(linear_to_srgb(srgb_to_linear(encoded) * factor[channel]));
      }
      composed.pixels[at + 3] = 255;
    }

  return register_dynamic_texture(composed_key, std::move(composed));
}

void import_materials(glb_import_t& import)
{
  const tinygltf::Model& model = import.model;
  for (size_t material_index = 0; material_index < model.materials.size(); ++material_index)
  {
    const tinygltf::Material&             source = model.materials[material_index];
    const tinygltf::PbrMetallicRoughness& pbr    = source.pbrMetallicRoughness;

    material_t material;
    material.name = source.name.empty() ? "material" + std::to_string(material_index) : source.name;
    if (pbr.baseColorFactor.size() == 4)
      material.diffuse_color = {(float)pbr.baseColorFactor[0], (float)pbr.baseColorFactor[1],
                                (float)pbr.baseColorFactor[2]};

    material.maps.albedo = texture_handle(import, pbr.baseColorTexture.index, pbr.baseColorTexture.texCoord);
    material.maps.normal = texture_handle(import, source.normalTexture.index, source.normalTexture.texCoord);
    material.maps.orm      = occlusion_roughness_metallic_of(import, source, material_index);
    material.maps.emissive = emissive_of(import, source, material_index);

    if (source.alphaMode != "OPAQUE")
      note_ignored(import, "alpha mode " + source.alphaMode + " (drawn opaque)");
    if (source.doubleSided)
      note_ignored(import, "double-sided materials");
    if (source.normalTexture.index >= 0 && source.normalTexture.scale != 1.0)
      note_ignored(import, "normal map scale");

    import.mesh.materials.push_back(std::move(material));
  }
}

uint32_t material_slot_for(glb_import_t& import, int material_index, const std::string& role)
{
  if (material_index >= 0)
  {
    if (material_index >= (int)import.model.materials.size())
      fatal_error("mesh '{}' {} names material {}, which does not exist", import.key, role,
                  material_index);
    return (uint32_t)material_index;
  }

  if (import.fallback_material_slot < 0)
  {
    material_t fallback;
    fallback.name                 = "default";
    import.fallback_material_slot = (int32_t)import.mesh.materials.size();
    import.mesh.materials.push_back(std::move(fallback));
  }
  return (uint32_t)import.fallback_material_slot;
}

// --- Geometry ---------------------------------------------------------------

void append_primitive(glb_import_t& import, const tinygltf::Primitive& primitive,
                      const mat4f& transform, const std::string& role)
{
  if (primitive.mode != TINYGLTF_MODE_TRIANGLES && primitive.mode != TINYGLTF_MODE_TRIANGLE_STRIP &&
      primitive.mode != TINYGLTF_MODE_TRIANGLE_FAN)
  {
    log_warning("[glb] {}: {} is drawn in mode {}, which has no surface; skipped", import.key,
                role, primitive.mode);
    return;
  }
  if (!primitive.targets.empty())
    note_ignored(import, "morph targets");
  if (primitive.attributes.count("COLOR_0") != 0)
    note_ignored(import, "vertex colours");
  if (primitive.attributes.count("JOINTS_0") != 0)
    note_ignored(import, "skinning (drawn in its bind pose)");

  const auto position_attribute = primitive.attributes.find("POSITION");
  if (position_attribute == primitive.attributes.end())
    fatal_error("mesh '{}' {} has no POSITION", import.key, role);

  const std::vector<float> positions = read_attribute(import, position_attribute->second, 3, role + " POSITION");
  const size_t             vertex_count = positions.size() / 3;

  std::vector<float> normals;
  const auto         normal_attribute = primitive.attributes.find("NORMAL");
  if (normal_attribute != primitive.attributes.end())
  {
    normals = read_attribute(import, normal_attribute->second, 3, role + " NORMAL");
    if (normals.size() != positions.size())
      fatal_error("mesh '{}' {} has {} normals for {} positions", import.key, role,
                  normals.size() / 3, vertex_count);
  }

  std::vector<float> uvs;
  const auto         uv_attribute = primitive.attributes.find("TEXCOORD_0");
  if (uv_attribute != primitive.attributes.end())
  {
    uvs = read_attribute(import, uv_attribute->second, 2, role + " TEXCOORD_0");
    if (uvs.size() / 2 != vertex_count)
      fatal_error("mesh '{}' {} has {} uvs for {} positions", import.key, role, uvs.size() / 2,
                  vertex_count);
  }

  const std::vector<uint32_t> triangles = triangle_list_from(
      read_corners(import, primitive, vertex_count, role), primitive.mode, import, role);

  const vec3f axis_x      = column_of(transform, 0);
  const vec3f axis_y      = column_of(transform, 1);
  const vec3f axis_z      = column_of(transform, 2);
  const float determinant = linalg::dot(axis_x, linalg::cross(axis_y, axis_z));
  if (determinant == 0.0f)
  {
    log_warning("[glb] {}: {} is scaled to nothing by its node; skipped", import.key, role);
    return;
  }

  const bool  mirrored        = determinant < 0.0f;
  const float normal_sign     = mirrored ? -1.0f : 1.0f;
  const vec3f normal_column_x = linalg::cross(axis_y, axis_z) * normal_sign;
  const vec3f normal_column_y = linalg::cross(axis_z, axis_x) * normal_sign;
  const vec3f normal_column_z = linalg::cross(axis_x, axis_y) * normal_sign;

  auto position_of = [&](uint32_t vertex) -> vec3f
  {
    return transform_point(transform, {positions[vertex * 3], positions[vertex * 3 + 1],
                                       positions[vertex * 3 + 2]});
  };
  auto uv_of = [&](uint32_t vertex) -> vec2f
  { return uvs.empty() ? vec2f{0.0f, 0.0f} : vec2f{uvs[vertex * 2], uvs[vertex * 2 + 1]}; };

  mesh_asset_t&  mesh         = import.mesh;
  const uint32_t index_offset = (uint32_t)mesh.indices.size();

  if (!normals.empty())
  {
    const uint32_t first_vertex = (uint32_t)mesh.vertices.size();
    for (uint32_t vertex = 0; vertex < vertex_count; ++vertex)
    {
      const float* normal = &normals[vertex * 3];
      vertex_xnu   out{};
      out.position = position_of(vertex);
      out.normal   = normalize_or_zero(normal_column_x * normal[0] + normal_column_y * normal[1] +
                                       normal_column_z * normal[2]);
      out.uv       = uv_of(vertex);
      mesh.vertices.push_back(out);
    }
    for (size_t at = 0; at < triangles.size(); at += 3)
    {
      mesh.indices.push_back(first_vertex + triangles[at]);
      mesh.indices.push_back(first_vertex + triangles[at + (mirrored ? 2 : 1)]);
      mesh.indices.push_back(first_vertex + triangles[at + (mirrored ? 1 : 2)]);
    }
  }
  else
  {
    for (size_t at = 0; at < triangles.size(); at += 3)
    {
      const uint32_t corners[3] = {triangles[at], triangles[at + (mirrored ? 2 : 1)],
                                   triangles[at + (mirrored ? 1 : 2)]};
      const vec3f    a          = position_of(corners[0]);
      const vec3f    b          = position_of(corners[1]);
      const vec3f    c          = position_of(corners[2]);
      const vec3f    face_normal = normalize_or_zero(linalg::cross(b - a, c - a));
      for (uint32_t corner : corners)
      {
        vertex_xnu out{};
        out.position = position_of(corner);
        out.normal   = face_normal;
        out.uv       = uv_of(corner);
        mesh.indices.push_back((uint32_t)mesh.vertices.size());
        mesh.vertices.push_back(out);
      }
    }
  }

  submesh_t submesh;
  submesh.index_offset   = index_offset;
  submesh.index_count    = (uint32_t)mesh.indices.size() - index_offset;
  submesh.material_index = material_slot_for(import, primitive.material, role);
  if (submesh.index_count > 0)
    mesh.submeshes.push_back(submesh);
}

void walk_node(glb_import_t& import, int node_index, const mat4f& parent_transform, size_t depth)
{
  const tinygltf::Model& model = import.model;
  if (node_index < 0 || node_index >= (int)model.nodes.size())
    fatal_error("mesh '{}' names node {}, which does not exist", import.key, node_index);
  if (depth > model.nodes.size())
    fatal_error("mesh '{}' node {} is its own ancestor", import.key, node_index);

  const tinygltf::Node& node      = model.nodes[node_index];
  const mat4f           transform = parent_transform * local_transform_of(node);

  if (node.skin >= 0)
    note_ignored(import, "skinning (drawn in its bind pose)");

  if (node.mesh >= 0)
  {
    if (node.mesh >= (int)model.meshes.size())
      fatal_error("mesh '{}' node {} names mesh {}, which does not exist", import.key, node_index,
                  node.mesh);
    const tinygltf::Mesh& source    = model.meshes[node.mesh];
    const std::string     mesh_name = source.name.empty() ? std::to_string(node.mesh) : source.name;
    for (size_t primitive = 0; primitive < source.primitives.size(); ++primitive)
      append_primitive(import, source.primitives[primitive], transform,
                       "primitive " + std::to_string(primitive) + " of mesh '" + mesh_name + "'");
  }

  for (int child : node.children)
    walk_node(import, child, transform, depth + 1);
}

std::vector<int> root_nodes_of(glb_import_t& import)
{
  const tinygltf::Model& model = import.model;
  if (!model.scenes.empty())
  {
    const int scene = model.defaultScene >= 0 ? model.defaultScene : 0;
    if (scene >= (int)model.scenes.size())
      fatal_error("mesh '{}' names scene {}, which does not exist", import.key, scene);
    if (model.scenes.size() > 1)
      note_ignored(import, "every scene but scene " + std::to_string(scene));
    return model.scenes[scene].nodes;
  }

  std::vector<bool> is_child(model.nodes.size(), false);
  for (const tinygltf::Node& node : model.nodes)
    for (int child : node.children)
      if (child >= 0 && child < (int)model.nodes.size())
        is_child[child] = true;

  std::vector<int> roots;
  for (size_t node = 0; node < model.nodes.size(); ++node)
    if (!is_child[node])
      roots.push_back((int)node);
  return roots;
}

} // namespace

mesh_asset_t decode_glb(Span<const uint8_t> bytes, const char* key)
{
  tinygltf::TinyGLTF loader;
  loader.SetImageLoader(keep_encoded_image_bytes, nullptr);

  tinygltf::Model model;
  std::string     error;
  std::string     warning;
  const bool      loaded = loader.LoadBinaryFromMemory(&model, &error, &warning, bytes.data, bytes.size());
  if (!warning.empty())
    log_warning("[glb] {}: {}", key, warning);
  if (!loaded)
    fatal_error("mesh '{}' is there but did not parse as binary glTF: {}", key, error);

  for (const std::string& extension : model.extensionsRequired)
    fatal_error("mesh '{}' requires glTF extension '{}', which this importer does not implement",
                key, extension);

  mesh_asset_t mesh;
  glb_import_t import{model, key, mesh};
  import.image_handles.resize(model.images.size());

  for (const std::string& extension : model.extensionsUsed)
    note_ignored(import, "extension " + extension);
  if (!model.animations.empty())
    note_ignored(import, "animations");

  import_materials(import);

  const mat4f root_transform = engine_from_gltf();
  for (int root : root_nodes_of(import))
    walk_node(import, root, root_transform, 0);

  if (mesh.indices.empty())
    fatal_error("mesh '{}' holds no triangles", key);

  if (!import.ignored_features.empty())
  {
    std::string joined;
    for (const std::string& feature : import.ignored_features)
      joined += (joined.empty() ? "" : ", ") + feature;
    log_warning("[glb] {}: not imported: {}", key, joined);
  }

  printf("[assets] Loaded glb '%s': %zu verts, %zu indices, %zu submeshes, %zu materials\n", key,
         mesh.vertices.size(), mesh.indices.size(), mesh.submeshes.size(), mesh.materials.size());
  return mesh;
}

} // namespace assets
