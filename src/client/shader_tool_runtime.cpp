#include "shader_tool_runtime.hpp"
#include "log.hpp"
#include "renderer.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <print>
#include <string>
#include <vector>

#include "../shared/vertex.hpp"

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

namespace client
{

// ---------------------------------------------------------------------------
// glslc subprocess
// ---------------------------------------------------------------------------

bool compile_shader_to_spv(const std::string &source_path,
                           const std::string &output_path,
                           const std::string &include_dir,
                           std::string &error_output)
{
  std::string command = "glslc \"" + source_path + "\" -o \"" + output_path +
                        "\" --target-env=vulkan1.0";
  if (!include_dir.empty())
  {
    command += " -I\"" + include_dir + "\"";
  }
  command += " 2>&1";

  FILE *pipe = popen(command.c_str(), "r");
  if (!pipe)
  {
    error_output = "Failed to launch glslc process";
    log_error("{}", error_output);
    return false;
  }

  error_output.clear();
  char buffer[256];
  while (fgets(buffer, sizeof(buffer), pipe))
  {
    error_output += buffer;
  }

  int exit_code = pclose(pipe);
  if (exit_code != 0)
  {
    log_warning("glslc compilation failed for '{}': {}", source_path,
                error_output);
    return false;
  }

  return true;
}

// ---------------------------------------------------------------------------
// SPIR-V file loading
// ---------------------------------------------------------------------------

static std::vector<uint32_t> read_spv_file(const std::string &path)
{
  std::ifstream file(path, std::ios::ate | std::ios::binary);
  if (!file.is_open())
  {
    log_error("Failed to open SPIR-V file: {}", path);
    return {};
  }

  size_t file_size = static_cast<size_t>(file.tellg());
  if (file_size == 0 || (file_size % 4) != 0)
  {
    log_error("Invalid SPIR-V file size: {} ({})", path, file_size);
    return {};
  }

  std::vector<uint32_t> code(file_size / 4);
  file.seekg(0);
  file.read(reinterpret_cast<char *>(code.data()), file_size);
  return code;
}

// ---------------------------------------------------------------------------
// Vulkan helpers
// ---------------------------------------------------------------------------

static uint32_t find_memory_type(VkPhysicalDevice physical_device,
                                 uint32_t type_filter,
                                 VkMemoryPropertyFlags properties)
{
  VkPhysicalDeviceMemoryProperties memory_properties;
  vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);

  for (uint32_t i = 0; i < memory_properties.memoryTypeCount; i++)
  {
    if ((type_filter & (1 << i)) &&
        (memory_properties.memoryTypes[i].propertyFlags & properties) ==
            properties)
    {
      return i;
    }
  }
  log_error("Failed to find suitable memory type for preview UBO!");
  return 0;
}

// UBO size — must match shader_tool_common.glsl layout
static constexpr int MAX_LIGHTS = 8;

struct gpu_light_t
{
  float position[4];
  float direction[4];
  float color_intensity[4];
  float spot_params[4]; // cos_inner, cos_outer, range, type
};

struct preview_scene_ubo_t
{
  float view[16];
  float projection[16];
  float view_projection[16];
  float camera_position[4];
  float time[4];
  int32_t light_count;
  int32_t debug_flags, _pad1, _pad2;
  gpu_light_t lights[MAX_LIGHTS];
  float param_color[4][4]; // 4 vec4s
  float param_vec4[8][4];  // 8 vec4s
  float param_float[16];
};

static_assert(sizeof(preview_scene_ubo_t) <= 2048,
              "UBO must fit in minUniformBufferMaxRange");

// ---------------------------------------------------------------------------
// Resource creation (stable across hot reloads)
// ---------------------------------------------------------------------------

bool create_preview_resources(VkDevice device,
                              VkPhysicalDevice physical_device,
                              preview_pipeline_t &out)
{
  // --- Descriptor set layout: binding 0 = UBO, bindings 1���6 = PBR texture samplers ---
  VkDescriptorSetLayoutBinding bindings[1 + PBR_TEXTURE_SLOT_COUNT]{};

  bindings[0].binding         = 0;
  bindings[0].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  bindings[0].descriptorCount = 1;
  bindings[0].stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

  for (int i = 0; i < PBR_TEXTURE_SLOT_COUNT; i++)
  {
    bindings[1 + i].binding         = static_cast<uint32_t>(1 + i);
    bindings[1 + i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1 + i].descriptorCount = 1;
    bindings[1 + i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
  }

  VkDescriptorSetLayoutCreateInfo layout_info{};
  layout_info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layout_info.bindingCount = 1 + PBR_TEXTURE_SLOT_COUNT;
  layout_info.pBindings    = bindings;

  if (vkCreateDescriptorSetLayout(device, &layout_info, nullptr,
                                  &out.descriptor_set_layout) != VK_SUCCESS)
  {
    log_error("Failed to create preview descriptor set layout");
    return false;
  }

  // --- Pipeline layout ---
  // Push constant for model matrix
  VkPushConstantRange push_constant{};
  push_constant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  push_constant.offset = 0;
  push_constant.size = 64; // mat4 model

  VkPipelineLayoutCreateInfo pipeline_layout_info{};
  pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_info.setLayoutCount = 1;
  pipeline_layout_info.pSetLayouts = &out.descriptor_set_layout;
  pipeline_layout_info.pushConstantRangeCount = 1;
  pipeline_layout_info.pPushConstantRanges = &push_constant;

  if (vkCreatePipelineLayout(device, &pipeline_layout_info, nullptr,
                             &out.pipeline_layout) != VK_SUCCESS)
  {
    log_error("Failed to create preview pipeline layout");
    return false;
  }

  // --- UBO buffers (one per frame in flight, host-visible, persistently mapped) ---
  for (int i = 0; i < PREVIEW_MAX_FRAMES_IN_FLIGHT; i++)
  {
    VkBufferCreateInfo buffer_info{};
    buffer_info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size        = sizeof(preview_scene_ubo_t);
    buffer_info.usage       = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &buffer_info, nullptr, &out.ubo_buffer[i]) != VK_SUCCESS)
    {
      log_error("Failed to create preview UBO buffer (frame {})", i);
      return false;
    }

    VkMemoryRequirements memory_requirements;
    vkGetBufferMemoryRequirements(device, out.ubo_buffer[i], &memory_requirements);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize  = memory_requirements.size;
    alloc_info.memoryTypeIndex = find_memory_type(
        physical_device, memory_requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(device, &alloc_info, nullptr, &out.ubo_memory[i]) != VK_SUCCESS)
    {
      log_error("Failed to allocate preview UBO memory (frame {})", i);
      return false;
    }

    vkBindBufferMemory(device, out.ubo_buffer[i], out.ubo_memory[i], 0);
    vkMapMemory(device, out.ubo_memory[i], 0, sizeof(preview_scene_ubo_t), 0, &out.ubo_mapped[i]);
  }

  // --- Descriptor pool + sets (one per frame in flight) ---
  VkDescriptorPoolSize pool_sizes[2]{};
  pool_sizes[0].type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  pool_sizes[0].descriptorCount = PREVIEW_MAX_FRAMES_IN_FLIGHT;
  pool_sizes[1].type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  pool_sizes[1].descriptorCount = PBR_TEXTURE_SLOT_COUNT * PREVIEW_MAX_FRAMES_IN_FLIGHT;

  VkDescriptorPoolCreateInfo pool_info{};
  pool_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.poolSizeCount = 2;
  pool_info.pPoolSizes    = pool_sizes;
  pool_info.maxSets       = PREVIEW_MAX_FRAMES_IN_FLIGHT;

  if (vkCreateDescriptorPool(device, &pool_info, nullptr,
                             &out.descriptor_pool) != VK_SUCCESS)
  {
    log_error("Failed to create preview descriptor pool");
    return false;
  }

  VkDescriptorSetLayout layouts[PREVIEW_MAX_FRAMES_IN_FLIGHT];
  for (int i = 0; i < PREVIEW_MAX_FRAMES_IN_FLIGHT; i++)
    layouts[i] = out.descriptor_set_layout;

  VkDescriptorSetAllocateInfo set_alloc_info{};
  set_alloc_info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  set_alloc_info.descriptorPool     = out.descriptor_pool;
  set_alloc_info.descriptorSetCount = PREVIEW_MAX_FRAMES_IN_FLIGHT;
  set_alloc_info.pSetLayouts        = layouts;

  if (vkAllocateDescriptorSets(device, &set_alloc_info, out.descriptor_set) != VK_SUCCESS)
  {
    log_error("Failed to allocate preview descriptor sets");
    return false;
  }

  // Write each frame's UBO into its descriptor set binding 0
  for (int i = 0; i < PREVIEW_MAX_FRAMES_IN_FLIGHT; i++)
  {
    VkDescriptorBufferInfo buffer_descriptor{};
    buffer_descriptor.buffer = out.ubo_buffer[i];
    buffer_descriptor.offset = 0;
    buffer_descriptor.range  = sizeof(preview_scene_ubo_t);

    VkWriteDescriptorSet ubo_write{};
    ubo_write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    ubo_write.dstSet          = out.descriptor_set[i];
    ubo_write.dstBinding      = 0;
    ubo_write.dstArrayElement = 0;
    ubo_write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo_write.descriptorCount = 1;
    ubo_write.pBufferInfo     = &buffer_descriptor;

    vkUpdateDescriptorSets(device, 1, &ubo_write, 0, nullptr);
  }

  // Create a 1x1 white fallback texture and write it to all 6 sampler slots.
  // Vulkan requires all bound descriptor slots to be valid even before real textures are loaded.
  {
    assets::texture_asset_t white{};
    white.width    = 1;
    white.height   = 1;
    white.channels = 4;
    white.pixels   = {0xFF, 0xFF, 0xFF, 0xFF};
    out.fallback_white_texture = renderer::upload_texture(&white);
    if (!out.fallback_white_texture.valid())
    {
      log_error("[shader_tool] Failed to create fallback white texture");
      return false;
    }

    VkDescriptorImageInfo fallback_img{};
    fallback_img.sampler     = out.fallback_white_texture.sampler;
    fallback_img.imageView   = out.fallback_white_texture.view;
    fallback_img.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet sampler_writes[PBR_TEXTURE_SLOT_COUNT]{};
    for (int frame = 0; frame < PREVIEW_MAX_FRAMES_IN_FLIGHT; frame++)
    {
      for (int i = 0; i < PBR_TEXTURE_SLOT_COUNT; i++)
      {
        sampler_writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        sampler_writes[i].dstSet          = out.descriptor_set[frame];
        sampler_writes[i].dstBinding      = static_cast<uint32_t>(1 + i);
        sampler_writes[i].dstArrayElement = 0;
        sampler_writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        sampler_writes[i].descriptorCount = 1;
        sampler_writes[i].pImageInfo      = &fallback_img;
      }
      vkUpdateDescriptorSets(device, PBR_TEXTURE_SLOT_COUNT, sampler_writes, 0, nullptr);
    }
  }

  log_terminal("[shader_tool] Preview resources created (UBO size = {} bytes)",
               sizeof(preview_scene_ubo_t));
  return true;
}

// ---------------------------------------------------------------------------
// Pipeline creation from SPIR-V (called on hot reload)
// ---------------------------------------------------------------------------

bool create_preview_pipeline_from_spv(VkDevice device,
                                      VkRenderPass render_pass,
                                      const std::string &vert_spv_path,
                                      const std::string &frag_spv_path,
                                      preview_pipeline_t &pipeline)
{
  auto vert_code = read_spv_file(vert_spv_path);
  auto frag_code = read_spv_file(frag_spv_path);
  if (vert_code.empty() || frag_code.empty())
  {
    log_error("Failed to read SPIR-V files for preview pipeline");
    return false;
  }

  // Destroy old pipeline + modules if they exist
  destroy_preview_pipeline(device, pipeline);

  // Shader modules
  VkShaderModuleCreateInfo vert_info{};
  vert_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  vert_info.codeSize = vert_code.size() * sizeof(uint32_t);
  vert_info.pCode = vert_code.data();

  if (vkCreateShaderModule(device, &vert_info, nullptr,
                           &pipeline.vert_module) != VK_SUCCESS)
  {
    log_error("Failed to create preview vert shader module");
    return false;
  }

  VkShaderModuleCreateInfo frag_info{};
  frag_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  frag_info.codeSize = frag_code.size() * sizeof(uint32_t);
  frag_info.pCode = frag_code.data();

  if (vkCreateShaderModule(device, &frag_info, nullptr,
                           &pipeline.frag_module) != VK_SUCCESS)
  {
    log_error("Failed to create preview frag shader module");
    vkDestroyShaderModule(device, pipeline.vert_module, nullptr);
    pipeline.vert_module = VK_NULL_HANDLE;
    return false;
  }

  // Pipeline
  VkPipelineShaderStageCreateInfo shader_stages[] = {
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
       VK_SHADER_STAGE_VERTEX_BIT, pipeline.vert_module, "main", nullptr},
      {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
       VK_SHADER_STAGE_FRAGMENT_BIT, pipeline.frag_module, "main", nullptr}};

  // Vertex input: vertex_xnu (position, normal, uv)
  VkVertexInputBindingDescription binding_description{};
  binding_description.binding = 0;
  binding_description.stride = sizeof(vertex_xnu);
  binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  VkVertexInputAttributeDescription attribute_descriptions[3]{};
  attribute_descriptions[0].binding = 0;
  attribute_descriptions[0].location = 0;
  attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attribute_descriptions[0].offset = offsetof(vertex_xnu, position);

  attribute_descriptions[1].binding = 0;
  attribute_descriptions[1].location = 1;
  attribute_descriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
  attribute_descriptions[1].offset = offsetof(vertex_xnu, normal);

  attribute_descriptions[2].binding = 0;
  attribute_descriptions[2].location = 2;
  attribute_descriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
  attribute_descriptions[2].offset = offsetof(vertex_xnu, uv);

  VkPipelineVertexInputStateCreateInfo vertex_input_info{};
  vertex_input_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertex_input_info.vertexBindingDescriptionCount = 1;
  vertex_input_info.pVertexBindingDescriptions = &binding_description;
  vertex_input_info.vertexAttributeDescriptionCount = 3;
  vertex_input_info.pVertexAttributeDescriptions = attribute_descriptions;

  VkPipelineInputAssemblyStateCreateInfo input_assembly{};
  input_assembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  input_assembly.primitiveRestartEnable = VK_FALSE;

  VkPipelineViewportStateCreateInfo viewport_state{};
  viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state.viewportCount = 1;
  viewport_state.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterizer{};
  rasterizer.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizer.depthClampEnable = VK_FALSE;
  rasterizer.rasterizerDiscardEnable = VK_FALSE;
  rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizer.lineWidth = 1.0f;
  rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
  rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizer.depthBiasEnable = VK_FALSE;

  VkPipelineMultisampleStateCreateInfo multisampling{};
  multisampling.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampling.sampleShadingEnable = VK_FALSE;
  multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo depth_stencil{};
  depth_stencil.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depth_stencil.depthTestEnable = VK_TRUE;
  depth_stencil.depthWriteEnable = VK_TRUE;
  depth_stencil.depthCompareOp = VK_COMPARE_OP_LESS;

  VkPipelineColorBlendAttachmentState color_blend_attachment{};
  color_blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  color_blend_attachment.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo color_blending{};
  color_blending.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  color_blending.logicOpEnable = VK_FALSE;
  color_blending.attachmentCount = 1;
  color_blending.pAttachments = &color_blend_attachment;

  VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                     VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamic_state{};
  dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamic_state.dynamicStateCount = 2;
  dynamic_state.pDynamicStates = dynamic_states;

  VkGraphicsPipelineCreateInfo pipeline_info{};
  pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_info.stageCount = 2;
  pipeline_info.pStages = shader_stages;
  pipeline_info.pVertexInputState = &vertex_input_info;
  pipeline_info.pInputAssemblyState = &input_assembly;
  pipeline_info.pViewportState = &viewport_state;
  pipeline_info.pRasterizationState = &rasterizer;
  pipeline_info.pMultisampleState = &multisampling;
  pipeline_info.pDepthStencilState = &depth_stencil;
  pipeline_info.pColorBlendState = &color_blending;
  pipeline_info.pDynamicState = &dynamic_state;
  pipeline_info.layout = pipeline.pipeline_layout;
  pipeline_info.renderPass = render_pass;
  pipeline_info.subpass = 0;

  if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info,
                                nullptr, &pipeline.pipeline) != VK_SUCCESS)
  {
    log_error("Failed to create preview graphics pipeline");
    vkDestroyShaderModule(device, pipeline.vert_module, nullptr);
    vkDestroyShaderModule(device, pipeline.frag_module, nullptr);
    pipeline.vert_module = VK_NULL_HANDLE;
    pipeline.frag_module = VK_NULL_HANDLE;
    return false;
  }

  log_terminal("[shader_tool] Preview pipeline created OK");
  return true;
}

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------

void destroy_preview_pipeline(VkDevice device, preview_pipeline_t &pipeline)
{
  if (pipeline.pipeline != VK_NULL_HANDLE)
  {
    vkDestroyPipeline(device, pipeline.pipeline, nullptr);
    pipeline.pipeline = VK_NULL_HANDLE;
  }
  if (pipeline.vert_module != VK_NULL_HANDLE)
  {
    vkDestroyShaderModule(device, pipeline.vert_module, nullptr);
    pipeline.vert_module = VK_NULL_HANDLE;
  }
  if (pipeline.frag_module != VK_NULL_HANDLE)
  {
    vkDestroyShaderModule(device, pipeline.frag_module, nullptr);
    pipeline.frag_module = VK_NULL_HANDLE;
  }
}

void destroy_preview_resources(VkDevice device, preview_pipeline_t &pipeline)
{
  destroy_preview_pipeline(device, pipeline);

  for (int i = 0; i < PREVIEW_MAX_FRAMES_IN_FLIGHT; i++)
  {
    if (pipeline.ubo_mapped[i])
    {
      vkUnmapMemory(device, pipeline.ubo_memory[i]);
      pipeline.ubo_mapped[i] = nullptr;
    }
    if (pipeline.ubo_buffer[i] != VK_NULL_HANDLE)
    {
      vkDestroyBuffer(device, pipeline.ubo_buffer[i], nullptr);
      pipeline.ubo_buffer[i] = VK_NULL_HANDLE;
    }
    if (pipeline.ubo_memory[i] != VK_NULL_HANDLE)
    {
      vkFreeMemory(device, pipeline.ubo_memory[i], nullptr);
      pipeline.ubo_memory[i] = VK_NULL_HANDLE;
    }
    // descriptor_sets are freed when the pool is destroyed
    pipeline.descriptor_set[i] = VK_NULL_HANDLE;
  }
  if (pipeline.descriptor_pool != VK_NULL_HANDLE)
  {
    vkDestroyDescriptorPool(device, pipeline.descriptor_pool, nullptr);
    pipeline.descriptor_pool = VK_NULL_HANDLE;
  }
  if (pipeline.pipeline_layout != VK_NULL_HANDLE)
  {
    vkDestroyPipelineLayout(device, pipeline.pipeline_layout, nullptr);
    pipeline.pipeline_layout = VK_NULL_HANDLE;
  }
  if (pipeline.descriptor_set_layout != VK_NULL_HANDLE)
  {
    vkDestroyDescriptorSetLayout(device, pipeline.descriptor_set_layout, nullptr);
    pipeline.descriptor_set_layout = VK_NULL_HANDLE;
  }

  // Destroy PBR textures and fallback
  for (int i = 0; i < PBR_TEXTURE_SLOT_COUNT; i++)
    renderer::destroy_texture(pipeline.pbr_textures[i]);
  renderer::destroy_texture(pipeline.fallback_white_texture);
}

// ---------------------------------------------------------------------------
// PBR texture binding
// ---------------------------------------------------------------------------

void bind_pbr_textures(VkDevice device, preview_pipeline_t &pipeline,
                       const assets::pbr_material_asset_t &material)
{
  const assets::asset_handle_t<assets::texture_asset_t> slots[PBR_TEXTURE_SLOT_COUNT] = {
      material.albedo, material.normal, material.roughness,
      material.ambient_occlusion, material.metallic, material.height,
  };

  // Upload each texture; destroy any previously-held GPU texture first.
  for (int i = 0; i < PBR_TEXTURE_SLOT_COUNT; i++)
  {
    renderer::destroy_texture(pipeline.pbr_textures[i]);

    const assets::texture_asset_t *tex = assets::get(slots[i]);
    if (tex)
      pipeline.pbr_textures[i] = renderer::upload_texture(tex);
  }

  // Write descriptor set bindings 1–6 for every frame in flight.
  VkDescriptorImageInfo image_infos[PBR_TEXTURE_SLOT_COUNT]{};
  for (int i = 0; i < PBR_TEXTURE_SLOT_COUNT; i++)
  {
    const renderer::gpu_texture_t &gpu_tex = pipeline.pbr_textures[i].valid()
                                                 ? pipeline.pbr_textures[i]
                                                 : pipeline.fallback_white_texture;
    image_infos[i].sampler     = gpu_tex.sampler;
    image_infos[i].imageView   = gpu_tex.view;
    image_infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  }

  VkWriteDescriptorSet writes[PBR_TEXTURE_SLOT_COUNT]{};
  for (int frame = 0; frame < PREVIEW_MAX_FRAMES_IN_FLIGHT; frame++)
  {
    for (int i = 0; i < PBR_TEXTURE_SLOT_COUNT; i++)
    {
      writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      writes[i].dstSet          = pipeline.descriptor_set[frame];
      writes[i].dstBinding      = static_cast<uint32_t>(1 + i);
      writes[i].dstArrayElement = 0;
      writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      writes[i].descriptorCount = 1;
      writes[i].pImageInfo      = &image_infos[i];
    }
    vkUpdateDescriptorSets(device, PBR_TEXTURE_SLOT_COUNT, writes, 0, nullptr);
  }
  log_terminal("[shader_tool] PBR textures bound to descriptor sets");
}

VkPhysicalDevice get_physical_device()
{
  return renderer::get_VkPhysicalDevice();
}

} // namespace client
