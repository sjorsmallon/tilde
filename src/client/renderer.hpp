#pragma once

#include <SDL.h>
#include <vector>
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

// Draw an AABB (solid filled faces by default, or 12-edge wireframe).
// When random_color=true the shader hashes random_seed to produce a unique
// color per AABB, ignoring the color parameter.
// min/max in world space
void DrawAABB(VkCommandBuffer cmd, const linalg::vec3 &min,
              const linalg::vec3 &max, uint32_t color,
              bool as_wireframe = false, bool random_color = false,
              uint32_t random_seed = 0);

// Draw a wireframe AABB (12 line edges). Thin wrapper around DrawAABB.
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

// Material-aware mesh drawing with proper shaders.
enum class ShaderType : uint8_t { Lit, Unlit };
void DrawMeshMaterial(VkCommandBuffer cmd, const linalg::vec3 &position,
                      const linalg::vec3 &scale,
                      assets::asset_handle_t<assets::mesh_asset_t> mesh_handle,
                      const linalg::vec3 &color, ShaderType shader_type,
                      const linalg::vec3 &rotation = {0, 0, 0});

// Draw a displacement mesh using the textured unlit pipeline.
// UVs are worldspace-based (pre-baked into the mesh at 128-unit scale).
void DrawMeshTextured(VkCommandBuffer cmd, const linalg::vec3 &position,
                      const linalg::vec3 &scale,
                      assets::asset_handle_t<assets::mesh_asset_t> mesh_handle,
                      const linalg::vec3 &rotation = {0, 0, 0});

// Invalidate a cached GPU mesh buffer so it gets re-uploaded on next draw.
// Used for dynamic meshes (e.g. displacement surfaces) that change at runtime.
void invalidate_mesh_gpu(assets::asset_handle_t<assets::mesh_asset_t> handle);

// Draw a filled convex polygon (e.g. a collision face).
// Vertices are in world space. Triangle-fan decomposed internally.
// Supports alpha blending (pass e.g. 0x8800FF00 for 50% green in ABGR).
// Call reset_debug_face_buffer() once per frame before any DrawFilledPolygon calls.
void DrawFilledPolygon(VkCommandBuffer cmd, const std::vector<linalg::vec3> &verts,
                       uint32_t color);

// Reset the ring buffer used by DrawFilledPolygon. Call once at the start of each frame.
void reset_debug_face_buffer();

// Draw an arrow (shaft = AABB, head = Pyramid)
void draw_arrow(VkCommandBuffer cmd, const linalg::vec3 &start,
                const linalg::vec3 &end, uint32_t color);

// Draw a wireframe wedge
void draw_wedge(VkCommandBuffer cmd, const shared::wedge_t &wedge,
                uint32_t color);

// Debug hitbox visualization helpers
// Draw a sphere hitbox (wireframe)
void draw_hitbox_sphere(VkCommandBuffer cmd, const linalg::vec3 &center,
                        float radius, uint32_t color);

// Draw a capsule hitbox (wireframe cylinder + spheres on ends)
void draw_hitbox_capsule(VkCommandBuffer cmd, const linalg::vec3 &center,
                         float radius, float half_height, uint32_t color);

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

// --- Particle System ---

struct particle_emitter_params_t
{
  uint64_t entity_id;
  linalg::vec3 position;
  float delta_time;
  float emit_rate;
  uint32_t max_particles;
  float lifetime_min, lifetime_max;
  float velocity_min, velocity_max;
  float spread;
  linalg::vec3 gravity;
  float drag;
  float size_start, size_end;
  float rotation_speed_min, rotation_speed_max;
  linalg::vec3 color_start, color_end;
  float alpha_start, alpha_end;
};

// Dispatch compute shader to update particles for one emitter.
// Call BEFORE BeginRenderPass.
void UpdateParticles(VkCommandBuffer cmd, const particle_emitter_params_t &params);

// Draw particles for one emitter. Call INSIDE render pass.
void DrawParticles(VkCommandBuffer cmd, const particle_emitter_params_t &params);

} // namespace renderer
} // namespace client
