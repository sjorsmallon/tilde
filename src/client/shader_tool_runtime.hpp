#pragma once

#include <string>
#include <vector>
#include <vulkan/vulkan.h>

namespace client
{

// Compile a GLSL shader source file to SPIR-V using glslc.
// Returns true on success; on failure, error_output contains glslc stderr.
bool compile_shader_to_spv(const std::string &source_path,
                           const std::string &output_path,
                           const std::string &include_dir,
                           std::string &error_output);

struct preview_pipeline_t
{
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptor_set_layout = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
  VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
  VkBuffer ubo_buffer = VK_NULL_HANDLE;
  VkDeviceMemory ubo_memory = VK_NULL_HANDLE;
  VkShaderModule vert_module = VK_NULL_HANDLE;
  VkShaderModule frag_module = VK_NULL_HANDLE;
  void *ubo_mapped = nullptr;
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

// Destroy everything (pipeline, layout, descriptors, UBO).
void destroy_preview_resources(VkDevice device, preview_pipeline_t &pipeline);

// Get the VkPhysicalDevice from the renderer.
VkPhysicalDevice get_physical_device();

} // namespace client
