#pragma once

// The renderer's whole public surface. Two rules hold everything else together:
//
//   1. GPU WORK HAPPENS AT REGISTRATION, NEVER AT DRAW. register_mesh /
//      register_texture / register_material do the staging copy and hand back a
//      handle; a draw only ever binds. Nothing below stalls the GPU mid-frame.
//   2. NOTHING IS STICKY. A draw is a value, a view is a value, a frame is a
//      span of view passes. There is no set_view, no depth-bias mode, no
//      once-per-frame reset to remember, and no VkCommandBuffer in the game
//      path.
//
// ImGui forces the frame to be two-phase, because UI is built imperatively
// between the two calls. That is the ONLY protocol left:
//
//   if (renderer::new_frame())          // acquires image, starts the ImGui frame
//   {
//     ... build ImGui UI, draw lists, view passes ...
//     renderer::render_frame(passes);   // executes, composites UI, presents
//   }
//
// Everything else is order-free: registration may happen any time after init(),
// including between those two calls, and passes are values.
//
// renderer_def.md is the design and the reasoning.

#include <SDL.h>
#include <optional>
#include <string>
#include <vector>
#include <vulkan/vulkan.h> // escape-hatch section only

#include "../shared/asset.hpp"
#include "../shared/color.hpp"
#include "../shared/linalg.hpp"
#include "../shared/span.hpp"
#include "camera.hpp"

namespace client
{
namespace renderer
{

// --- Lifecycle ---

bool init(SDL_Window *window);
void shutdown();
void process_event(const SDL_Event *event);

// False means skip this frame entirely (minimized / swapchain rebuild) -- build
// no UI, call no render_frame. No ImGui frame is left open when it returns
// false, so a skipped frame costs the caller nothing but the early return.
[[nodiscard]] bool new_frame();

// --- Handles ---
// Renderer-owned storage. Invalid by default; registration returns invalid on
// failure (already logged) -- the same shape as assets::load_mesh. Submitting a
// draw with an invalid mesh handle logs and skips; it never renders silently
// wrong.

struct mesh_handle_t
{
  uint32_t index = UINT32_MAX;
  bool     valid() const { return index != UINT32_MAX; }
  bool     operator==(const mesh_handle_t &) const = default;
};

struct texture_handle_t
{
  uint32_t index = UINT32_MAX;
  bool     valid() const { return index != UINT32_MAX; }
  bool     operator==(const texture_handle_t &) const = default;
};

struct material_handle_t
{
  uint32_t index = UINT32_MAX;
  bool     valid() const { return index != UINT32_MAX; }
  bool     operator==(const material_handle_t &) const = default;
};

// --- Materials ---

enum class shader_t : uint8_t
{
  lit,
  unlit
};

enum class blend_mode_t : uint8_t
{
  opaque,
  alpha
};

enum class cull_mode_t : uint8_t
{
  back,
  none
};

enum class fill_mode_t : uint8_t
{
  solid,
  wireframe
};

// Which vertex buffers a pipeline reads. Fixed per MESH at register_mesh time
// from mesh_asset_t::is_skinned(), and part of the pipeline cache key -- it is
// not a per-draw choice, because a mesh either has a skin array or it does not.
enum class vertex_layout_t : uint8_t
{
  static_mesh,
  skinned
};

// Everything that decides WHICH pipeline a material renders through. Resolved
// against the internal pipeline cache; the full key is (pipeline_state_t,
// vertex layout, fill mode) -- the last two come from the mesh and the draw.
//
// Note what is NOT here: `textured`. Every mesh pipeline binds an albedo
// sampler, and a material with no texture resolves to an internal 1x1 white at
// registration, so the colour multiplies out of the shader. That is what keeps
// update_material free of pipeline consequences.
struct pipeline_state_t
{
  shader_t     shader      = shader_t::lit;
  blend_mode_t blend_mode  = blend_mode_t::opaque;
  cull_mode_t  cull_mode   = cull_mode_t::back;
  bool         depth_test  = true;
  bool         depth_write = true;

  bool operator==(const pipeline_state_t &) const = default;
};

// Freely mutable; no pipeline consequences. Invalid texture = flat colour. A
// texture that was NAMED but failed to load renders the magenta checkerboard
// (renderer-internal), never silently flat.
struct material_parameters_t
{
  texture_handle_t base_color_texture;
  linalg::vec4f    base_color = {1, 1, 1, 1};
};

struct material_t
{
  pipeline_state_t      pipeline_state; // register-time identity; change = new material
  material_parameters_t parameters;     // mutable in place via update_material
};

// --- Registration (all GPU upload happens HERE, never at draw time) ---

// `srgb` is about what the bytes MEAN: true for authored colour (albedo), false
// for data (normals, roughness). The swapchain is B8G8R8A8_SRGB, so the
// hardware encodes on write and a colour texture must be decoded on read or it
// is gamma-corrected twice.
texture_handle_t register_texture(const assets::texture_asset_t &texture, bool srgb);

material_handle_t register_material(const material_t &material);

// Parameters only. A pipeline_state change is a new material -- register again.
void update_material(material_handle_t handle, const material_parameters_t &parameters);

// Uploads vertices/indices/skin, and registers the asset's own materials as the
// mesh's default material table (slot order = the asset's material order). A
// mesh with no submeshes gets one synthesized submesh over all its indices at
// slot 0, so "has materials" stops being a branch anywhere downstream.
mesh_handle_t register_mesh(const assets::mesh_asset_t &mesh);

// Re-upload changed geometry (displacement sculpting). The upload happens NOW,
// not lazily at the next draw.
void update_mesh(mesh_handle_t handle, const assets::mesh_asset_t &mesh);

// The mesh's own material table, in slot order. A caller that wants the same
// textures under a DIFFERENT pipeline_state -- an unlit view of a model, a
// translucent placement ghost -- reads these, re-registers each with the state
// it wants, and passes the result as mesh_draw_t::material_overrides.
[[nodiscard]] Span<const material_handle_t> mesh_default_materials(mesh_handle_t handle);
[[nodiscard]] material_parameters_t         material_parameters(material_handle_t handle);

// --- Draws ---

struct mesh_draw_t
{
  mesh_handle_t                 mesh;
  linalg::mat4f                 transform; // caller composes TRS; the renderer derives the normal matrix
  Span<const material_handle_t> material_overrides = {}; // empty = the mesh's default table, by slot
  Span<const linalg::mat4f>     pose               = {}; // skinned mesh only; empty = bind pose.
                                                         // Length must equal the skeleton's bone
                                                         // count; a mismatch logs and draws bind pose.
  color_t     tint = colors::white;
  fill_mode_t fill = fill_mode_t::solid;
};

// --- Debug drawing ---
// Immediate-style geometry that doesn't earn a material: editor overlays,
// hitboxes, gizmos. Append during update, hand the list to a view_pass_t.
//
// The list is CALLER-OWNED on purpose. Once the view is a parameter, a global
// batch cannot answer "which viewport do these lines render into?" -- see
// renderer_def.md. It is also the one retained home for debug geometry, which
// is what makes a one-shot event (a hitscan trace fired in a fixed tick) able
// to outlive the single frame it was appended in.

struct debug_line_t
{
  linalg::vec3f start;
  linalg::vec3f end;
  color_t       color;
  float         depth_bias;
  float         remaining_seconds; // <= 0 dies on the next retire()
};

struct debug_polygon_t
{
  uint32_t first_vertex; // slice of debug_draw_list_t::polygon_vertices
  uint32_t vertex_count;
  color_t  color;
  float    remaining_seconds;
  // Darken toward the triangle edges, so an untextured solid box reads as a box
  // instead of a silhouette. True for the faces of a solid aabb/box; false for
  // an overlay face (a collision polygon), where the darkening would just be
  // noise over geometry you are trying to see through.
  bool shaded;
};

struct debug_text_t
{
  linalg::vec3f position;
  std::string   text;
  color_t       color;
  float         remaining_seconds;
};

struct debug_draw_list_t
{
  std::vector<debug_line_t>    lines;
  std::vector<linalg::vec3f>   polygon_vertices; // one pool, sliced by polygons
  std::vector<debug_polygon_t> polygons;
  std::vector<debug_text_t>    texts;

  // Per-frame maintenance -- call once at frame start. Subtracts delta_seconds
  // from every entry and removes the expired. Entries appended with the default
  // lifetime of 0 die here, i.e. they draw for exactly the frame they were
  // appended in. Keeps capacity.
  //
  // It takes the caller's dt on purpose: whether debug draws age by game time
  // or wall time (what pausing does to them) is the caller's decision, made by
  // which dt it passes.
  void retire(float delta_seconds);
  void clear(); // wholesale drop (map switch, state exit)

  // Append helpers -- typed push_backs, nothing more.
  //
  // `depth_bias` replaces the old sticky set_line_depth_bias: it is per call,
  // and more negative pulls toward the camera. 0 = none.
  // `seconds` is how long the entry outlives this frame: 0 (the default) = this
  // frame only; > 0 = survives retire() until the time runs out.
  void line(const linalg::vec3f &start, const linalg::vec3f &end, color_t color,
            float depth_bias = 0.0f, float seconds = 0.0f);
  void aabb(const linalg::vec3f &min, const linalg::vec3f &max, color_t color,
            fill_mode_t fill = fill_mode_t::wireframe, float depth_bias = 0.0f,
            float seconds = 0.0f);
  // The same shape, spelled the way the EDITOR holds it: an object has a
  // position and half-extents, where a bounds computation produces min/max.
  // Both spellings exist because converting at every call site is how a sign
  // error gets in.
  void box(const linalg::vec3f &center, const linalg::vec3f &half_extents, color_t color,
           fill_mode_t fill = fill_mode_t::wireframe, float depth_bias = 0.0f,
           float seconds = 0.0f);
  // Triangle-fan decomposed, and alpha-blended when the colour says so. There is
  // no once-per-frame reset to remember -- the list IS the frame scope.
  void filled_polygon(Span<const linalg::vec3f> vertices, color_t color, float seconds = 0.0f,
                      bool shaded = false);

  // Compositions -- these decompose into `lines` entries at append time.
  void arrow(const linalg::vec3f &start, const linalg::vec3f &end, color_t color,
             float seconds = 0.0f);
  void wire_circle(const linalg::vec3f &center, float radius, const linalg::vec3f &normal,
                   color_t color, float seconds = 0.0f);
  void wire_sphere(const linalg::vec3f &center, float radius, color_t color, float seconds = 0.0f);
  void wire_capsule(const linalg::vec3f &center, float radius, float half_height, color_t color,
                    float seconds = 0.0f);

  // Projected through the pass's view and composited over the 3D scene, so it
  // is NOT depth-tested -- a label on a bone inside a mesh still reads.
  // Off-screen labels are dropped.
  void text(const linalg::vec3f &world_position, const char *text, color_t color,
            float seconds = 0.0f);
};

// --- Particles ---

struct particle_emitter_parameters_t
{
  uint64_t      entity_id;
  linalg::vec3f position;
  float         delta_time;
  float         emit_rate;
  uint32_t      max_particles;
  float         lifetime_min, lifetime_max;
  float         velocity_min, velocity_max;
  float         spread;
  linalg::vec3f gravity;
  float         drag;
  float         size_start, size_end;
  float         rotation_speed_min, rotation_speed_max;
  linalg::vec3f color_start, color_end;
  float         alpha_start, alpha_end;
};

// --- Escape hatch: a raw-Vulkan draw inside a pass ---
// For code that builds its own pipelines (the shader editor). Recorded inside
// the render pass, after the pass's own draws, with the pass's viewport already
// applied -- which is how custom pipelines keep working without the render pass
// being public. Nothing in the game path may use it.

struct custom_draw_t
{
  void (*record)(VkCommandBuffer cmd, void *user) = nullptr;
  void *user                                      = nullptr;
};

// --- Views and passes ---

struct viewport_t
{
  // normalized
  linalg::vec2 start;
  linalg::vec2 dimensions;
};

struct render_view_t
{
  viewport_t viewport;
  camera_t   camera;
};

// One camera's worth of frame. A second camera (an editor panel, a scope PiP) is
// simply a second pass in the span handed to render_frame.
//
// `debug` is a POINTER because the list is optional and non-owning: null means
// "no debug draws" (a shipping view, a PiP pass), and the caller owns the list --
// typically a state member reused across frames so vector capacity stays warm.
// By value would copy vectors every frame; a Span does not fit because the list
// is several parallel vectors, not one array.
struct view_pass_t
{
  render_view_t                             view;
  Span<const mesh_draw_t>                   draws;              // sorted internally by pipeline, then material
  const debug_draw_list_t                  *debug     = nullptr;
  Span<const particle_emitter_parameters_t> particles = {};     // compute sequenced before the render pass
  Span<const custom_draw_t>                 custom    = {};     // escape hatch, see above
};

// Executes the whole frame: particle compute, render pass, every view pass in
// order, ImGui composite, submit, present. The pass structure is internal.
void render_frame(Span<const view_pass_t> passes);

// --- Overlay text ---

// Temporary banner at the top of the screen for ~3s. Fire-and-forget.
void draw_announcement(const char *text);

// --- Utilities ---

// The matrices a pass with this view draws through, against the current
// swapchain extent. Pure function of its arguments.
//
// Exposed because a custom pipeline needs the SAME matrices the rest of the
// pass uses, and rebuilding them by hand is how the two silently drift apart.
// All three come back together because a shader that wants view and projection
// separately should not have to recover one from the other.
struct view_matrices_t
{
  linalg::mat4f view;
  linalg::mat4f projection;
  linalg::mat4f view_projection;
};

[[nodiscard]] view_matrices_t view_matrices(const render_view_t &view);
[[nodiscard]] linalg::mat4f   view_projection(const render_view_t &view);

// World point -> pixels through THIS view and the current swapchain extent.
// Pure function of its arguments -- no dependency on any prior call.
// nullopt = behind a perspective camera's eye, where the perspective divide
// would otherwise flip it to a mirrored spot on screen that looks plausible.
[[nodiscard]] std::optional<linalg::vec2> try_project_to_screen(const render_view_t &view,
                                                                const linalg::vec3f  &world);

// GPU supports fillModeNonSolid; false means wireframe fills draw solid.
bool wireframe_supported();

// --- Escape hatch: raw Vulkan access ---
// For code that builds its own pipelines (shader_tool_runtime,
// shader_editor_state). Everything below is outside the draw-list model.

VkDevice         get_VkDevice();
VkPhysicalDevice get_VkPhysicalDevice();
VkRenderPass     get_VkRenderPass();
uint32_t         get_current_frame_index();
uint32_t         get_max_frames_in_flight();

// Owning GPU texture for custom pipelines (the PBR preview). The draw-list path
// never sees this type -- it uses texture_handle_t.
struct gpu_texture_t
{
  VkImage        image   = VK_NULL_HANDLE;
  VkDeviceMemory memory  = VK_NULL_HANDLE;
  VkImageView    view    = VK_NULL_HANDLE;
  VkSampler      sampler = VK_NULL_HANDLE;
  bool           valid() const { return image != VK_NULL_HANDLE; }
};

gpu_texture_t upload_texture(const assets::texture_asset_t *texture, bool srgb = false);
void          destroy_texture(gpu_texture_t &texture);

struct mesh_gpu_info_t
{
  VkBuffer vertex_buffer;
  VkBuffer index_buffer;
  uint32_t index_count;
};

[[nodiscard]] std::optional<mesh_gpu_info_t> try_get_mesh_gpu_info(mesh_handle_t handle);

} // namespace renderer
} // namespace client
