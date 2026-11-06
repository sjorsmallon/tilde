#include "renderer/internal.hpp"

#include "../../shared/log.hpp"

#include <algorithm>
#include <cstring>

// The shadow volumes a view pass carries, as their planes, in one storage buffer of set 3
// (shadow_volume_data.glsl) read by reveal.glsl's cuts: out of the scene block, whose fixed size was the
// ceiling on how many a pass could carry. Each frame in flight owns a region; a pass appends its records and
// the scene block says where they start.

namespace client
{
namespace renderer
{

namespace
{

constexpr uint32_t SHADOW_VOLUME_CUTS_GEOMETRY_BIT = 1u << 16;

static_assert(shared::MAX_SHADOW_VOLUME_PLANES == 43 && shared::SHADOW_VOLUME_SIDE_SLOTS == 29,
              "scene.glsl's MAX_SHADOW_VOLUME_PLANES and SHADOW_VOLUME_SIDE_SLOTS");

struct gpu_shadow_volume_t
{
  float    planes[shared::MAX_SHADOW_VOLUME_PLANES][4];
  uint32_t light;
  uint32_t counts;
  uint32_t unused[2];
};
static_assert(sizeof(gpu_shadow_volume_t) == 16 * shared::MAX_SHADOW_VOLUME_PLANES + 16,
              "shadow_volume_data.glsl's std430 ShadowVolumeRecord");

// A frame's region holds this many passes' worth at the per-pass limit; past it a pass carries none.
constexpr uint32_t PASSES_PER_FRAME  = 4;
constexpr uint32_t VOLUMES_PER_FRAME = PASSES_PER_FRAME * MAX_SCENE_SHADOW_VOLUMES;

struct storage_buffer_t
{
  VkBuffer       buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  uint8_t*       mapped = nullptr;
  VkDeviceSize   bytes  = 0;
};

storage_buffer_t g_volume_records;

uint32_t g_frame_slot   = 0;
uint32_t g_volumes_used = 0;

void create_storage_buffer(storage_buffer_t& storage, VkDeviceSize bytes)
{
  storage.bytes = bytes;
  create_buffer(bytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, storage.buffer,
                storage.memory);
  if (storage.buffer == VK_NULL_HANDLE || storage.memory == VK_NULL_HANDLE)
    fatal_error("[renderer] could not create a {}-byte shadow volume storage buffer", bytes);
  vkMapMemory(g_device, storage.memory, 0, bytes, 0, (void**)&storage.mapped);
}

void destroy_storage_buffer(storage_buffer_t& storage)
{
  if (storage.mapped)
    vkUnmapMemory(g_device, storage.memory);
  if (storage.buffer)
    vkDestroyBuffer(g_device, storage.buffer, nullptr);
  if (storage.memory)
    vkFreeMemory(g_device, storage.memory, nullptr);
  storage = {};
}

void write_planes(float (*out)[4], Span<const Plane> sides, Span<const Plane> fronts)
{
  for (uint32_t slot = 0; slot < shared::MAX_SHADOW_VOLUME_PLANES; ++slot)
  {
    const bool   side  = slot < shared::SHADOW_VOLUME_SIDE_SLOTS;
    const Plane* plane = nullptr;
    if (side && slot < sides.size())
      plane = &sides[slot];
    else if (!side && slot - shared::SHADOW_VOLUME_SIDE_SLOTS < fronts.size())
      plane = &fronts[slot - shared::SHADOW_VOLUME_SIDE_SLOTS];
    if (plane == nullptr)
    {
      out[slot][0] = out[slot][1] = out[slot][2] = 0.0f;
      out[slot][3] = 1.0e9f;
      continue;
    }
    out[slot][0] = plane->normal.x;
    out[slot][1] = plane->normal.y;
    out[slot][2] = plane->normal.z;
    out[slot][3] = linalg::dot(plane->normal, plane->point);
  }
}

} // namespace

void create_shadow_volume_storage()
{
  create_storage_buffer(g_volume_records, sizeof(gpu_shadow_volume_t) * VOLUMES_PER_FRAME * MAX_FRAMES_IN_FLIGHT);
}

void destroy_shadow_volume_storage()
{
  destroy_storage_buffer(g_volume_records);
}

VkDescriptorBufferInfo get_shadow_volume_storage_descriptor()
{
  return VkDescriptorBufferInfo{g_volume_records.buffer, 0, g_volume_records.bytes};
}

void begin_shadow_volume_storage_frame(uint32_t frame_slot)
{
  g_frame_slot   = frame_slot;
  g_volumes_used = 0;
}

written_shadow_volumes_t write_shadow_volumes(Span<const shared::shadow_volume_t> volumes)
{
  const uint32_t first_volume = g_frame_slot * VOLUMES_PER_FRAME + g_volumes_used;
  if (volumes.empty())
    return {.first_volume = first_volume, .count = 0};

  static uint32_t most_volumes_warned_about = MAX_SCENE_SHADOW_VOLUMES;
  if (volumes.size() > most_volumes_warned_about)
  {
    most_volumes_warned_about = volumes.size();
    log_warning("[renderer] this view pass carries {} shadow volumes; only the first {} reach the cut draws",
                volumes.size(), MAX_SCENE_SHADOW_VOLUMES);
  }
  const uint32_t volume_count = std::min(volumes.size(), MAX_SCENE_SHADOW_VOLUMES);
  if (g_volumes_used + volume_count > VOLUMES_PER_FRAME)
  {
    log_error("[renderer] this frame's shadow volume storage is full ({} volumes); a pass carries none",
              g_volumes_used);
    return {.first_volume = first_volume, .count = 0};
  }

  gpu_shadow_volume_t* volumes_out = reinterpret_cast<gpu_shadow_volume_t*>(g_volume_records.mapped) + first_volume;
  for (uint32_t index = 0; index < volume_count; ++index)
  {
    const shared::shadow_volume_t& volume = volumes[index];
    gpu_shadow_volume_t&           out    = volumes_out[index];
    write_planes(out.planes, Span<const Plane>(volume.side_planes.data, volume.side_plane_count),
                 Span<const Plane>(volume.light_facing_planes.data, volume.light_facing_plane_count));
    static_assert(sizeof(volume.light) == sizeof(uint32_t));
    std::memcpy(&out.light, &volume.light, sizeof(uint32_t));
    out.counts    = volume.side_plane_count | (volume.light_facing_plane_count << 8) |
                    (volume.cuts_geometry ? SHADOW_VOLUME_CUTS_GEOMETRY_BIT : 0u);
    out.unused[0] = out.unused[1] = 0;
  }
  g_volumes_used += volume_count;
  return {.first_volume = first_volume, .count = volume_count};
}

} // namespace renderer
} // namespace client
