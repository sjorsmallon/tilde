#pragma once

#include <SDL.h>
#include <optional>
#include <vector>
#include <vulkan/vulkan.h>

#include "asset.hpp"
#include "camera.hpp"
#include "color.hpp"
#include "linalg.hpp"
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
void draw_AABB(VkCommandBuffer cmd, const linalg::vec3 &min,
              const linalg::vec3 &max, color_t color,
              bool as_wireframe = false, bool random_color = false,
              uint32_t random_seed = 0);

// Draw a wireframe AABB (12 line edges). Thin wrapper around draw_AABB.
// min/max in world space
void draw__wire_AABB(VkCommandBuffer cmd, const linalg::vec3 &min,
                  const linalg::vec3 &max, color_t color);

// Draw a simple 3D line
void draw_line(VkCommandBuffer cmd, const linalg::vec3 &start,
              const linalg::vec3 &end, color_t color);

// Set depth bias for subsequent draw_line / draw_wire_aabb calls, and for the
// next wireframe draw_mesh. Use stronger (more negative) values to push lines
// closer to camera. Sticky: it applies until changed, so a caller that raises
// it is expected to restore it.
// Lines are batched and drawn at end of frame, but the bias in force at
// draw_line time is the one that frame's flush uses for that line.
void set_line_depth_bias(float constant_factor, float slope_factor);

// Draw a mesh from an asset handle
// Returns true if the GPU supports polygon wireframe (fillModeNonSolid).
bool WireframeSupported();

enum class  shader_type : uint8_t { Lit, Unlit, Textured };

struct mesh_draw_parameters_t
{
  linalg::vec3 position  = {0, 0, 0};
  linalg::vec3 scale     = {1, 1, 1};
  linalg::vec3 rotation  = {0, 0, 0};
  // Tint color. Used as flat color for unshaded meshes, and as a
  // fallback tint for meshes without per-material colors.
  color_t      color     = colors::white;
  shader_type  shader    = shader_type::Lit;
  bool         wireframe = false;

  // The pose to skin this draw with: one matrix per bone, in the mesh's
  // skeleton's bone order, as produced by assets::compute_skinning_matrices.
  // Ignored entirely for an unskinned mesh.
  //
  // Null on a SKINNED mesh means BIND POSE, which the renderer derives from the
  // skeleton and caches. That is a real default rather than a failure case: a
  // caller that has no animator yet gets the model standing in the pose it was
  // authored in, and the animation work can arrive at the call sites one at a
  // time. `skinning_matrix_count` must match the skeleton's bone count when the
  // pointer is non-null; a mismatch is refused and drawn in bind pose.
  const linalg::mat4f *skinning_matrices     = nullptr;
  uint32_t             skinning_matrix_count = 0;
};

void draw_mesh(VkCommandBuffer cmd,
              assets::asset_handle_t<assets::mesh_asset_t> mesh_handle,
              const mesh_draw_parameters_t &parameters = {});

// Invalidate a cached GPU mesh buffer so it gets re-uploaded on next draw.
// Used for dynamic meshes (e.g. displacement surfaces) that change at runtime.
void invalidate_mesh_gpu(assets::asset_handle_t<assets::mesh_asset_t> handle);

// Draw a filled convex polygon (e.g. a collision face).
// Vertices are in world space. Triangle-fan decomposed internally.
// Supports alpha blending (pass e.g. with_alpha(colors::green, 128) for 50% green).
// Call reset_debug_face_buffer() once per frame before any  draw_filled_polygon calls.
void  draw_filled_polygon(VkCommandBuffer cmd, const std::vector<linalg::vec3> &verts,
                       color_t color);

// Reset the ring buffer used by  draw_filled_polygon. Call once at the start of each frame.
void reset_debug_face_buffer();

// Draw an arrow (shaft = AABB, head = Pyramid)
void draw_arrow(VkCommandBuffer cmd, const linalg::vec3 &start,
                const linalg::vec3 &end, color_t color);

// Draw a wireframe wedge
void draw_wedge(VkCommandBuffer cmd, const shared::wedge_t &wedge,
                color_t color);

// Debug hitbox visualization helpers
// Draw a sphere hitbox (wireframe)
void draw_hitbox_sphere(VkCommandBuffer cmd, const linalg::vec3 &center,
                        float radius, color_t color);

// Draw a capsule hitbox (wireframe cylinder + spheres on ends)
void draw_hitbox_capsule(VkCommandBuffer cmd, const linalg::vec3 &center,
                         float radius, float half_height, color_t color);

// Apply the viewport to the command buffer (calculating pixel rect from
// normalized)
void set_viewport(VkCommandBuffer cmd, const viewport_t &vp);

// Establish the view for every subsequent draw call this frame: applies the
// viewport and computes the view-projection matrix the draw_* functions read.
// Draws nothing itself — the caller then issues its own draw_mesh / draw_line /
// draw_AABB calls against it.
void set_view(VkCommandBuffer cmd, const render_view_t &view);

// World point -> pixels, through the view-projection and viewport the CURRENT
// frame's set_view established. `nullopt` means the point has no screen
// position: behind a perspective camera's eye, where the perspective divide
// would otherwise flip it to a mirrored spot on screen that looks plausible.
[[nodiscard]] std::optional<linalg::vec2> try_project_to_screen(const linalg::vec3 &world);

// A text label anchored to a world position -- bone names, entity ids, debug
// values that need to sit ON the thing they describe.
//
// Unlike every other draw_* here this is NOT a Vulkan draw: it projects and
// appends to ImGui's background draw list, which is composited after the 3D
// pass. So it needs no command buffer, it is not depth-tested (a label on a
// bone inside the mesh still shows), and it must be called AFTER set_view for
// the frame -- it reads that frame's matrices. Off-screen labels are dropped.
void draw_text_in_world(const linalg::vec3 &world, const char *text, color_t color);

// Initialize the renderer (Vulkan + ImGui)
bool init(SDL_Window *window);

// shutdown the renderer
void shutdown();

// Begin a new frame. Returns a command buffer to record 3D commands into.
// Returns VK_NULL_HANDLE if the frame should be skipped (e.g. minimized).
VkCommandBuffer begin_frame();

// Process SDL events (mostly for ImGui)
void process_event(const SDL_Event *event);

// Begin the main render pass. Call this after recording your own 3D commands
// but before end_frame if you want to draw into the main swapchain.
// Actually, with the current structure, usage is:
// 1. begin_frame() -> simple setup
// 2. State::render_3d() -> records to cmd
// 3. begin_render_pass() -> starts RP
// 4. end_frame() -> draws UI, ends RP, submits
//
// This is a bit "leaky" regarding RenderPass state.
// A cleaner way for this simple app:
// begin_render_pass(cmd) starts the main pass.
// Begin the main render pass.
void begin_render_pass(VkCommandBuffer cmd);

// Draw a temporary announcement text at the top of the screen
void draw_announcement(const char *text);

// End the frame. Renders ImGui, ends render pass, submits to queue, presents.
void end_frame(VkCommandBuffer cmd);

VkDevice get_VkDevice();;
VkRenderPass get_VkRenderPass();
VkPhysicalDevice get_VkPhysicalDevice();
uint32_t get_current_frame_idx_in_swapchain();
int get_max_frames_in_flight();

// --- GPU texture upload ---
// Upload a texture_asset_t to the GPU. Returns an invalid gpu_texture_t on failure.
// The caller owns the returned object and must call destroy_texture when done.
struct gpu_texture_t
{
  VkImage        image   = VK_NULL_HANDLE;
  VkDeviceMemory memory  = VK_NULL_HANDLE;
  VkImageView    view    = VK_NULL_HANDLE;
  VkSampler      sampler = VK_NULL_HANDLE;
  bool valid() const { return image != VK_NULL_HANDLE; }
};

// `srgb` picks the image format, and it is a question about what the BYTES MEAN,
// not about how they look: the swapchain is B8G8R8A8_SRGB, so the hardware
// encodes on write and a colour texture must be decoded on read or it is
// gamma-corrected twice. Pass true for anything authored as a colour (albedo),
// false for data sampled as numbers (roughness, metallic, height, normals).
gpu_texture_t upload_texture(const assets::texture_asset_t *texture, bool srgb = false);
void          destroy_texture(gpu_texture_t &tex);

// Get GPU-uploaded mesh buffers for custom pipeline drawing.
// Returns false if the mesh isn't valid or upload fails.
struct mesh_gpu_info_t
{
  VkBuffer vertex_buffer;
  VkBuffer index_buffer;
  uint32_t index_count;
};
bool get_mesh_gpu_info(assets::asset_handle_t<assets::mesh_asset_t> handle,
                    mesh_gpu_info_t &out);

// --- Particle System ---

struct particle_emitter_parameters_t
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
// Call BEFORE begin_render_pass.
void update_particles(VkCommandBuffer cmd, const particle_emitter_parameters_t &parameters);

// Draw particles for one emitter. Call INSIDE render pass.
void draw_particles(VkCommandBuffer cmd, const particle_emitter_parameters_t &parameters);

} // namespace renderer
} // namespace client
