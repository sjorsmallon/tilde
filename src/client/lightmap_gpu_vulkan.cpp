#include "lightmap_gpu_vulkan.hpp"

#include "../shared/log.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace client
{

namespace
{

const uint32_t lightmap_probe_ray_comp_spv[] =
#include "lightmap_probe_ray.comp.spv.h"
    ;

const uint32_t lightmap_indirect_comp_spv[] =
#include "lightmap_indirect.comp.spv.h"
    ;

const uint32_t lightmap_direct_comp_spv[] =
#include "lightmap_direct.comp.spv.h"
    ;

// The push block lightmap_probe_ray.comp reads.
struct probe_ray_push_t
{
  uint32_t sample_count = 0;
  float ray_bias = 0.f;
  float max_distance = 0.f;
};

// The push block both shading kernels read: the two counts, then the settings
// block verbatim.
struct bake_push_t
{
  uint32_t sample_count = 0;
  uint32_t light_count = 0;
  shared::gpu_bake_settings_t settings;
};
static_assert(sizeof(bake_push_t) == 40 && offsetof(bake_push_t, settings) == 8,
              "bake_push_t is the push block lightmap_indirect.comp and lightmap_direct.comp read");

// lightmap_direct.comp's results: a vec4 per record (irradiance rgb, shadow rays
// cast in w) at the front, then coverage and weight, sample-major.
struct direct_record_result_t
{
  float irradiance[3];
  float rays_cast;
};
static_assert(sizeof(direct_record_result_t) == 16);
static_assert(sizeof(linalg::vec3) == 12, "the coverage and weight readback is one memcpy");

size_t direct_result_floats_per_record(size_t light_count)
{
  return 4 + 2 * light_count;
}

// light_arrival.glsl's struct Light, filled the way renderer.cpp fills
// gpu_light_t: the kernel includes that text and reads this layout.
struct gpu_bake_light_t
{
  float position[4];
  float direction[4];
  float radiance[4];
  float spot_params[4];
};
static_assert(sizeof(gpu_bake_light_t) == 64, "struct Light in light_arrival.glsl is 64 bytes");

gpu_bake_light_t bake_light_from(const shared::baked_light_t &source, uint32_t slot)
{
  const shared::scene_light_t &light = source.light;
  gpu_bake_light_t out{};
  out.position[0] = light.position.x;
  out.position[1] = light.position.y;
  out.position[2] = light.position.z;
  out.position[3] = (float)slot;
  out.direction[0] = light.forward.x;
  out.direction[1] = light.forward.y;
  out.direction[2] = light.forward.z;
  out.direction[3] = light.source_radius;
  out.radiance[0] = light.radiance.x;
  out.radiance[1] = light.radiance.y;
  out.radiance[2] = light.radiance.z;
  out.radiance[3] = -1.f;
  out.spot_params[0] = light.cos_inner;
  out.spot_params[1] = light.cos_outer;
  out.spot_params[2] = light.range;
  out.spot_params[3] = (float)(int)light.kind;
  return out;
}

// The results buffer is twelve floats per record in indirect_sh_l1_t's order, so
// the readback is one memcpy.
static_assert(sizeof(shared::indirect_sh_l1_t) == 12 * sizeof(float),
              "lightmap_indirect.comp writes indirect_sh_l1_t as twelve contiguous floats");

constexpr uint32_t WORKGROUP_SIZE = 64;

// How many records one indirect dispatch shades. A batch is millions of records
// and each is rays_per_sample chains of several rays, and Windows resets a GPU
// whose kernel runs past about two seconds -- so a dispatch is cut to a budget
// of CHAINS (two million, tens of milliseconds on a 4090) rather than a count of
// records, and never fewer than a workgroup's worth of them.
size_t indirect_records_per_dispatch(int rays_per_sample)
{
  constexpr size_t CHAIN_BUDGET = (size_t)1 << 21;
  constexpr size_t MOST = (size_t)1 << 16;
  const size_t by_chains = CHAIN_BUDGET / (size_t)std::max(rays_per_sample, 1);
  return std::clamp<size_t>(by_chains, 1024, MOST);
}

// The direct term's cut, by the same rule over SHADOW RAYS: a record costs at
// most one spiral per light, and a shadow ray is a fraction of a chain, so the
// budget is larger and the records per dispatch more.
size_t direct_records_per_dispatch(size_t light_count, int soft_shadow_samples)
{
  constexpr size_t RAY_BUDGET = (size_t)1 << 23;
  constexpr size_t MOST = (size_t)1 << 18;
  const size_t rays_per_record =
      std::max<size_t>(light_count, 1) * (size_t)std::max(soft_shadow_samples, 1);
  return std::clamp<size_t>(RAY_BUDGET / rays_per_record, 1024, MOST);
}

VkDeviceAddress align_up(VkDeviceAddress value, VkDeviceAddress alignment)
{
  return (value + alignment - 1) / alignment * alignment;
}

// A bake is an editor tool on one workstation: a Vulkan call that fails here is
// a broken setup, not something to draw around.
void check(VkResult result, const char *what)
{
  if (result != VK_SUCCESS)
    fatal_error("[lightmap-gpu] {} failed with VkResult {}.", what, (int)result);
}

VkWriteDescriptorSet buffer_write(VkDescriptorSet set, uint32_t binding,
                                  const VkDescriptorBufferInfo *info)
{
  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = set;
  write.dstBinding = binding;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  write.pBufferInfo = info;
  return write;
}

} // namespace

vulkan_batch_solver_t::vulkan_batch_solver_t()
{
  if (!renderer::ray_query_is_available())
    fatal_error("[lightmap-gpu] a Vulkan solver on a device without ray query ({}); ask "
                "renderer::ray_query_is_available() first.",
                renderer::ray_query_unavailable_reason());
  device = renderer::ray_query_device();

  vkGetPhysicalDeviceMemoryProperties(device.physical_device, &memory_properties);

  VkPhysicalDeviceAccelerationStructurePropertiesKHR acceleration_properties{};
  acceleration_properties.sType =
      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR;
  VkPhysicalDeviceProperties2 properties{};
  properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  properties.pNext = &acceleration_properties;
  vkGetPhysicalDeviceProperties2(device.physical_device, &properties);
  scratch_alignment = std::max<VkDeviceSize>(
      acceleration_properties.minAccelerationStructureScratchOffsetAlignment, 1);

  // The texture descriptor array is sized once, here, at the smaller of our cap
  // and what the device lets one compute stage see; a scene naming more than
  // this refuses loudly at upload.
  const VkPhysicalDeviceLimits &limits = properties.properties.limits;
  texture_descriptor_capacity =
      std::min({MOST_TEXTURES, limits.maxPerStageDescriptorSampledImages,
                limits.maxDescriptorSetSampledImages});

  VkFenceCreateInfo fence_info{};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  check(vkCreateFence(device.device, &fence_info, nullptr, &fence), "vkCreateFence");

  create_kernels();
}

vulkan_batch_solver_t::~vulkan_batch_solver_t()
{
  // Every submission this object made was waited on before it returned, so
  // nothing here is in flight.
  destroy_scene();
  destroy_buffer(dispatch_samples);
  destroy_buffer(dispatch_results);
  destroy_buffer(dispatch_chart_light_masks);
  destroy_kernel(probe_kernel);
  destroy_kernel(indirect_kernel);
  destroy_kernel(direct_kernel);
  if (descriptor_pool) vkDestroyDescriptorPool(device.device, descriptor_pool, nullptr);
  if (fence) vkDestroyFence(device.device, fence, nullptr);
}

// --- Memory -------------------------------------------------------------------

uint32_t vulkan_batch_solver_t::memory_type_for(uint32_t type_bits,
                                                VkMemoryPropertyFlags properties) const
{
  for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i)
    if ((type_bits & (1u << i)) &&
        (memory_properties.memoryTypes[i].propertyFlags & properties) == properties)
      return i;
  fatal_error("[lightmap-gpu] no memory type matches bits {:#x} with properties {:#x}.",
              type_bits, (uint32_t)properties);
}

gpu_buffer_t vulkan_batch_solver_t::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                                  VkMemoryPropertyFlags properties) const
{
  gpu_buffer_t out;
  out.size = size;

  VkBufferCreateInfo buffer_info{};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = std::max<VkDeviceSize>(size, 1);
  buffer_info.usage = usage;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  check(vkCreateBuffer(device.device, &buffer_info, nullptr, &out.buffer), "vkCreateBuffer");

  VkMemoryRequirements requirements{};
  vkGetBufferMemoryRequirements(device.device, out.buffer, &requirements);

  // A buffer the acceleration structure build or a kernel addresses by device
  // address needs its MEMORY flagged for it as well as its usage.
  const bool addressed = (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) != 0;
  VkMemoryAllocateFlagsInfo allocate_flags{};
  allocate_flags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
  allocate_flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

  VkMemoryAllocateInfo allocate_info{};
  allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocate_info.pNext = addressed ? &allocate_flags : nullptr;
  allocate_info.allocationSize = requirements.size;
  allocate_info.memoryTypeIndex = memory_type_for(requirements.memoryTypeBits, properties);
  check(vkAllocateMemory(device.device, &allocate_info, nullptr, &out.memory),
        "vkAllocateMemory");
  check(vkBindBufferMemory(device.device, out.buffer, out.memory, 0), "vkBindBufferMemory");

  if (addressed)
  {
    VkBufferDeviceAddressInfo address_info{};
    address_info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    address_info.buffer = out.buffer;
    out.address = vkGetBufferDeviceAddress(device.device, &address_info);
  }
  return out;
}

void vulkan_batch_solver_t::destroy_buffer(gpu_buffer_t &buffer) const
{
  if (buffer.buffer) vkDestroyBuffer(device.device, buffer.buffer, nullptr);
  if (buffer.memory) vkFreeMemory(device.device, buffer.memory, nullptr);
  buffer = {};
}

void vulkan_batch_solver_t::write_host_visible(const gpu_buffer_t &buffer, const void *data,
                                               VkDeviceSize size) const
{
  if (size == 0) return;
  if (size > buffer.size)
    fatal_error("[lightmap-gpu] writing {} bytes into a {}-byte buffer.", size, buffer.size);
  void *mapped = nullptr;
  check(vkMapMemory(device.device, buffer.memory, 0, size, 0, &mapped), "vkMapMemory");
  std::memcpy(mapped, data, (size_t)size);
  vkUnmapMemory(device.device, buffer.memory);
}

gpu_buffer_t vulkan_batch_solver_t::upload_device_local(const void *data, VkDeviceSize size,
                                                        VkBufferUsageFlags usage)
{
  gpu_buffer_t staging = create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  write_host_visible(staging, data, size);

  gpu_buffer_t out = create_buffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (size > 0)
  {
    VkCommandBuffer commands = begin_commands();
    VkBufferCopy region{};
    region.size = size;
    vkCmdCopyBuffer(commands, staging.buffer, out.buffer, 1, &region);
    submit_and_wait(commands);
  }
  destroy_buffer(staging);
  return out;
}

void vulkan_batch_solver_t::ensure_host_visible(gpu_buffer_t &buffer, VkDeviceSize bytes) const
{
  if (buffer.size >= bytes) return;
  destroy_buffer(buffer);
  buffer = create_buffer(bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

void vulkan_batch_solver_t::upload_textures(
    const std::vector<const assets::texture_asset_t *> &textures)
{
  destroy_textures();
  if (textures.size() > texture_descriptor_capacity)
    fatal_error("[lightmap-gpu] the scene names {} textures and the solver's descriptor array "
                "holds {}.",
                textures.size(), texture_descriptor_capacity);
  if (textures.empty()) return;

  // Every texture as its own image at its own size, the asset's bytes verbatim
  // (an asset with three channels is widened to four), one submission for all.
  texture_images.resize(textures.size());
  std::vector<gpu_buffer_t> stagings(textures.size());
  std::vector<std::vector<uint8_t>> widened(textures.size());
  VkCommandBuffer commands = begin_commands();

  for (size_t i = 0; i < textures.size(); ++i)
  {
    const assets::texture_asset_t &texture = *textures[i];
    gpu_texture_image_t &out = texture_images[i];
    const uint32_t width = (uint32_t)texture.width;
    const uint32_t height = (uint32_t)texture.height;
    const size_t texel_count = (size_t)width * (size_t)height;
    const size_t byte_count = texel_count * 4;

    const uint8_t *pixels = texture.pixels.data();
    if (texture.channels != 4)
    {
      std::vector<uint8_t> &rgba = widened[i];
      rgba.resize(byte_count, 255);
      for (size_t texel = 0; texel < texel_count; ++texel)
        std::memcpy(&rgba[texel * 4], &texture.pixels[texel * (size_t)texture.channels], 3);
      pixels = rgba.data();
    }

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_R8G8B8A8_UINT;
    image_info.extent = {width, height, 1};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(device.device, &image_info, nullptr, &out.image), "vkCreateImage");

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device.device, out.image, &requirements);
    VkMemoryAllocateInfo allocate_info{};
    allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.allocationSize = requirements.size;
    allocate_info.memoryTypeIndex =
        memory_type_for(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device.device, &allocate_info, nullptr, &out.memory),
          "vkAllocateMemory (texture)");
    check(vkBindImageMemory(device.device, out.image, out.memory, 0), "vkBindImageMemory");

    stagings[i] = create_buffer(byte_count, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    write_host_visible(stagings[i], pixels, byte_count);

    VkImageSubresourceRange whole{};
    whole.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    whole.levelCount = 1;
    whole.layerCount = 1;

    VkImageMemoryBarrier to_transfer{};
    to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.image = out.image;
    to_transfer.subresourceRange = whole;
    to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &to_transfer);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(commands, stagings[i].buffer, out.image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier to_sampled = to_transfer;
    to_sampled.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    to_sampled.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    to_sampled.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    to_sampled.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                         &to_sampled);

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = out.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_R8G8B8A8_UINT;
    view_info.subresourceRange = whole;
    check(vkCreateImageView(device.device, &view_info, nullptr, &out.view), "vkCreateImageView");
  }

  submit_and_wait(commands);
  for (gpu_buffer_t &staging : stagings) destroy_buffer(staging);
}

void vulkan_batch_solver_t::destroy_textures()
{
  for (gpu_texture_image_t &image : texture_images)
  {
    if (image.view) vkDestroyImageView(device.device, image.view, nullptr);
    if (image.image) vkDestroyImage(device.device, image.image, nullptr);
    if (image.memory) vkFreeMemory(device.device, image.memory, nullptr);
  }
  texture_images.clear();
}

// --- Commands -----------------------------------------------------------------

VkCommandBuffer vulkan_batch_solver_t::begin_commands() const
{
  VkCommandBufferAllocateInfo allocate_info{};
  allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocate_info.commandPool = device.command_pool;
  allocate_info.commandBufferCount = 1;
  VkCommandBuffer commands = VK_NULL_HANDLE;
  check(vkAllocateCommandBuffers(device.device, &allocate_info, &commands),
        "vkAllocateCommandBuffers");

  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  check(vkBeginCommandBuffer(commands, &begin_info), "vkBeginCommandBuffer");
  return commands;
}

// Its own fence rather than the renderer's vkQueueWaitIdle: a bake dispatch
// waits for exactly what it submitted and nothing the frame has in flight.
void vulkan_batch_solver_t::submit_and_wait(VkCommandBuffer commands)
{
  check(vkEndCommandBuffer(commands), "vkEndCommandBuffer");
  check(vkResetFences(device.device, 1, &fence), "vkResetFences");

  VkSubmitInfo submit_info{};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &commands;
  check(vkQueueSubmit(device.queue, 1, &submit_info, fence), "vkQueueSubmit");
  check(vkWaitForFences(device.device, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");

  vkFreeCommandBuffers(device.device, device.command_pool, 1, &commands);
}

// One kernel over `record_count` records, one workgroup-rounded dispatch, and a
// barrier so the host may read the results buffer once the fence is through.
void vulkan_batch_solver_t::dispatch_and_wait(const compute_kernel_t &kernel, const void *push,
                                              uint32_t push_bytes, uint32_t record_count)
{
  VkCommandBuffer commands = begin_commands();
  vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, kernel.pipeline);
  vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, kernel.pipeline_layout, 0, 1,
                          &kernel.set, 0, nullptr);
  vkCmdPushConstants(commands, kernel.pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, push_bytes,
                     push);
  vkCmdDispatch(commands, (record_count + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE, 1, 1);

  VkMemoryBarrier to_host{};
  to_host.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  to_host.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  to_host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                       0, 1, &to_host, 0, nullptr, 0, nullptr);
  submit_and_wait(commands);
}

void vulkan_batch_solver_t::write_acceleration_structure_descriptor(
    VkWriteDescriptorSet &write, VkWriteDescriptorSetAccelerationStructureKHR &info,
    VkDescriptorSet set) const
{
  info = {};
  info.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
  info.accelerationStructureCount = 1;
  info.pAccelerationStructures = &top_level;

  write = {};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.pNext = &info;
  write.dstSet = set;
  write.dstBinding = 0;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
}

// --- The scene ----------------------------------------------------------------

void vulkan_batch_solver_t::build_acceleration_structures(uint32_t vertex_count,
                                                          uint32_t triangle_count)
{
  // The BLAS: the driver's own BVH over the vertex and index buffers as they
  // are. Opaque, so a query never asks a shader whether a hit counts.
  VkAccelerationStructureGeometryKHR geometry{};
  geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
  geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
  geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
  VkAccelerationStructureGeometryTrianglesDataKHR &triangle_data = geometry.geometry.triangles;
  triangle_data.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
  triangle_data.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
  triangle_data.vertexData.deviceAddress = vertices.address;
  triangle_data.vertexStride = sizeof(linalg::vec4);
  triangle_data.maxVertex = vertex_count - 1;
  triangle_data.indexType = VK_INDEX_TYPE_UINT32;
  triangle_data.indexData.deviceAddress = indices.address;

  const auto build_one = [&](VkAccelerationStructureTypeKHR type,
                             const VkAccelerationStructureGeometryKHR &what,
                             uint32_t primitive_count, gpu_buffer_t &storage,
                             VkAccelerationStructureKHR &out_structure) {
    VkAccelerationStructureBuildGeometryInfoKHR build{};
    build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build.type = type;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount = 1;
    build.pGeometries = &what;

    VkAccelerationStructureBuildSizesInfoKHR sizes{};
    sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    device.get_acceleration_structure_build_sizes(
        device.device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build,
        &primitive_count, &sizes);

    storage = create_buffer(sizes.accelerationStructureSize,
                            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkAccelerationStructureCreateInfoKHR create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    create_info.buffer = storage.buffer;
    create_info.size = sizes.accelerationStructureSize;
    create_info.type = type;
    check(device.create_acceleration_structure(device.device, &create_info, nullptr,
                                               &out_structure),
          "vkCreateAccelerationStructureKHR");

    // Over-allocated by the alignment the device wants for its scratch, since
    // a fresh buffer's address promises nothing about it.
    gpu_buffer_t scratch = create_buffer(sizes.buildScratchSize + scratch_alignment,
                                         VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                             VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    build.dstAccelerationStructure = out_structure;
    build.scratchData.deviceAddress = align_up(scratch.address, scratch_alignment);

    VkAccelerationStructureBuildRangeInfoKHR range{};
    range.primitiveCount = primitive_count;
    const VkAccelerationStructureBuildRangeInfoKHR *ranges = &range;

    VkCommandBuffer commands = begin_commands();
    device.cmd_build_acceleration_structures(commands, 1, &build, &ranges);
    submit_and_wait(commands);
    destroy_buffer(scratch);
  };

  build_one(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, geometry, triangle_count,
            bottom_level_storage, bottom_level);

  // The TLAS: one instance of that BLAS under the identity, both faces hittable
  // -- the CPU's slab test has no winding either.
  VkAccelerationStructureDeviceAddressInfoKHR address_info{};
  address_info.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
  address_info.accelerationStructure = bottom_level;

  VkAccelerationStructureInstanceKHR instance_record{};
  instance_record.transform.matrix[0][0] = 1.f;
  instance_record.transform.matrix[1][1] = 1.f;
  instance_record.transform.matrix[2][2] = 1.f;
  instance_record.mask = 0xff;
  instance_record.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
  instance_record.accelerationStructureReference =
      device.get_acceleration_structure_device_address(device.device, &address_info);
  instance = upload_device_local(
      &instance_record, sizeof(instance_record),
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
          VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);

  VkAccelerationStructureGeometryKHR instances{};
  instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
  instances.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
  instances.geometry.instances.sType =
      VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
  instances.geometry.instances.arrayOfPointers = VK_FALSE;
  instances.geometry.instances.data.deviceAddress = instance.address;

  build_one(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, instances, 1, top_level_storage,
            top_level);
}

void vulkan_batch_solver_t::upload_scene(const shared::batch_solver_scene_t &scene)
{
  destroy_scene();
  accumulated = {};
  settings = scene.settings;

  if (!scene.gpu_scene)
    fatal_error("[lightmap-gpu] upload_scene with no triangle scene.");
  if (scene.lights.size() > shared::LIGHT_MASK_BITS)
    fatal_error("[lightmap-gpu] {} baked lights, and a chart's light mask holds {} bits.",
                scene.lights.size(), shared::LIGHT_MASK_BITS);
  const shared::gpu_bake_scene_t &gpu = *scene.gpu_scene;
  if (gpu.triangles.empty() || gpu.vertices.empty())
  {
    log_error("[lightmap-gpu] the scene has no triangles; there is nothing to build an "
              "acceleration structure over.");
    return;
  }

  const VkBufferUsageFlags geometry_usage =
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
      VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
  vertices = upload_device_local(gpu.vertices.data(),
                                 gpu.vertices.size() * sizeof(linalg::vec4), geometry_usage);
  indices = upload_device_local(gpu.indices.data(), gpu.indices.size() * sizeof(uint32_t),
                                geometry_usage);
  triangles = upload_device_local(gpu.triangles.data(),
                                  gpu.triangles.size() * sizeof(shared::gpu_triangle_t),
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  materials = upload_device_local(gpu.materials.data(),
                                  gpu.materials.size() * sizeof(shared::gpu_material_t),
                                  VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

  // The lights in the kernel's struct, filled by the same rule as the runtime's
  // scene block. A map with none still gets a buffer to bind; the kernel's loop
  // runs to light_count and never reads it.
  std::vector<gpu_bake_light_t> light_records;
  light_records.reserve(scene.lights.size());
  for (uint32_t slot = 0; slot < scene.lights.size(); ++slot)
    light_records.push_back(bake_light_from(scene.lights[slot], slot));
  lights = upload_device_local(light_records.data(),
                               light_records.size() * sizeof(gpu_bake_light_t),
                               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
  uploaded_light_count = (uint32_t)light_records.size();

  // An absent albedo reads as the untextured grey and an absent emissive as
  // black, decided in the kernel off GPU_NO_TEXTURE; nothing is bound for it.
  upload_textures(gpu.textures);

  build_acceleration_structures((uint32_t)gpu.vertices.size(), (uint32_t)gpu.triangles.size());
  uploaded_triangle_count = (uint32_t)gpu.triangles.size();
}

void vulkan_batch_solver_t::destroy_scene()
{
  if (top_level) device.destroy_acceleration_structure(device.device, top_level, nullptr);
  if (bottom_level)
    device.destroy_acceleration_structure(device.device, bottom_level, nullptr);
  top_level = VK_NULL_HANDLE;
  bottom_level = VK_NULL_HANDLE;
  destroy_buffer(top_level_storage);
  destroy_buffer(bottom_level_storage);
  destroy_buffer(instance);
  destroy_buffer(vertices);
  destroy_buffer(indices);
  destroy_buffer(triangles);
  destroy_buffer(materials);
  destroy_buffer(lights);
  destroy_textures();
  uploaded_triangle_count = 0;
  uploaded_light_count = 0;
}

// --- The kernels --------------------------------------------------------------

compute_kernel_t vulkan_batch_solver_t::create_kernel(const uint32_t *spirv, size_t spirv_bytes,
                                                      Span<const kernel_binding_t> bindings,
                                                      uint32_t push_bytes, const char *name)
{
  compute_kernel_t kernel;

  std::vector<VkDescriptorSetLayoutBinding> layout_bindings(bindings.size());
  std::vector<VkDescriptorBindingFlags> binding_flags(bindings.size(), 0);
  for (uint32_t i = 0; i < bindings.size(); ++i)
  {
    layout_bindings[i].binding = i;
    layout_bindings[i].descriptorType = bindings[i].type;
    layout_bindings[i].descriptorCount = bindings[i].count;
    layout_bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    if (bindings[i].count > 1) binding_flags[i] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
  }

  VkDescriptorSetLayoutBindingFlagsCreateInfo flags_info{};
  flags_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
  flags_info.bindingCount = (uint32_t)binding_flags.size();
  flags_info.pBindingFlags = binding_flags.data();

  VkDescriptorSetLayoutCreateInfo layout_info{};
  layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layout_info.pNext = &flags_info;
  layout_info.bindingCount = (uint32_t)layout_bindings.size();
  layout_info.pBindings = layout_bindings.data();
  check(vkCreateDescriptorSetLayout(device.device, &layout_info, nullptr, &kernel.set_layout),
        "vkCreateDescriptorSetLayout");

  VkPushConstantRange push_range{};
  push_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  push_range.size = push_bytes;

  VkPipelineLayoutCreateInfo pipeline_layout_info{};
  pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_info.setLayoutCount = 1;
  pipeline_layout_info.pSetLayouts = &kernel.set_layout;
  pipeline_layout_info.pushConstantRangeCount = 1;
  pipeline_layout_info.pPushConstantRanges = &push_range;
  check(vkCreatePipelineLayout(device.device, &pipeline_layout_info, nullptr,
                               &kernel.pipeline_layout),
        "vkCreatePipelineLayout");

  VkShaderModuleCreateInfo module_info{};
  module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  module_info.codeSize = spirv_bytes;
  module_info.pCode = spirv;
  VkShaderModule module = VK_NULL_HANDLE;
  if (vkCreateShaderModule(device.device, &module_info, nullptr, &module) != VK_SUCCESS)
    fatal_error("[lightmap-gpu] vkCreateShaderModule failed for {}.", name);

  VkComputePipelineCreateInfo pipeline_info{};
  pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipeline_info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  pipeline_info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  pipeline_info.stage.module = module;
  pipeline_info.stage.pName = "main";
  pipeline_info.layout = kernel.pipeline_layout;
  if (vkCreateComputePipelines(device.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr,
                               &kernel.pipeline) != VK_SUCCESS)
    fatal_error("[lightmap-gpu] vkCreateComputePipelines failed for {}.", name);
  vkDestroyShaderModule(device.device, module, nullptr);

  VkDescriptorSetAllocateInfo set_info{};
  set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  set_info.descriptorPool = descriptor_pool;
  set_info.descriptorSetCount = 1;
  set_info.pSetLayouts = &kernel.set_layout;
  if (vkAllocateDescriptorSets(device.device, &set_info, &kernel.set) != VK_SUCCESS)
    fatal_error("[lightmap-gpu] vkAllocateDescriptorSets failed for {}.", name);

  return kernel;
}

void vulkan_batch_solver_t::destroy_kernel(compute_kernel_t &kernel) const
{
  if (kernel.pipeline) vkDestroyPipeline(device.device, kernel.pipeline, nullptr);
  if (kernel.pipeline_layout)
    vkDestroyPipelineLayout(device.device, kernel.pipeline_layout, nullptr);
  if (kernel.set_layout) vkDestroyDescriptorSetLayout(device.device, kernel.set_layout, nullptr);
  kernel = {};
}

void vulkan_batch_solver_t::create_kernels()
{
  // One pool for every kernel this solver has: the probe's three bindings, the
  // indirect kernel's eight plus its texture array, the direct kernel's five.
  VkDescriptorPoolSize pool_sizes[3]{};
  pool_sizes[0].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
  pool_sizes[0].descriptorCount = 3;
  pool_sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  pool_sizes[1].descriptorCount = 13;
  pool_sizes[2].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  pool_sizes[2].descriptorCount = texture_descriptor_capacity;
  VkDescriptorPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.maxSets = 3;
  pool_info.poolSizeCount = 3;
  pool_info.pPoolSizes = pool_sizes;
  check(vkCreateDescriptorPool(device.device, &pool_info, nullptr, &descriptor_pool),
        "vkCreateDescriptorPool");

  const kernel_binding_t probe_bindings[] = {{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
                                             {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
                                             {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
  probe_kernel = create_kernel(lightmap_probe_ray_comp_spv, sizeof(lightmap_probe_ray_comp_spv),
                               Span<const kernel_binding_t>(probe_bindings),
                               sizeof(probe_ray_push_t), "lightmap_probe_ray.comp");

  // lightmap_indirect.comp's bindings, in order: the TLAS, samples, results,
  // vertices, indices, triangles, materials, lights, then the texture array.
  const kernel_binding_t indirect_bindings[] = {
      {VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, texture_descriptor_capacity}};
  indirect_kernel = create_kernel(lightmap_indirect_comp_spv, sizeof(lightmap_indirect_comp_spv),
                                  Span<const kernel_binding_t>(indirect_bindings),
                                  sizeof(bake_push_t), "lightmap_indirect.comp");

  // lightmap_direct.comp's bindings, in order: the TLAS, samples, results, the
  // chart light masks, lights. Shadow rays ask only whether something is in the
  // way, so no triangle, material or texture reaches it.
  const kernel_binding_t direct_bindings[] = {{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1},
                                              {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
                                              {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
                                              {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1},
                                              {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}};
  direct_kernel = create_kernel(lightmap_direct_comp_spv, sizeof(lightmap_direct_comp_spv),
                                Span<const kernel_binding_t>(direct_bindings),
                                sizeof(bake_push_t), "lightmap_direct.comp");
}

void vulkan_batch_solver_t::solve_direct(Span<const shared::gpu_sample_t> samples,
                                         Span<const uint64_t> chart_light_masks,
                                         shared::gpu_direct_results_t &out)
{
  if (!has_scene()) fatal_error("[lightmap-gpu] solve_direct before a scene was uploaded.");

  const size_t light_count = uploaded_light_count;
  out.resize(samples.size(), light_count);
  ++accumulated.direct_dispatches;
  if (samples.size() == 0) return;
  if (chart_light_masks.size() == 0)
    fatal_error("[lightmap-gpu] solve_direct over {} records with no chart masks.",
                samples.size());
  for (uint32_t i = 0; i < samples.size(); ++i)
    if (samples[i].chart_index >= chart_light_masks.size())
      fatal_error("[lightmap-gpu] record {} names chart {} of a batch carrying {} chart masks.",
                  i, samples[i].chart_index, chart_light_masks.size());

  const size_t records_per_dispatch =
      direct_records_per_dispatch(light_count, settings.soft_shadow_samples);
  const size_t largest = std::min<size_t>(records_per_dispatch, samples.size());
  const size_t floats_per_record = direct_result_floats_per_record(light_count);
  ensure_host_visible(dispatch_samples, (VkDeviceSize)largest * sizeof(shared::gpu_sample_t));
  ensure_host_visible(dispatch_results,
                      (VkDeviceSize)largest * floats_per_record * sizeof(float));

  // A uint64_t per chart IS the uvec2 the kernel reads, low word first.
  const VkDeviceSize masks_bytes = (VkDeviceSize)chart_light_masks.size() * sizeof(uint64_t);
  ensure_host_visible(dispatch_chart_light_masks, masks_bytes);
  write_host_visible(dispatch_chart_light_masks, chart_light_masks.data, masks_bytes);

  VkWriteDescriptorSetAccelerationStructureKHR structure_write{};
  const VkDescriptorBufferInfo buffer_infos[] = {{dispatch_samples.buffer, 0, VK_WHOLE_SIZE},
                                                 {dispatch_results.buffer, 0, VK_WHOLE_SIZE},
                                                 {dispatch_chart_light_masks.buffer, 0, VK_WHOLE_SIZE},
                                                 {lights.buffer, 0, VK_WHOLE_SIZE}};
  VkWriteDescriptorSet writes[5]{};
  write_acceleration_structure_descriptor(writes[0], structure_write, direct_kernel.set);
  for (uint32_t i = 0; i < 4; ++i)
    writes[1 + i] = buffer_write(direct_kernel.set, 1 + i, &buffer_infos[i]);
  vkUpdateDescriptorSets(device.device, 5, writes, 0, nullptr);

  std::vector<direct_record_result_t> record_results;
  for (size_t first = 0; first < samples.size(); first += records_per_dispatch)
  {
    const size_t count = std::min<size_t>(records_per_dispatch, samples.size() - first);
    write_host_visible(dispatch_samples, samples.data + first,
                       (VkDeviceSize)count * sizeof(shared::gpu_sample_t));

    bake_push_t push;
    push.sample_count = (uint32_t)count;
    push.light_count = (uint32_t)light_count;
    push.settings = settings;
    dispatch_and_wait(direct_kernel, &push, sizeof(push), (uint32_t)count);

    const VkDeviceSize results_bytes = (VkDeviceSize)count * floats_per_record * sizeof(float);
    void *mapped = nullptr;
    check(vkMapMemory(device.device, dispatch_results.memory, 0, results_bytes, 0, &mapped),
          "vkMapMemory (direct results)");
    const float *floats = (const float *)mapped;

    record_results.resize(count);
    std::memcpy(record_results.data(), floats, count * sizeof(direct_record_result_t));
    for (size_t i = 0; i < count; ++i)
    {
      const direct_record_result_t &record = record_results[i];
      out.irradiance[first + i] = {record.irradiance[0], record.irradiance[1],
                                   record.irradiance[2]};
      accumulated.shade.direct_rays += (size_t)(record.rays_cast + 0.5f);
    }
    const size_t per_light_floats = count * light_count;
    std::memcpy(out.coverage.data() + first * light_count, floats + count * 4,
                per_light_floats * sizeof(float));
    std::memcpy(out.weight.data() + first * light_count, floats + count * 4 + per_light_floats,
                per_light_floats * sizeof(float));
    vkUnmapMemory(device.device, dispatch_results.memory);
  }
}

void vulkan_batch_solver_t::solve_indirect(Span<const shared::gpu_sample_t> samples,
                                           shared::gpu_indirect_results_t &out)
{
  if (!has_scene()) fatal_error("[lightmap-gpu] solve_indirect before a scene was uploaded.");

  out.values.assign(samples.size(), shared::indirect_sh_l1_t{});
  ++accumulated.indirect_dispatches;
  accumulated.shade.chains +=
      (size_t)samples.size() * (size_t)std::max(settings.rays_per_sample, 0);
  if (samples.size() == 0) return;

  const size_t records_per_dispatch = indirect_records_per_dispatch(settings.rays_per_sample);
  const size_t largest = std::min<size_t>(records_per_dispatch, samples.size());
  ensure_host_visible(dispatch_samples, (VkDeviceSize)largest * sizeof(shared::gpu_sample_t));
  ensure_host_visible(dispatch_results, (VkDeviceSize)largest * sizeof(shared::indirect_sh_l1_t));

  // The scene half of the set is written once; the two dispatch buffers are
  // the same buffers every time, so their bindings hold too. Ranges are whole
  // buffers -- a dispatch reads only its first sample_count records. The
  // texture array is written as far as the scene fills it and no further: the
  // binding is partially bound, and nothing indexes past the scene's count.
  VkWriteDescriptorSetAccelerationStructureKHR structure_write{};
  const VkDescriptorBufferInfo buffer_infos[] = {
      {dispatch_samples.buffer, 0, VK_WHOLE_SIZE}, {dispatch_results.buffer, 0, VK_WHOLE_SIZE},
      {vertices.buffer, 0, VK_WHOLE_SIZE},         {indices.buffer, 0, VK_WHOLE_SIZE},
      {triangles.buffer, 0, VK_WHOLE_SIZE},        {materials.buffer, 0, VK_WHOLE_SIZE},
      {lights.buffer, 0, VK_WHOLE_SIZE}};
  std::vector<VkDescriptorImageInfo> image_infos(texture_images.size());
  for (size_t i = 0; i < texture_images.size(); ++i)
    image_infos[i] = {VK_NULL_HANDLE, texture_images[i].view,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

  VkWriteDescriptorSet writes[9]{};
  write_acceleration_structure_descriptor(writes[0], structure_write, indirect_kernel.set);
  for (uint32_t i = 0; i < 7; ++i)
    writes[1 + i] = buffer_write(indirect_kernel.set, 1 + i, &buffer_infos[i]);
  uint32_t write_count = 8;
  if (!image_infos.empty())
  {
    VkWriteDescriptorSet &write = writes[8];
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = indirect_kernel.set;
    write.dstBinding = 8;
    write.descriptorCount = (uint32_t)image_infos.size();
    write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    write.pImageInfo = image_infos.data();
    write_count = 9;
  }
  vkUpdateDescriptorSets(device.device, write_count, writes, 0, nullptr);

  for (size_t first = 0; first < samples.size(); first += records_per_dispatch)
  {
    const size_t count = std::min<size_t>(records_per_dispatch, samples.size() - first);
    const VkDeviceSize samples_bytes = (VkDeviceSize)count * sizeof(shared::gpu_sample_t);
    const VkDeviceSize results_bytes = (VkDeviceSize)count * sizeof(shared::indirect_sh_l1_t);
    write_host_visible(dispatch_samples, samples.data + first, samples_bytes);

    bake_push_t push;
    push.sample_count = (uint32_t)count;
    push.light_count = uploaded_light_count;
    push.settings = settings;
    dispatch_and_wait(indirect_kernel, &push, sizeof(push), (uint32_t)count);

    void *mapped = nullptr;
    check(vkMapMemory(device.device, dispatch_results.memory, 0, results_bytes, 0, &mapped),
          "vkMapMemory (indirect results)");
    std::memcpy(out.values.data() + first, mapped, (size_t)results_bytes);
    vkUnmapMemory(device.device, dispatch_results.memory);
  }
}

void vulkan_batch_solver_t::probe_rays(Span<const shared::gpu_sample_t> samples,
                                       float ray_bias, float max_distance,
                                       std::vector<float> &out_distances)
{
  if (!has_scene()) fatal_error("[lightmap-gpu] probe_rays before a scene was uploaded.");

  out_distances.assign(samples.size(), -1.f);
  if (samples.size() == 0) return;

  const VkDeviceSize samples_bytes = (VkDeviceSize)samples.size() * sizeof(shared::gpu_sample_t);
  const VkDeviceSize results_bytes = (VkDeviceSize)samples.size() * sizeof(float);
  ensure_host_visible(dispatch_samples, samples_bytes);
  ensure_host_visible(dispatch_results, results_bytes);
  write_host_visible(dispatch_samples, samples.data, samples_bytes);

  VkWriteDescriptorSetAccelerationStructureKHR structure_write{};
  const VkDescriptorBufferInfo sample_info{dispatch_samples.buffer, 0, samples_bytes};
  const VkDescriptorBufferInfo result_info{dispatch_results.buffer, 0, results_bytes};

  VkWriteDescriptorSet writes[3]{};
  write_acceleration_structure_descriptor(writes[0], structure_write, probe_kernel.set);
  writes[1] = buffer_write(probe_kernel.set, 1, &sample_info);
  writes[2] = buffer_write(probe_kernel.set, 2, &result_info);
  vkUpdateDescriptorSets(device.device, 3, writes, 0, nullptr);

  probe_ray_push_t push;
  push.sample_count = samples.size();
  push.ray_bias = ray_bias;
  push.max_distance = max_distance;
  dispatch_and_wait(probe_kernel, &push, sizeof(push), samples.size());

  void *mapped = nullptr;
  check(vkMapMemory(device.device, dispatch_results.memory, 0, results_bytes, 0, &mapped),
        "vkMapMemory (probe results)");
  std::memcpy(out_distances.data(), mapped, (size_t)results_bytes);
  vkUnmapMemory(device.device, dispatch_results.memory);
}

} // namespace client
