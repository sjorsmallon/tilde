#pragma once

#include "renderer.hpp"

#include <vulkan/vulkan.h>

// What the renderer's translation units share with each other and with nothing
// outside src/client/renderer/. A name is here because two of them use it;
// everything a single file owns stays static in that file.

namespace client
{
namespace renderer
{

// How many frames the CPU may run ahead of the GPU. Anything sized per
// frame-in-flight -- command buffers, sync objects, the per-frame uniform
// allocator -- is sized by this.
constexpr int MAX_FRAMES_IN_FLIGHT = 2;

extern VkDevice      g_device;
extern VkQueue       g_graphics_queue;
extern VkCommandPool g_command_pool;
extern VkRenderPass g_scene_render_pass;
extern uint32_t     g_current_frame_idx_in_swapchain;

void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                   VkBuffer &buffer, VkDeviceMemory &bufferMemory);

// The scene pass's blend states: the pipeline's own for the HDR colour, then every data image, written whole or left alone.
using scene_blend_attachments_t =
    Array<VkPipelineColorBlendAttachmentState, SCENE_COLOR_ATTACHMENT_COUNT>;
scene_blend_attachments_t scene_blend_attachments(const VkPipelineColorBlendAttachmentState& color,
                                                  bool writes_data_images);

// uploads.cpp
VkCommandBuffer pending_upload_commands();
void            free_after_upload(VkBuffer buffer, VkDeviceMemory memory);
void            flush_pending_uploads();

// retired_buffers.cpp
void retire_buffer(VkBuffer buffer, VkDeviceMemory memory);
void destroy_buffers_retired_in_frame_slot(uint32_t frame_slot);
void destroy_all_retired_buffers();

// debug_draw.cpp
void create_debug_resources();
void destroy_debug_resources();
void reset_debug_vertices();
void record_debug_lines(VkCommandBuffer cmd, const debug_draw_list_t &debug,
                        const linalg::mat4f &view_projection_matrix, const linalg::vec3f &camera_position);
void record_debug_polygons(VkCommandBuffer cmd, const debug_draw_list_t &debug,
                           const linalg::mat4f &view_projection_matrix, const linalg::vec3f &camera_position,
                           bool opaque_pass);

} // namespace renderer
} // namespace client
