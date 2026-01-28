#pragma once

#include <SDL.h>
#include <vulkan/vulkan.h>

#include "camera.hpp"
#include "ecs.hpp"
#include "linalg.hpp"

namespace client {
namespace renderer {

struct viewport_t {
  // normalized
  linalg::vec2 start;
  linalg::vec2 dimensions;
};

struct render_view_t {
  viewport_t viewport;
  camera_t camera;
};

// Draw a wireframe AABB with barycentric edge darkening
// min/max in world space
void DrawAABB(VkCommandBuffer cmd, float minX, float minY, float minZ,
              float maxX, float maxY, float maxZ, uint32_t color);

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
void BeginRenderPass(VkCommandBuffer cmd);

// End the frame. Renders ImGui, ends render pass, submits to queue, presents.
void EndFrame(VkCommandBuffer cmd);

VkDevice GetDevice(); // Helper if needed

} // namespace renderer
} // namespace client
