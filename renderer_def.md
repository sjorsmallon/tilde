# Renderer — Design

Outcome of the design discussion on 2026-08-12. This is the shape of the
renderer rewrite that has been "expected but not scheduled" since 2026-08-10 —
the goal, in the user's words: **as simple, understandable, and stateless as
possible.** It supersedes the 2026-08-09 assessment's "leave it alone" only in
the sense that the destination is now written down; nothing here schedules the
work.

`src/client/renderer.{hpp,cpp}` today is ~5,000 lines with ~60 file-statics.
The statics are mostly harmless (Vulkan objects that live for the process).
What actually hurts is the **API contract**, in three forms — and each maps to
one structural change below.

## Why (the three stateful contracts)

1. **Sticky, order-dependent calls.** `set_view` must precede every `draw_*`;
   `set_line_depth_bias` is sticky and callers are "expected to restore it";
   `reset_debug_face_buffer` must run once per frame before any
   `draw_filled_polygon`; the `begin_frame → render_3d → begin_render_pass →
   end_frame` protocol is documented as leaky in renderer.hpp itself. Every one
   of these is a hidden parameter passed through time instead of through the
   signature. `g_current_view_proj` is also the known one-camera wall (the
   Animation tool's editor panel, scope PiP, shadow maps all want a second
   view).

2. **Hidden heavy work inside the draw path.** `draw_mesh` →
   `upload_mesh_to_gpu` ends in `vkQueueWaitIdle`; textures upload and
   descriptors allocate lazily mid-draw. First sight of a player = mesh + 3
   textures = several full GPU stalls mid-frame. A draw call's cost depends on
   what every previous frame happened to draw.

3. **`draw_mesh` is a submit-time decision tree.** It picks among ~9 pipelines
   from *(wireframe, shader_type, skinned, textured, has_materials)*, with
   rules subtle enough to need paragraph comments ("unlit is a skinning shader
   too", "wireframe wins over the shader choice"). Root cause: **the pipeline
   choice belongs to the material, but today it is derived per draw call from
   draw parameters.** That is why every new axis (skinned, textured,
   wireframe) multiplied the branching instead of adding a row somewhere.

## Core model

Three changes, one per contract above:

1. **Uploads at registration time, never at draw time.** `register_mesh` /
   `register_texture` / `register_material` do the staging copy once,
   explicitly, and return handles. Draw only ever binds. This deletes the
   mid-frame `vkQueueWaitIdle` stalls for free.

2. **Material owns pipeline identity; the renderer owns a pipeline cache.**
   A material carries a `pipeline_state_t` (shader, blend, cull, depth flags).
   `register_material` resolves it against an internal cache keyed on
   *(pipeline_state_t, vertex layout, fill mode)* — the last two contributed
   by the mesh and the draw. The nine named `g_*_pipeline` globals collapse
   into that one map. `textured` and `skinned` stop being choices anywhere:
   textured follows from the material's texture handle being valid, skinned
   follows from the mesh having a skin buffer plus a non-empty pose.

3. **The view is a parameter, not ambient state.** Callers build draw lists
   and hand them to `render_frame` as *view passes* — each pass a
   self-contained value of (view, draws, debug list, particles). A second
   camera is a second pass. `render_frame` sorts draws by pipeline, then
   material, internally; callers append in any order.

The command buffer and render pass leave the public API entirely. ImGui forces
the frame to be two-phase (UI is built imperatively between the two calls),
but that is the only protocol left:

```
if (renderer::new_frame())            // acquires image, starts ImGui frame
{
  ... build ImGui UI, draw lists, view passes ...
  renderer::render_frame(passes);     // executes, composites UI, presents
}
```

Everything else is order-free: registration may happen any time after `init`
(including between the two calls), and passes are values.

## The API

```cpp
#pragma once

#include <SDL.h>
#include <optional>
#include <vector>
#include <vulkan/vulkan.h>   // escape-hatch section only

#include "asset.hpp"
#include "camera.hpp"
#include "color.hpp"
#include "linalg.hpp"
#include "span.hpp"

namespace client
{
namespace renderer
{

// --- Lifecycle ---

bool init(SDL_Window* window);
void shutdown();
void process_event(const SDL_Event* event);

// False means skip this frame entirely (minimized / swapchain rebuild) —
// build no UI, call no render_frame.
[[nodiscard]] bool new_frame();

// --- Handles ---
// Renderer-owned storage. Invalid by default; registration returns invalid on
// failure (already logged) — same shape as assets::load_mesh. Submitting a
// draw with an invalid mesh handle logs and skips; it never renders silently
// wrong.

struct mesh_handle_t     { uint32_t index = UINT32_MAX; bool valid() const { return index != UINT32_MAX; } };
struct texture_handle_t  { uint32_t index = UINT32_MAX; bool valid() const { return index != UINT32_MAX; } };
struct material_handle_t { uint32_t index = UINT32_MAX; bool valid() const { return index != UINT32_MAX; } };

// --- Materials ---

enum class shader_t     : uint8_t { lit, unlit };
enum class blend_mode_t : uint8_t { opaque, alpha };
enum class cull_mode_t  : uint8_t { back, none };
enum class fill_mode_t  : uint8_t { solid, wireframe };

// Everything that decides WHICH pipeline a material renders through. Resolved
// against the internal pipeline cache at register_material time; the cache key
// is (pipeline_state_t, vertex layout, fill_mode) — the last two come from the
// mesh and the draw.
struct pipeline_state_t
{
  shader_t     shader      = shader_t::lit;
  blend_mode_t blend_mode  = blend_mode_t::opaque;
  cull_mode_t  cull_mode   = cull_mode_t::back;
  bool         depth_test  = true;
  bool         depth_write = true;
};

// Freely mutable; no pipeline consequences. Invalid texture = flat colour.
// A texture that was NAMED but failed to load renders the magenta
// checkerboard (renderer-internal), never silently flat.
struct material_parameters_t
{
  texture_handle_t base_color_texture;
  vec4f            base_color = {1, 1, 1, 1};
};

struct material_t
{
  pipeline_state_t      pipeline_state;   // register-time identity; change = new material
  material_parameters_t parameters;       // mutable in place via update_material
};

// --- Registration (all GPU upload happens HERE, never at draw time) ---

// `srgb` is about what the bytes MEAN: true for authored colour (albedo),
// false for data (normals, roughness). Same rule as today.
texture_handle_t  register_texture(const assets::texture_asset_t& texture, bool srgb);

material_handle_t register_material(const material_t& material);

// Parameters only. A pipeline_state change is a new material — register again.
void update_material(material_handle_t handle, const material_parameters_t& parameters);

// Uploads vertices/indices/skin, and registers the asset's own materials as
// the mesh's default material table (slot order = the asset's material order).
mesh_handle_t register_mesh(const assets::mesh_asset_t& mesh);

// Re-upload changed geometry (displacement sculpting). Replaces
// invalidate_mesh_gpu: the upload happens now, not lazily at next draw.
void update_mesh(mesh_handle_t handle, const assets::mesh_asset_t& mesh);

// --- Draws ---

struct mesh_draw_t
{
  mesh_handle_t                 mesh;
  linalg::mat4f                 transform;               // caller composes TRS; renderer derives the normal matrix
  Span<const material_handle_t> material_overrides = {}; // empty = mesh's default table, indexed by material_slot
  Span<const linalg::mat4f>     pose               = {}; // skinned mesh only; empty = bind pose.
                                                         // Length must equal the skeleton's bone count;
                                                         // a mismatch logs and draws bind pose.
  color_t                       tint               = colors::white;
  fill_mode_t                   fill               = fill_mode_t::solid;
};

// --- Views and passes ---

struct viewport_t
{
  linalg::vec2 start;       // normalized
  linalg::vec2 dimensions;
};

struct render_view_t
{
  viewport_t viewport;
  camera_t   camera;
};

// Defined further down; named here because a pass points at them.
struct debug_draw_list_t;
struct custom_draw_t;

// One camera's worth of frame. A second camera (editor panel, scope PiP) is
// simply a second pass in the span handed to render_frame.
//
// `debug` is a POINTER because the list is optional and non-owning: null means
// "no debug draws" (a shipping view, a PiP pass), and the caller owns the list
// -- typically a state member reused across frames so vector capacity stays
// warm. By value would copy vectors every frame; Span does not fit because the
// list is several parallel vectors, not one array.
struct view_pass_t
{
  render_view_t                             view;
  Span<const mesh_draw_t>                   draws;              // sorted internally by pipeline, then material
  const debug_draw_list_t*                  debug     = nullptr;
  Span<const particle_emitter_parameters_t> particles = {};     // compute sequenced before the render pass internally
  Span<const custom_draw_t>                 custom    = {};     // escape hatch, see below
};

// Executes the whole frame: particle compute, render pass, every view pass in
// order, ImGui composite, submit, present. The pass structure is internal.
void render_frame(Span<const view_pass_t> passes);

// --- Debug drawing ---
// Immediate-style geometry that doesn't earn a material: editor overlays,
// hitboxes, gizmos. Append during update, hand the list to a view_pass_t.
// A list is a value — plain vectors, retired by the caller each frame (see
// "Lifetimes" in the design section).

// The stored primitives. Every append helper bottoms out in these; the
// renderer only ever sees four flat arrays.
struct debug_line_t
{
  linalg::vec3 start;
  linalg::vec3 end;
  color_t      color;
  float        depth_bias;
  float        remaining_seconds;   // <= 0 dies on next retire()
};

struct debug_polygon_t
{
  uint32_t first_vertex;   // slice of debug_draw_list_t::polygon_vertices
  uint32_t vertex_count;
  color_t  color;
  float    remaining_seconds;
};

struct debug_text_t
{
  linalg::vec3 position;
  std::string  text;
  color_t      color;
  float        remaining_seconds;
};

struct debug_draw_list_t
{
  std::vector<debug_line_t>    lines;
  std::vector<linalg::vec3>    polygon_vertices;  // one pool, sliced by polygons
  std::vector<debug_polygon_t> polygons;
  std::vector<debug_text_t>    texts;

  // Per-frame maintenance — call once at frame start. Subtracts delta_seconds
  // from every entry and removes the expired. Entries appended with the
  // default lifetime of 0 die here, i.e. they draw for exactly the frame they
  // were appended in — the old clear-per-frame semantics. Keeps capacity.
  void retire(float delta_seconds);
  void clear();   // wholesale drop (map switch, state exit)

  // Append helpers — typed push_backs, nothing more.
  // depth_bias replaces the sticky set_line_depth_bias: per call, more
  // negative pulls toward the camera. 0 = none.
  // `seconds` is how long the entry outlives this frame: 0 (default) = this
  // frame only; > 0 = survives retire() until the time runs out.
  void line(const linalg::vec3& start, const linalg::vec3& end, color_t color,
            float depth_bias = 0.0f, float seconds = 0.0f);
  void aabb(const linalg::vec3& min, const linalg::vec3& max, color_t color,
            fill_mode_t fill = fill_mode_t::wireframe, float seconds = 0.0f);
  // Solid, alpha-blended, triangle-fan decomposed. Replaces
  // draw_filled_polygon; the once-per-frame reset call is gone — the list IS
  // the frame scope.
  void filled_polygon(Span<const linalg::vec3> vertices, color_t color,
                      float seconds = 0.0f);
  // Compositions — decompose into `lines` entries at append time:
  void arrow(const linalg::vec3& start, const linalg::vec3& end, color_t color,
             float seconds = 0.0f);
  void wedge(const shared::wedge_t& wedge, color_t color, float seconds = 0.0f);
  void wire_sphere(const linalg::vec3& center, float radius, color_t color,
                   float seconds = 0.0f);
  void wire_capsule(const linalg::vec3& center, float radius, float half_height,
                    color_t color, float seconds = 0.0f);
  // Projected via the pass's view, composited over the 3D scene, not
  // depth-tested.
  void text(const linalg::vec3& world_position, const char* text, color_t color,
            float seconds = 0.0f);
};

// --- Overlay text ---

// Temporary banner at the top of the screen for ~3s. Fire-and-forget.
void draw_announcement(const char* text);

// --- Utilities ---

// World point -> pixels through THIS view and the current swapchain extent.
// Pure function of its arguments — no dependency on any prior call.
// nullopt = behind a perspective camera's eye.
[[nodiscard]] std::optional<linalg::vec2>
try_project_to_screen(const render_view_t& view, const linalg::vec3& world);

// GPU supports fillModeNonSolid; false means wireframe fills draw solid.
bool wireframe_supported();

// --- Escape hatch: raw Vulkan access ---
// For code that builds its own pipelines (shader_tool_runtime,
// shader_editor_state). Everything below is outside the draw-list model;
// nothing in the game path may use it.

VkDevice         get_VkDevice();
VkPhysicalDevice get_VkPhysicalDevice();
VkRenderPass     get_VkRenderPass();
uint32_t         get_current_frame_index();
int              get_max_frames_in_flight();

// Recorded inside the render pass, after the pass's own draws, viewport
// already applied. This is how the shader editor's custom pipelines keep
// working without begin_render_pass being public.
struct custom_draw_t
{
  void (*record)(VkCommandBuffer cmd, void* user) = nullptr;
  void* user = nullptr;
};

// Owning GPU texture for custom pipelines (PBR preview). The draw-list path
// never sees this type — it uses texture_handle_t.
struct gpu_texture_t
{
  VkImage        image   = VK_NULL_HANDLE;
  VkDeviceMemory memory  = VK_NULL_HANDLE;
  VkImageView    view    = VK_NULL_HANDLE;
  VkSampler      sampler = VK_NULL_HANDLE;
  bool valid() const { return image != VK_NULL_HANDLE; }
};
gpu_texture_t upload_texture(const assets::texture_asset_t* texture, bool srgb = false);
void          destroy_texture(gpu_texture_t& texture);

struct mesh_gpu_info_t
{
  VkBuffer vertex_buffer;
  VkBuffer index_buffer;
  uint32_t index_count;
};
[[nodiscard]] std::optional<mesh_gpu_info_t> try_get_mesh_gpu_info(mesh_handle_t handle);

} // namespace renderer
} // namespace client
```

## Decisions, and why

- **`material_index` → `material_slot` + two tables.** A submesh names a slot;
  the mesh carries the default table (from its asset's materials);
  `mesh_draw_t.material_overrides` swaps the table per instance. This buys
  per-instance material overrides (team skins, damage states) without touching
  the mesh. The draft `render_component_t{mesh, materials}` is exactly
  `(mesh, material_overrides)` and stops being a distinct type.

- **No `shader_t*` and no pipeline handle on the material.** The caller hands
  over `pipeline_state_t`; resolution happens once at registration. Holding
  loose state booleans that the draw path interprets per draw would rebuild
  today's decision tree, just reading from a struct instead of from draw
  parameters. The parameters/state split is the "MATERIAL DATA" vs "PIPELINE
  STATE" distinction from the pipeline notes in todo.md, made literal.

- **`fill` on the draw, in the pipeline key, not on the material.** Wireframe
  is a way of *looking at a draw*, and this is what makes skinned-wireframe
  fall out of the cache instead of being a hand-managed special pipeline.

- **`transform` is a `mat4f`**, not position/rotation/scale. One general type;
  euler composition moves to the call site; the renderer keeps zero rotation
  math (the hand-rolled `mat4_t` dies with it — `linalg` is the one math
  library).

- **Pose rides the draw item** as a `Span`, length-checked against the
  skeleton's bone count, empty = bind pose. Same contract as today's
  `skinning_matrices` pointer+count pair, minus the raw pointer+count pair.
  The per-frame bone upload keeps the existing `frame_uniform_allocator_t`
  (bump-per-frame, double-buffered across frames) — that machinery was built
  2026-08-09 and carries over unchanged.

- **Sorting is inside `render_frame`** (by pipeline, then material). Cheap at
  this game's draw counts; keeps every call site dumb.

- **Particle compute stops being caller-sequenced.** Emitters ride the pass;
  the renderer orders compute before the render pass, because that ordering is
  a Vulkan fact, not a caller decision.

- **`shader_type::Textured` (the displacement path) dies** — a displacement
  becomes an ordinary lit material with a texture. The whole
  `g_disp_texture_*` global cluster folds into ordinary registration.

- **The fallback ladder carries over as-is**: invalid texture handle → flat
  white (colour multiplies out of the shader), named-but-failed texture →
  magenta/black checkerboard. Both are renderer-internal textures; the rule
  that a broken asset must never be mistakable for authored art is unchanged.

- **`renderer::material_t` coexists with `assets::material_t`** (the parsed
  .mesh/.mtl data — the input you build a renderer material from). If the pair
  ever reads badly at call sites, the asset-side one is the rename candidate
  (`parsed_material_t`), not this one.

- **The entity `Render` component is untouched.** It keeps its manifest mesh
  id; the client resolves id → `mesh_handle_t` once and builds `mesh_draw_t`s
  per frame. Nothing in this design crosses the schema/wire seam, so
  `SCHEMA_HASH` and the `.def` family are unaffected.

- **`draw_AABB`'s shader random-color mode dies.** One caller
  (geometry_editor's per-uid box colors); the call site hashes the uid to a
  `color_t` on the CPU instead. The `use_random_color` push-constant plumbing
  goes with it.

## Debug drawing: why a caller-owned list

Two standard shapes exist for debug drawing: immediate global functions over a
hidden batch (Unity `Debug.DrawLine`, Unreal `DrawDebugLine`), and a
caller-owned list you fill and hand over (ImGui's `ImDrawList`, im3d). Both
are common. **The current renderer is already secretly the first shape**:
`draw_line` ignores its `VkCommandBuffer` parameter and appends to a global
batch flushed at end of frame; the filled-polygon ring buffer with its
`reset_debug_face_buffer()` protocol is the same thing. So the list is not new
machinery — it takes the batch that already exists, makes it a value, and
hands ownership to the caller.

The reason to prefer the caller-owned form here is single: **once the view is
a parameter, a global batch cannot answer "which viewport do these lines
render into?"** With one camera, ambient state works — which is why it never
hurt. With the animation-tool panel / PiP it either draws editor gizmos into
the scope overlay or needs a "current debug target" setting, which is
`set_view` reborn. Attaching the list to the pass is the data-flow answer.
Deleting the two sticky bits (`set_line_depth_bias`, the reset call) falls out
as a side effect.

**`overlay_renderer_t` is deleted, not inherited.** It is a virtual interface
with exactly one real implementation (`VulkanOverlayRenderer` in
tool_editor_state.cpp) that forwards one-to-one to `renderer::draw_*`, and its
`get_command_buffer()` leaks the very type it was built to hide. Editor tools
take a `debug_draw_list_t&` instead, which achieves what the interface was
for, better: no Vulkan types at all (no `get_command_buffer()` hole possible),
no virtuals, and the editor compile-isolation test still works because filling
a vector needs no GPU. Net: one type replaces three current mechanisms — the
hidden line batch, the polygon ring buffer + reset protocol, and
`overlay_renderer_t` entirely.

### Lifetimes

Two debug-draw lifetimes exist, and only one was served by the sticky-global
design:

- **Per-frame, regenerated from state.** Hitboxes, selection outlines, bone
  labels: the source of truth lives in game/editor state, the caller
  re-appends every frame, and the entry should die with the frame. This is the
  default — append with no lifetime, `retire()` drops it next frame.
- **One-shot events.** A hitscan trace, a server-reported hit position, a
  pathfinding probe: the event fires ONCE, in a fixed tick or a network
  handler — a different rate than render frames — and with pure per-frame
  lifetime it would be visible for exactly one frame, i.e. invisible. These
  append with `seconds > 0` and survive `retire()` until the time runs out.

The lifetime lives IN the list (`remaining_seconds` per entry) rather than
anywhere else, because both alternatives are worse:

- a **renderer-retained store** (Unreal's `DrawDebugLine(..., duration)`
  shape) is hidden cross-frame state with an ambiguous view — which pass draws
  it? — exactly the shape this design deletes;
- a **separate caller-side timed store** that drains into the frame list keeps
  the frame list "pure", but then every system wanting a 2-second marker owns
  an extra object and must remember the drain call — two types for one
  concept, and the plumbing is the part that gets forgotten.

`retire(delta_seconds)` takes the caller's dt on purpose: whether debug draws
age by game time or wall time (what pausing does to them) is the caller's
decision, made by which dt it passes.

What a call site looks like:

```cpp
// Play_State member — lives across frames; timed entries make this the one
// retained home for debug geometry:
renderer::debug_draw_list_t debug;

void Play_State::render(float delta_seconds)
{
  debug.retire(delta_seconds);   // expired entries drop; last frame's
                                 // per-frame entries die here

  if (cvars.debug_show_hitboxes)
    for (const posed_hitbox_t& hitbox : posed_hitboxes)
      debug.wire_capsule(hitbox.center, hitbox.radius, hitbox.half_height, colors::green);

  renderer::view_pass_t pass;
  pass.view  = {viewport, camera};
  pass.draws = mesh_draws;     // Span over this frame's mesh_draw_t vector
  pass.debug = &debug;
  renderer::render_frame(Span<const renderer::view_pass_t>(&pass, 1));
}

// Elsewhere, in an event that fires once (a fixed tick, a network message):
debug.line(trace_start, trace_end, colors::red, /*depth_bias*/ 0.0f, /*seconds*/ 2.0f);
```

(Methods on the list vs free functions over it is taste, not structure; the
compositions — sphere = three circles, arrow = lines — must be written once
either way. Drafted as methods; flip if free functions read better.)

## What dies, and where each went

| Gone | Replaced by |
|---|---|
| `set_view` | `view_pass_t.view` — view is data, per pass |
| `begin_frame` / `begin_render_pass` / `end_frame` | `new_frame` + `render_frame`; pass structure internal |
| `VkCommandBuffer` on every `draw_*` | gone from the game path; `custom_draw_t` for the shader tool |
| `set_line_depth_bias` (sticky) | `depth_bias` parameter on `debug_draw_list_t::line` |
| `reset_debug_face_buffer` | the list is caller-owned; `retire(dt)` at frame start |
| `draw_mesh(handle, parameters)` | `mesh_draw_t` in a pass |
| `mesh_draw_parameters_t` euler pos/rot/scale | `transform` mat4 composed at call site |
| `shader_type::Textured` + `g_disp_texture_*` | ordinary lit material with a texture |
| `invalidate_mesh_gpu` | `update_mesh` (eager re-upload) |
| `draw_AABB` random-color mode, `draw__wire_AABB` | `debug_draw_list_t::aabb`; per-uid color hashed at call site |
| `overlay_renderer_t` + `VulkanOverlayRenderer` | tools take `debug_draw_list_t&` |
| nine named `g_*_pipeline` globals | pipeline cache keyed on (pipeline_state_t, vertex layout, fill) |
| hand-rolled `mat4_t` | `linalg` |
| lazy upload inside `draw_mesh` / `material_texture_descriptor_set` | `register_*` at load time |

## Deferred / open

- **PBR material fields** (normal / metallic-roughness / occlusion / emissive
  textures, metallic/roughness/emissive scalars): declared in the original
  draft, deliberately absent here. They arrive WITH the shaders that read
  them, not before. `material_parameters_t` is the growth point.
- **Unregistration** (freeing GPU meshes/textures/materials): not in v1.
  Nothing unloads assets today either; `update_mesh` covers the one mutation
  case (displacements). Add when a real caller exists.
- **Migration order** (rough, bottom-up, unscheduled): pipeline cache +
  registration layer first (new code beside old); then `render_frame` with the
  old draw functions reimplemented on top; then call sites converted per
  state/tool; then the old API and `overlay_renderer_t` deleted. The escape
  hatch (shader tool) converts last via `custom_draw_t`.
- Whether `debug_draw_list_t` uses methods or free functions — taste, see
  above.
