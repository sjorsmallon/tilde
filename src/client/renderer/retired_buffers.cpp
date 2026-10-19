#include "renderer/internal.hpp"

#include <vector>

// A replaced buffer may still be read by a frame in flight. It waits in the slot
// it was retired in and is destroyed when that slot's fence is next waited, by
// which point every frame that could have recorded it has completed.

namespace client
{
namespace renderer
{

namespace
{

struct retired_buffer_t
{
  VkBuffer       buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
};

Array<std::vector<retired_buffer_t>, MAX_FRAMES_IN_FLIGHT> g_retired_buffers;

void destroy_retired_buffers(std::vector<retired_buffer_t>& retired)
{
  for (const retired_buffer_t& entry : retired)
  {
    if (entry.buffer) vkDestroyBuffer(g_device, entry.buffer, nullptr);
    if (entry.memory) vkFreeMemory(g_device, entry.memory, nullptr);
  }
  retired.clear();
}

} // namespace

void retire_buffer(VkBuffer buffer, VkDeviceMemory memory)
{
  if (buffer == VK_NULL_HANDLE && memory == VK_NULL_HANDLE)
    return;
  g_retired_buffers[g_current_frame_idx_in_swapchain].push_back({buffer, memory});
}

void destroy_buffers_retired_in_frame_slot(uint32_t frame_slot)
{
  destroy_retired_buffers(g_retired_buffers[frame_slot]);
}

void destroy_all_retired_buffers()
{
  for (std::vector<retired_buffer_t>& retired : g_retired_buffers)
    destroy_retired_buffers(retired);
}

} // namespace renderer
} // namespace client
