#include "renderer/internal.hpp"

#include "../shared/frame_timing.hpp"
#include "../shared/log.hpp"

#include <vector>

// Every upload of the frame records into ONE command buffer, which is
// submitted once and waited once at the flush. The staging buffers it reads
// are kept until then. Four hundred brush refreshes used to be four hundred
// full queue drains in one frame; now they are one.

namespace client
{
namespace renderer
{

namespace
{

struct staging_buffer_t
{
  VkBuffer       buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
};

VkCommandBuffer               g_pending_upload_commands = VK_NULL_HANDLE;
std::vector<staging_buffer_t> g_staging_to_free_after_upload;

} // namespace

VkCommandBuffer pending_upload_commands()
{
  if (g_pending_upload_commands != VK_NULL_HANDLE)
    return g_pending_upload_commands;

  VkCommandBufferAllocateInfo info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  info.commandPool        = g_command_pool;
  info.commandBufferCount = 1;
  if (vkAllocateCommandBuffers(g_device, &info, &g_pending_upload_commands) != VK_SUCCESS)
    fatal_error("[renderer] could not allocate the upload command buffer");

  VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(g_pending_upload_commands, &begin);
  return g_pending_upload_commands;
}

void free_after_upload(VkBuffer buffer, VkDeviceMemory memory)
{
  g_staging_to_free_after_upload.push_back({buffer, memory});
}

void flush_pending_uploads()
{
  if (g_pending_upload_commands == VK_NULL_HANDLE)
    return;

  FRAME_ZONE("flush_pending_uploads (vkQueueWaitIdle)");

  vkEndCommandBuffer(g_pending_upload_commands);
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.commandBufferCount = 1;
  submit.pCommandBuffers    = &g_pending_upload_commands;
  vkQueueSubmit(g_graphics_queue, 1, &submit, VK_NULL_HANDLE);
  if (vkQueueWaitIdle(g_graphics_queue) != VK_SUCCESS)
    fatal_error("[renderer] the upload batch of {} staging buffers lost the device",
                g_staging_to_free_after_upload.size());
  vkFreeCommandBuffers(g_device, g_command_pool, 1, &g_pending_upload_commands);
  g_pending_upload_commands = VK_NULL_HANDLE;

  for (const staging_buffer_t& staging : g_staging_to_free_after_upload)
  {
    if (staging.buffer) vkDestroyBuffer(g_device, staging.buffer, nullptr);
    if (staging.memory) vkFreeMemory(g_device, staging.memory, nullptr);
  }
  g_staging_to_free_after_upload.clear();
}

} // namespace renderer
} // namespace client
