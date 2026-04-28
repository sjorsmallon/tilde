#pragma once

#include <string>
#include <vector>
#include <vulkan/vulkan.h>

#include "../shared/asset.hpp"
#include "renderer.hpp"

namespace client
{

// Compile a GLSL shader source file to SPIR-V using glslc.
// Returns true on success; on failure, error_output contains glslc stderr.
bool compile_shader_to_spv(const std::string &source_path,
                           const std::string &output_path,
                           const std::string &include_dir,
                           std::string &error_output);

static constexpr int PBR_TEXTURE_SLOT_COUNT = 6;
static constexpr int PREVIEW_MAX_FRAMES_IN_FLIGHT = 2;

struct preview_pipeline_t
{
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
  VkShaderModule vert_module = VK_NULL_HANDLE;
  VkShaderModule frag_module = VK_NULL_HANDLE;

  // One UBO + descriptor set per frame in flight to avoid GPU/CPU race conditions.
  VkBuffer ubo_buffer[PREVIEW_MAX_FRAMES_IN_FLIGHT] = {};
  VkDeviceMemory ubo_memory[PREVIEW_MAX_FRAMES_IN_FLIGHT] = {};
  void *ubo_mapped[PREVIEW_MAX_FRAMES_IN_FLIGHT] = {};
  VkDescriptorSet descriptor_set[PREVIEW_MAX_FRAMES_IN_FLIGHT] = {};

  // PBR texture slots: set=0, bindings 1–6 (albedo, normal, roughness, ao, metallic, height)
  renderer::gpu_texture_t pbr_textures[PBR_TEXTURE_SLOT_COUNT] = {};
  renderer::gpu_texture_t fallback_white_texture;
};

// Create the descriptor set layout + pipeline layout + descriptor pool/set + UBO.
// These are stable across shader reloads.
bool create_preview_resources(VkDevice device, VkPhysicalDevice physical_device,
                              preview_pipeline_t &out);

// Create or recreate the pipeline from SPIR-V files.
// Destroys old pipeline + shader modules if they exist.
bool create_preview_pipeline_from_spv(VkDevice device,
                                      VkRenderPass render_pass,
                                      const std::string &vert_spv_path,
                                      const std::string &frag_spv_path,
                                      preview_pipeline_t &pipeline);

// Destroy the pipeline and shader modules only (for hot reload).
void destroy_preview_pipeline(VkDevice device, preview_pipeline_t &pipeline);

// Destroy everything (pipeline, layout, descriptors, UBO, textures).
void destroy_preview_resources(VkDevice device, preview_pipeline_t &pipeline);

// Upload PBR textures and write them into descriptor set bindings 1–6.
// Call after create_preview_resources() succeeds and after loading the pbr_material.
// Slots for invalid texture handles fall back to a 1x1 white texture.
void bind_pbr_textures(VkDevice device, preview_pipeline_t &pipeline,
                       const assets::pbr_material_asset_t &material);

// Get the VkPhysicalDevice from the renderer.
VkPhysicalDevice get_physical_device();

} // namespace client
