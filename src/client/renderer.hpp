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
  unlit,
  // Lit, plus a world-space grid ruled onto the surface. Only meaningful on a
  // mesh whose UV is a world-axis projection over the 128-unit cell -- the
  // generated brush mesh -- since the grid is read straight out of fragUV.
  grid
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

// `draw_when_occluded` asks for a SECOND, dimmed draw of the same geometry where the depth
// test FAILS, so the part of an entry hidden behind something reads as faint
// instead of as absent. It is per entry and not per pass because an editor gizmo
// being occluded is information, where a hit volume buried inside the player
// model it belongs to is just invisible.
struct debug_line_t
{
  linalg::vec3f start;
  linalg::vec3f end;
  color_t       color;
  float         depth_bias;
  float         remaining_seconds; // <= 0 dies on the next retire()
  bool          draw_when_occluded;
};

// How a filled polygon is shaded, as one value rather than a tail of bools --
// past three, positional flags at a call site stop being readable. Every member
// defaults to the plain flat overlay, so `{}` is the polygon nobody asked
// anything of.
struct debug_face_style_t
{
  // Darken toward the triangle edges, so an untextured solid box reads as a box
  // instead of a silhouette. True for the faces of a solid aabb/box; false for
  // an overlay face (a collision polygon), where the darkening would just be
  // noise over geometry you are trying to see through.
  bool shaded = false;

  // Also draw the hidden part, dimmed. See debug_line_t.
  bool draw_when_occluded = false;

  // Fade the face out as it turns to face the camera, leaving the silhouette --
  // the classic rim term. What it buys is DEPTH COMPLEXITY: ten overlapping
  // volumes drawn flat pile their face-on interiors into one wash, and this
  // removes exactly the part that carries no shape information.
  //
  // Wrong for a collision polygon, which is a flat quad you look at face-on
  // precisely to see which plane you hit -- which is why it is per polygon and
  // not a property of the overlay pipeline.
  bool rim = false;
};

struct debug_polygon_t
{
  uint32_t first_vertex; // slice of debug_draw_list_t::polygon_vertices
  uint32_t vertex_count;
  color_t             color;
  float               remaining_seconds;
  debug_face_style_t  style;
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
            float depth_bias = 0.0f, float seconds = 0.0f, bool draw_when_occluded = false);
  void aabb(const linalg::vec3f &min, const linalg::vec3f &max, color_t color,
            fill_mode_t fill = fill_mode_t::wireframe, float depth_bias = 0.0f,
            float seconds = 0.0f, bool draw_when_occluded = false);
  // The same shape, spelled the way the EDITOR holds it: an object has a
  // position and half-extents, where a bounds computation produces min/max.
  // Both spellings exist because converting at every call site is how a sign
  // error gets in.
  void box(const linalg::vec3f &center, const linalg::vec3f &half_extents, color_t color,
           fill_mode_t fill = fill_mode_t::wireframe, float depth_bias = 0.0f,
           float seconds = 0.0f, bool draw_when_occluded = false);
  // Triangle-fan decomposed, and alpha-blended when the colour says so. There is
  // no once-per-frame reset to remember -- the list IS the frame scope.
  void filled_polygon(Span<const linalg::vec3f> vertices, color_t color, float seconds = 0.0f,
                      debug_face_style_t style = {});

  // Compositions -- these decompose into `lines` entries at append time.
  // `draw_when_occluded` rides through to every line the head decomposes into.
  // A manipulator is the case that needs it: a gizmo drawn at an object centre
  // is INSIDE that object, so depth-tested it is invisible exactly when you want
  // to grab it.
  void arrow(const linalg::vec3f &start, const linalg::vec3f &end, color_t color,
             float seconds = 0.0f, bool draw_when_occluded = false);
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

// --- Screen-space UI ---
// Textured quads in framebuffer pixels: the HUD, the crosshair, the
// announcement banner. Composited once per frame, after every view pass and
// before ImGui, with no camera and no depth.
//
// The renderer knows QUADS, not fonts. A font sits one layer up
// (client/ui/font.hpp) and produces quads into this list, which is what keeps
// glyph packing, metrics and text layout out of the renderer entirely.
//
// Caller-owned for the same reason debug_draw_list_t is, minus the lifetimes:
// screen-space UI is per-FRAME and regenerated from state every frame, so this
// one is cleared rather than retired.

struct ui_vertex_t
{
  linalg::vec2 position; // framebuffer PIXELS, origin top-left
  linalg::vec2 uv;
  uint32_t     color; // to_abgr(), the same packing the debug shaders take
};

// A run of vertices sharing one texture. Batches exist so a string of glyphs is
// one draw call; nothing else about them is interesting.
struct ui_batch_t
{
  texture_handle_t texture; // invalid = the renderer's internal 1x1 white
  uint32_t         first_vertex = 0;
  uint32_t         vertex_count = 0;
};

struct ui_draw_list_t
{
  std::vector<ui_vertex_t> vertices;
  std::vector<ui_batch_t>  batches;

  void clear(); // keeps capacity

  void quad(linalg::vec2 min, linalg::vec2 max, linalg::vec2 uv_min, linalg::vec2 uv_max,
            color_t color, texture_handle_t texture = {});

  void rect(linalg::vec2 min, linalg::vec2 max, color_t color);
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
// order, the screen-space UI, ImGui composite, submit, present. The pass
// structure is internal.
//
// `ui` is a reference and not a pointer, unlike view_pass_t::debug: screen-space
// UI is per-FRAME and always present, where a debug list is optional per pass. It
// is composited AFTER every view pass and BEFORE ImGui, so the dev console and
// the editor panels sit on top of the HUD -- which is the precedence you want
// the moment the console is open.
void render_frame(Span<const view_pass_t> passes, const ui_draw_list_t &ui);

// --- Utilities ---

// The swapchain extent in pixels, which is the coordinate space ui_draw_list_t
// works in. Exposed so layout code has it without reaching into ImGui's io.
[[nodiscard]] linalg::vec2 screen_size();

// Pixels per logical UI unit on the display the window is currently on: 1.0 at
// 96 DPI, 1.5 at Windows' 150%. The ONE owner of that factor -- the process is
// per-monitor-v2 DPI aware, so nothing upscales the finished frame any more and
// every fixed pixel size in the UI is now a size in REAL pixels that has to be
// multiplied by this or it shrinks as the display gets denser.
[[nodiscard]] float display_scale();

// Convert a point in LOGICAL WINDOW POINTS -- what SDL_GetMouseState and every
// SDL event report -- into the FRAMEBUFFER PIXELS this UI draws in.
//
// The two are the same number only at 100% display scaling. The window is
// created with SDL_WINDOW_ALLOW_HIGHDPI, so on a scaled display screen_size()
// (the swapchain extent, from SDL_Vulkan_GetDrawableSize) is larger than
// SDL_GetWindowSize, and a hit-test done in the wrong one lands at a growing
// offset from the text it belongs to. This lives here because the renderer owns
// the window; every UI hit-test goes through it.
[[nodiscard]] linalg::vec2 logical_window_points_to_framebuffer_pixels(linalg::vec2 window_point);

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

// The front-face winding EVERY pipeline uses, in here and out at the escape
// hatch alike. It was four separate literals before, which is how three of the
// hand-written geometry tables came to disagree with each other.
//
// Every surface is wound counter-clockwise seen from OUTSIDE, so
// cross(b - a, c - b) is the outward normal -- what Blender, the .obj exporter,
// add_box_faces() and debug_draw_list_t::aabb all produce. Anything hand-wound
// has to match; there is no per-object escape.
//
// Getting it wrong is nearly invisible, which is why it survived so long: vertex
// normals still light the surface correctly, so a solid object renders as its
// own interior rather than as anything obviously broken. Check it by going
// INSIDE a box -- the walls should be see-through. Solid from both sides means
// culling is off, not that the winding is right.
constexpr VkFrontFace HOUSE_FRONT_FACE = VK_FRONT_FACE_COUNTER_CLOCKWISE;

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
