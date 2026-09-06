#pragma once

// The Vulkan implementation of the bake's solver seam (lightmap_gpu_plan.md
// step 4): the step-2 triangle scene uploaded as buffers, a BLAS the driver
// builds over them, one TLAS holding it under an identity instance, every
// texture at its own size under one descriptor array, and a compute pipeline
// per kernel. It dispatches on the
// renderer's device with its own command buffer and fence -- the
// end_single_time_commands shape -- so the frame in flight never sees it.
//
// Step 4 delivered a ray that hits something: probe_rays fires one ray along
// every record's normal and hands back the hit distance, and
// shared::probe_ray_distances answers the same rays through the occluder BVH.
// Where the two disagree is where a wrong stride, a wrong winding or an unbuilt
// TLAS shows up as a number rather than as a dark bake three steps later.
//
// Step 5 is the INDIRECT kernel (resources/shaders/lightmap_indirect.comp):
// solve_indirect shades a batch of records through it, in dispatches small
// enough that no single submission can trip the OS's GPU timeout -- a batch is
// millions of records and a record is hundreds of rays, and Windows kills a
// kernel that runs past two seconds. Step 6 is the DIRECT kernel
// (lightmap_direct.comp), cut into dispatches by the same rule over shadow rays.
// Step 7's probe half is a MODE of the indirect kernel (push.probes), so
// solve_probes binds the same set and reads sixteen floats back per record.
// Each one's pin is the Lightmap tool's comparison against cpu_batch_solver_t
// over the same records.

#include "../shared/lightmap_gpu.hpp"
#include "renderer.hpp"

#include <vector>

namespace client
{

struct gpu_buffer_t
{
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceAddress address = 0;
  VkDeviceSize size = 0;

  [[nodiscard]] bool valid() const { return buffer != VK_NULL_HANDLE; }
};

// One texture of the scene at its native size, R8G8B8A8_UINT: the kernel reads
// raw bytes and decodes them itself (lightmap_gpu_plan.md step 5b).
struct gpu_texture_image_t
{
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
};

// One binding of a kernel's set. A count above one is a PARTIALLY BOUND array:
// the layout is sized once and a scene fills the front of it.
struct kernel_binding_t
{
  VkDescriptorType type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  uint32_t count = 1;
};

// One compute kernel: its descriptor set layout, pipeline layout, pipeline and
// the one set it dispatches with, rewritten before every dispatch.
struct compute_kernel_t
{
  VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkDescriptorSet set = VK_NULL_HANDLE;
};

struct vulkan_batch_solver_t final : shared::lightmap_batch_solver_t
{
  // Fatal on a device without ray query: ask renderer::ray_query_is_available()
  // first, and give the reason it reports to whoever pressed the button.
  vulkan_batch_solver_t();
  ~vulkan_batch_solver_t() override;
  vulkan_batch_solver_t(const vulkan_batch_solver_t &) = delete;
  vulkan_batch_solver_t &operator=(const vulkan_batch_solver_t &) = delete;

  // The two per-light result arrays are what size a batch: 512 bytes per sample
  // at 64 lights, so a samples-times-lights budget rather than a sample count.
  static constexpr size_t RESULT_BUDGET_IN_BYTES = (size_t)256 << 20;

  // The most textures one scene may bind, before the device's own limit: the
  // descriptor array's size, fixed at layout creation.
  static constexpr uint32_t MOST_TEXTURES = 1024;

  [[nodiscard]] const char *name() const override { return "the Vulkan ray query solver"; }
  [[nodiscard]] size_t result_budget_in_floats() const override
  {
    return RESULT_BUDGET_IN_BYTES / sizeof(float);
  }
  void upload_scene(const shared::batch_solver_scene_t &scene) override;
  void solve_direct(Span<const shared::gpu_sample_t> samples,
                    Span<const uint64_t> chart_light_masks,
                    shared::gpu_direct_results_t &out) override;
  void solve_indirect(Span<const shared::gpu_sample_t> samples,
                      shared::gpu_indirect_results_t &out) override;
  void solve_probes(Span<const shared::gpu_sample_t> samples,
                    const shared::probe_visibility_slots_t &visibility_slots,
                    shared::gpu_probe_results_t &out) override;
  [[nodiscard]] shared::batch_solve_statistics_t statistics() const override
  {
    return accumulated;
  }

  // Step 4's pin. One ray per record from position + normal * ray_bias along
  // the normal, no further than max_distance; -1 is a miss.
  void probe_rays(Span<const shared::gpu_sample_t> samples, float ray_bias, float max_distance,
                  std::vector<float> &out_distances);

  [[nodiscard]] bool has_scene() const { return top_level != VK_NULL_HANDLE; }
  [[nodiscard]] uint32_t triangle_count() const { return uploaded_triangle_count; }

private:
  renderer::ray_query_device_t device;
  VkPhysicalDeviceMemoryProperties memory_properties{};
  VkDeviceSize scratch_alignment = 256;
  VkFence fence = VK_NULL_HANDLE;
  uint32_t texture_descriptor_capacity = 0;

  gpu_buffer_t vertices;
  gpu_buffer_t indices;
  gpu_buffer_t triangles;
  gpu_buffer_t materials;
  gpu_buffer_t instance;
  gpu_buffer_t bottom_level_storage;
  gpu_buffer_t top_level_storage;
  VkAccelerationStructureKHR bottom_level = VK_NULL_HANDLE;
  VkAccelerationStructureKHR top_level = VK_NULL_HANDLE;
  std::vector<gpu_texture_image_t> texture_images;
  gpu_buffer_t lights;
  uint32_t uploaded_triangle_count = 0;
  uint32_t uploaded_light_count = 0;
  // One bit per slot whose light is Mixed: what a probe needs to know about a
  // light that the kernel's Light struct does not carry.
  uint64_t uploaded_analytic_lights = 0;
  shared::gpu_bake_settings_t settings;

  VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
  compute_kernel_t probe_kernel;
  compute_kernel_t indirect_kernel;
  compute_kernel_t direct_kernel;

  // Host-visible and reused across dispatches: the records of one dispatch in,
  // its answers out, and the batch's chart light masks beside them. Grown to
  // the largest dispatch seen and never shrunk.
  gpu_buffer_t dispatch_samples;
  gpu_buffer_t dispatch_results;
  gpu_buffer_t dispatch_chart_light_masks;

  shared::batch_solve_statistics_t accumulated;

  [[nodiscard]] uint32_t memory_type_for(uint32_t type_bits,
                                         VkMemoryPropertyFlags properties) const;
  [[nodiscard]] gpu_buffer_t create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                           VkMemoryPropertyFlags properties) const;
  void destroy_buffer(gpu_buffer_t &buffer) const;
  void write_host_visible(const gpu_buffer_t &buffer, const void *data, VkDeviceSize size) const;
  [[nodiscard]] gpu_buffer_t upload_device_local(const void *data, VkDeviceSize size,
                                                 VkBufferUsageFlags usage);
  void upload_textures(const std::vector<const assets::texture_asset_t *> &textures);
  void destroy_textures();

  [[nodiscard]] VkCommandBuffer begin_commands() const;
  void submit_and_wait(VkCommandBuffer commands);

  void build_acceleration_structures(uint32_t vertex_count, uint32_t triangle_count);
  void ensure_host_visible(gpu_buffer_t &buffer, VkDeviceSize bytes) const;
  void write_acceleration_structure_descriptor(VkWriteDescriptorSet &write,
                                               VkWriteDescriptorSetAccelerationStructureKHR &info,
                                               VkDescriptorSet set) const;
  void dispatch_and_wait(const compute_kernel_t &kernel, const void *push, uint32_t push_bytes,
                         uint32_t record_count);
  // The indirect kernel's whole set, the scene half and the two dispatch
  // buffers: written before a solve_indirect and before a solve_probes.
  void write_indirect_kernel_descriptors();
  [[nodiscard]] compute_kernel_t create_kernel(const uint32_t *spirv, size_t spirv_bytes,
                                               Span<const kernel_binding_t> bindings,
                                               uint32_t push_bytes, const char *name);
  void destroy_kernel(compute_kernel_t &kernel) const;
  void create_kernels();
  void destroy_scene();
};

} // namespace client
