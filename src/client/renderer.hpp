#pragma once

#include <SDL.h>
#include <vulkan/vulkan.h>

namespace client {
namespace renderer {

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
