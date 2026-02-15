#pragma once

#include <SDL.h>
#include <vulkan/vulkan.h>

#include "asset.hpp"
#include "camera.hpp"
#include "linalg.hpp"
#include "old_ideas/ecs.hpp"
#include "shapes.hpp"

namespace client
{
namespace renderer
{

struct viewport_t
{
  // normalized
  linalg::vec2 start;
  linalg::vec2 dimensions;
};

struct render_view_t
{
  viewport_t viewport;
  camera_t camera;
};

// Draw a solid AABB (filled faces)
// min/max in world space
void DrawAABB(VkCommandBuffer cmd, const linalg::vec3 &min,
              const linalg::vec3 &max, uint32_t color);

// Draw a wireframe AABB (12 line edges)
// min/max in world space
void DrawWireAABB(VkCommandBuffer cmd, const linalg::vec3 &min,
                  const linalg::vec3 &max, uint32_t color);

// Draw a simple 3D line
void DrawLine(VkCommandBuffer cmd, const linalg::vec3 &start,
              const linalg::vec3 &end, uint32_t color);

// Draw a mesh from an asset handle
void DrawMesh(VkCommandBuffer cmd, const linalg::vec3 &position,
              const linalg::vec3 &scale,
              assets::asset_handle_t<assets::mesh_asset_t> mesh_handle,
              uint32_t color,
              const linalg::vec3 &rotation = {0, 0, 0});

// Draw a mesh as wireframe from an asset handle
void DrawMeshWireframe(VkCommandBuffer cmd, const linalg::vec3 &position,
                       const linalg::vec3 &scale,
                       assets::asset_handle_t<assets::mesh_asset_t> mesh_handle,
                       uint32_t color,
                       const linalg::vec3 &rotation = {0, 0, 0});

// Draw an arrow (shaft = AABB, head = Pyramid)
void draw_arrow(VkCommandBuffer cmd, const linalg::vec3 &start,
                const linalg::vec3 &end, uint32_t color);

// Draw a wireframe wedge
void draw_wedge(VkCommandBuffer cmd, const shared::wedge_t &wedge,
                uint32_t color);

// Apply the viewport to the command buffer (calculating pixel rect from
// normalized)
void set_viewport(VkCommandBuffer cmd, const viewport_t &vp);

// The main draw function for a specific view
void render_view(VkCommandBuffer cmd, const render_view_t &view,
                 const ecs::Registry &registry);

// Initialize the renderer (Vulkan + ImGui)
bool Init(SDL_Window *window);

// Shutdown the renderer
void Shutdown();

// Begin a new frame. Returns a command buffer to record 3D commands into.
// Returns VK_NULL_HANDLE if the frame should be skipped (e.g. minimized).
VkCommandBuffer BeginFrame();

// Process SDL events (mostly for ImGui)
void ProcessEvent(const SDL_Event *event);

// Begin the main render pass. Call this after recording your own 3D commands
// but before EndFrame if you want to draw into the main swapchain.
// Actually, with the current structure, usage is:
// 1. BeginFrame() -> simple setup
// 2. State::render_3d() -> records to cmd
// 3. BeginRenderPass() -> starts RP
// 4. EndFrame() -> draws UI, ends RP, submits
//
// This is a bit "leaky" regarding RenderPass state.
// A cleaner way for this simple app:
// BeginRenderPass(cmd) starts the main pass.
// Begin the main render pass.
void BeginRenderPass(VkCommandBuffer cmd);

// Draw a temporary announcement text at the top of the screen
void draw_announcement(const char *text);

// End the frame. Renders ImGui, ends render pass, submits to queue, presents.
void EndFrame(VkCommandBuffer cmd);

VkDevice GetDevice(); // Helper if needed

} // namespace renderer
} // namespace client
