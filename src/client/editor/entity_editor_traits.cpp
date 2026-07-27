#include "../../shared/player_constants.hpp"
#include "../../shared/entities/entity_reflection.hpp"
#include "entity_editor_traits.hpp"
#include "../../shared/asset.hpp"
#include "../../shared/editor_grid.hpp"
#include "../../shared/map.hpp"
#include "../../shared/shapes.hpp"
#include "renderer.hpp"
#include <cmath>
#include <cstring>
#include <print>
#include <string>

// Pull in all entity headers so the X-macro can see every class.
#define ENTITIES_WANT_INCLUDES
#undef ENTITIES_WANT_INCLUDES

namespace client
{

// ===================================================================
// Template specializations — one per entity type in entities.def.
// If you add a new entity and forget to specialize, the linker will
// tell you exactly which instantiation is missing.
// ===================================================================

// -- Player Spawn -------------------------------------------------------

template <>
linalg::vec3
Entity_Editor_Traits<entities::Player_Spawn_Entity>::get_half_extents(
    const entities::Player_Spawn_Entity *)
{
  return {shared::player_half_width, shared::player_half_height,
          shared::player_half_width};
}

template <>
bool Entity_Editor_Traits<entities::Player_Spawn_Entity>::draw_ghost(
    const entities::Player_Spawn_Entity *, overlay_renderer_t &renderer,
    const linalg::vec3 &center)
{
  const linalg::vec3 hull{shared::player_half_width,
                          shared::player_half_height,
                          shared::player_half_width};
  renderer.draw_wire_box(center, hull, colors::pink);
  renderer.draw_line(center, center + linalg::vec3{0, 48, 0}, colors::pink);
  return true;
}

template <>
bool Entity_Editor_Traits<entities::Player_Spawn_Entity>::draw_in_editor(
    const entities::Player_Spawn_Entity *e, overlay_renderer_t &renderer,
    uint32_t, bool)
{
  const linalg::vec3 hull{shared::player_half_width,
                          shared::player_half_height,
                          shared::player_half_width};
  renderer.draw_wire_box(e->position, hull, colors::pink);
  renderer.draw_line(e->position, e->position + linalg::vec3{0, 48, 0}, colors::pink);
  return true;
}

template <>
bool Entity_Editor_Traits<entities::Player_Spawn_Entity>::draw_selection_wireframe(
    const entities::Player_Spawn_Entity *, overlay_renderer_t &, color_t, float)
{
  return false; // use AABB fallback
}

// -- Particle Emitter ---------------------------------------------------

template <>
linalg::vec3
Entity_Editor_Traits<entities::Particle_Emitter_Entity>::get_half_extents(
    const entities::Particle_Emitter_Entity *)
{
  return {0, 0, 0}; // point entity
}

template <>
bool Entity_Editor_Traits<entities::Particle_Emitter_Entity>::draw_ghost(
    const entities::Particle_Emitter_Entity *, overlay_renderer_t &renderer,
    const linalg::vec3 &center)
{
  constexpr float r = 16.f;
  renderer.draw_line(center + linalg::vec3{-r, 0, 0},
                     center + linalg::vec3{r, 0, 0}, colors::gold);
  renderer.draw_line(center + linalg::vec3{0, 0, -r},
                     center + linalg::vec3{0, 0, r}, colors::gold);
  renderer.draw_line(center, center + linalg::vec3{0, 32, 0}, colors::gold);
  return true;
}

template <>
bool Entity_Editor_Traits<entities::Particle_Emitter_Entity>::draw_in_editor(
    const entities::Particle_Emitter_Entity *e, overlay_renderer_t &renderer,
    uint32_t, bool)
{
  constexpr float r = 16.f;
  renderer.draw_line(e->position + linalg::vec3{-r, 0, 0},
                     e->position + linalg::vec3{r, 0, 0}, colors::gold);
  renderer.draw_line(e->position + linalg::vec3{0, 0, -r},
                     e->position + linalg::vec3{0, 0, r}, colors::gold);
  renderer.draw_line(e->position, e->position + linalg::vec3{0, 32, 0}, colors::gold);
  return true;
}

template <>
bool Entity_Editor_Traits<entities::Particle_Emitter_Entity>::draw_selection_wireframe(
    const entities::Particle_Emitter_Entity *, overlay_renderer_t &, color_t, float)
{
  return false; // use AABB fallback
}

// -- Weapon -------------------------------------------------------------

template <>
linalg::vec3
Entity_Editor_Traits<entities::Weapon_Entity>::get_half_extents(
    const entities::Weapon_Entity *)
{
  return {editor::DEFAULT_HALF_EXTENT, editor::DEFAULT_HALF_EXTENT,
          editor::DEFAULT_HALF_EXTENT};
}

template <>
bool Entity_Editor_Traits<entities::Weapon_Entity>::draw_ghost(
    const entities::Weapon_Entity *, overlay_renderer_t &, const linalg::vec3 &)
{
  return false; // has render component, use default
}

template <>
bool Entity_Editor_Traits<entities::Weapon_Entity>::draw_in_editor(
    const entities::Weapon_Entity *, overlay_renderer_t &, uint32_t, bool)
{
  return false; // relies on render component
}

template <>
bool Entity_Editor_Traits<entities::Weapon_Entity>::draw_selection_wireframe(
    const entities::Weapon_Entity *, overlay_renderer_t &, color_t, float)
{
  return false; // try render component mesh first
}

// -- Player (runtime only, not placed in editor) ------------------------

template <>
linalg::vec3
Entity_Editor_Traits<entities::Player_Entity>::get_half_extents(
    const entities::Player_Entity *)
{
  return {shared::player_half_width, shared::player_half_height,
          shared::player_half_width};
}

template <>
bool Entity_Editor_Traits<entities::Player_Entity>::draw_ghost(
    const entities::Player_Entity *, overlay_renderer_t &, const linalg::vec3 &)
{
  return false;
}

template <>
bool Entity_Editor_Traits<entities::Player_Entity>::draw_in_editor(
    const entities::Player_Entity *e, overlay_renderer_t &renderer,
    uint32_t, bool)
{
  const char *mesh_path = "resources/obj/pyramid.obj";
  if (mesh_path)
  {
    auto mesh_handle = assets::load_mesh(mesh_path);
    if (mesh_handle.valid())
    {
      renderer::draw_mesh(renderer.get_command_buffer(), mesh_handle,
                         {.position = e->position,
                          .rotation = e->orientation,
                          .wireframe = true});
      return true;
    }
  }
  return false;
}

template <>
bool Entity_Editor_Traits<entities::Player_Entity>::draw_selection_wireframe(
    const entities::Player_Entity *e, overlay_renderer_t &renderer, color_t color,
    float)
{
  const char *mesh_path = "resources/obj/pyramid.obj";
  auto mesh_handle = assets::load_mesh(mesh_path);
  if (!mesh_handle.valid())
    return false;
  if (!renderer::WireframeSupported())
    return false;
  renderer::draw_mesh(renderer.get_command_buffer(), mesh_handle,
                     {.position  = e->position,
                      .rotation  = e->orientation,
                      .color     = color,
                      .wireframe = true});
  return true;
}

// -- Trigger Volume -----------------------------------------------------

template <>
linalg::vec3
Entity_Editor_Traits<entities::Trigger_Volume_Entity>::get_half_extents(
    const entities::Trigger_Volume_Entity *e)
{
  return e->volume.half_extents;
}

template <>
bool Entity_Editor_Traits<entities::Trigger_Volume_Entity>::draw_ghost(
    const entities::Trigger_Volume_Entity *e, overlay_renderer_t &renderer,
    const linalg::vec3 &center)
{
  renderer.draw_wire_box(center, e->volume.half_extents, colors::red); // red
  return true;
}

template <>
bool Entity_Editor_Traits<entities::Trigger_Volume_Entity>::draw_in_editor(
    const entities::Trigger_Volume_Entity *e, overlay_renderer_t &renderer,
    uint32_t, bool)
{
  renderer.draw_wire_box(e->position, e->volume.half_extents, colors::red); // red
  return true;
}

template <>
bool Entity_Editor_Traits<entities::Trigger_Volume_Entity>::draw_selection_wireframe(
    const entities::Trigger_Volume_Entity *e, overlay_renderer_t &renderer,
    color_t color, float)
{
  renderer.draw_wire_box(e->position, e->volume.half_extents, color);
  return true;
}

// -- Rocket (runtime only, not placed in editor) ------------------------

template <>
linalg::vec3
Entity_Editor_Traits<entities::Rocket_Entity>::get_half_extents(
    const entities::Rocket_Entity *)
{
  return {editor::DEFAULT_HALF_EXTENT, editor::DEFAULT_HALF_EXTENT,
          editor::DEFAULT_HALF_EXTENT};
}

template <>
bool Entity_Editor_Traits<entities::Rocket_Entity>::draw_ghost(
    const entities::Rocket_Entity *, overlay_renderer_t &, const linalg::vec3 &)
{
  return false;
}

template <>
bool Entity_Editor_Traits<entities::Rocket_Entity>::draw_in_editor(
    const entities::Rocket_Entity *, overlay_renderer_t &, uint32_t, bool)
{
  return false; // runtime only
}

template <>
bool Entity_Editor_Traits<entities::Rocket_Entity>::draw_selection_wireframe(
    const entities::Rocket_Entity *, overlay_renderer_t &, color_t, float)
{
  return false;
}

// -- Light (point/spot/directional, drawn as a cross in editor) ------

template <>
linalg::vec3
Entity_Editor_Traits<entities::Light_Entity>::get_half_extents(
    const entities::Light_Entity *)
{
  return {editor::DEFAULT_HALF_EXTENT, editor::DEFAULT_HALF_EXTENT,
          editor::DEFAULT_HALF_EXTENT};
}

template <>
bool Entity_Editor_Traits<entities::Light_Entity>::draw_ghost(
    const entities::Light_Entity *, overlay_renderer_t &renderer,
    const linalg::vec3 &center)
{
  float size = 0.3f;
  renderer.draw_line(center - linalg::vec3{size, 0, 0},
                     center + linalg::vec3{size, 0, 0}, colors::yellow);
  renderer.draw_line(center - linalg::vec3{0, size, 0},
                     center + linalg::vec3{0, size, 0}, colors::yellow);
  renderer.draw_line(center - linalg::vec3{0, 0, size},
                     center + linalg::vec3{0, 0, size}, colors::yellow);
  return true;
}

template <>
bool Entity_Editor_Traits<entities::Light_Entity>::draw_in_editor(
    const entities::Light_Entity *e, overlay_renderer_t &renderer,
    uint32_t, bool)
{
  float size = 0.3f;
  linalg::vec3 p = e->position;
  renderer.draw_line(p - linalg::vec3{size, 0, 0},
                     p + linalg::vec3{size, 0, 0}, colors::yellow);
  renderer.draw_line(p - linalg::vec3{0, size, 0},
                     p + linalg::vec3{0, size, 0}, colors::yellow);
  renderer.draw_line(p - linalg::vec3{0, 0, size},
                     p + linalg::vec3{0, 0, size}, colors::yellow);
  return true;
}

template <>
bool Entity_Editor_Traits<entities::Light_Entity>::draw_selection_wireframe(
    const entities::Light_Entity *e, overlay_renderer_t &renderer,
    color_t color, float)
{
  float size = 0.4f;
  linalg::vec3 p = e->position;
  renderer.draw_line(p - linalg::vec3{size, 0, 0},
                     p + linalg::vec3{size, 0, 0}, color);
  renderer.draw_line(p - linalg::vec3{0, size, 0},
                     p + linalg::vec3{0, size, 0}, color);
  renderer.draw_line(p - linalg::vec3{0, 0, size},
                     p + linalg::vec3{0, 0, size}, color);
  return true;
}

// -- Physics_Body (runtime only, server-spawned via console) ------------

template <>
linalg::vec3
Entity_Editor_Traits<entities::Physics_Body_Entity>::get_half_extents(
    const entities::Physics_Body_Entity *e)
{
  return e ? e->size
           : linalg::vec3{editor::DEFAULT_HALF_EXTENT,
                          editor::DEFAULT_HALF_EXTENT,
                          editor::DEFAULT_HALF_EXTENT};
}

template <>
bool Entity_Editor_Traits<entities::Physics_Body_Entity>::draw_ghost(
    const entities::Physics_Body_Entity *, overlay_renderer_t &,
    const linalg::vec3 &)
{
  return false;
}

template <>
bool Entity_Editor_Traits<entities::Physics_Body_Entity>::draw_in_editor(
    const entities::Physics_Body_Entity *, overlay_renderer_t &, uint32_t, bool)
{
  return false; // runtime only — never appears in editor
}

template <>
bool Entity_Editor_Traits<entities::Physics_Body_Entity>::draw_selection_wireframe(
    const entities::Physics_Body_Entity *, overlay_renderer_t &, color_t, float)
{
  return false;
}

// ===================================================================
// Dispatch: runtime entity -> compile-time specialization
//
// Each of the three dispatchers below is one switch over the closed enum,
// where it used to be an X-macro expanding to a chain of dynamic_casts. Two
// things improved at once: an unhandled entity type is now a -Wswitch warning
// at compile time instead of a silent fall-through to the default, and the
// per-entity lookup is an integer jump rather than up to eight RTTI walks.
//
// ENTITY_DISPATCH keeps them to one line per type. A macro is still the least
// bad option here -- the alternative is three near-identical eight-case
// switches -- but it expands over the GENERATED enum, so it cannot go out of
// sync with the .def the way the X-macro list could.
// ===================================================================

#define ENTITY_DISPATCH(CALL)                                                  \
  switch (e->type)                                                             \
  {                                                                            \
    case entities::entity_type::Player_Spawn_Entity:                           \
      CALL(entities::Player_Spawn_Entity);                                     \
    case entities::entity_type::Player_Entity:                                 \
      CALL(entities::Player_Entity);                                           \
    case entities::entity_type::Weapon_Entity:                                 \
      CALL(entities::Weapon_Entity);                                           \
    case entities::entity_type::Rocket_Entity:                                 \
      CALL(entities::Rocket_Entity);                                           \
    case entities::entity_type::Particle_Emitter_Entity:                       \
      CALL(entities::Particle_Emitter_Entity);                                 \
    case entities::entity_type::Trigger_Volume_Entity:                         \
      CALL(entities::Trigger_Volume_Entity);                                   \
    case entities::entity_type::Light_Entity:                                  \
      CALL(entities::Light_Entity);                                            \
    case entities::entity_type::Physics_Body_Entity:                           \
      CALL(entities::Physics_Body_Entity);                                     \
    case entities::entity_type::Invalid:                                       \
      break;                                                                   \
  }

linalg::vec3 get_placement_half_extents(const entities::Entity *e)
{
  // Box-volume entities share one path: any entity that owns a Box_Volume
  // component reports its extents through it, no per-type trait code required.
  // Trigger_Volume is the only such entity left now that geometry has moved out.
  if (const entities::Box_Volume *volume = entities::get_box_volume(e))
    return volume->half_extents;

#define DISPATCH_HALF_EXTENTS(CLASS)                                           \
  return Entity_Editor_Traits<CLASS>::get_half_extents(                        \
      static_cast<const CLASS *>(e))
  ENTITY_DISPATCH(DISPATCH_HALF_EXTENTS)
#undef DISPATCH_HALF_EXTENTS

  return {editor::DEFAULT_HALF_EXTENT, editor::DEFAULT_HALF_EXTENT,
          editor::DEFAULT_HALF_EXTENT};
}

bool draw_entity_ghost(const entities::Entity *e, overlay_renderer_t &renderer,
                       const linalg::vec3 &center)
{
#define DISPATCH_GHOST(CLASS)                                                  \
  return Entity_Editor_Traits<CLASS>::draw_ghost(static_cast<const CLASS *>(e),\
                                                 renderer, center)
  ENTITY_DISPATCH(DISPATCH_GHOST)
#undef DISPATCH_GHOST

  return false;
}

// ===================================================================
// Editor drawing: render-component path + per-entity trait fallback
// ===================================================================

// Try to draw an entity via its render_component_t. Returns true on success.
static bool try_draw_render_component(const entities::Entity *e,
                                      VkCommandBuffer cmd)
{
  const entities::Render *rc = entities::get_render(e);
  if (!rc || !rc->visible)
    return false;

  assets::asset_handle_t<assets::mesh_asset_t> mesh_handle = assets::get_mesh(rc->mesh);
  if (!mesh_handle.valid())
    return false;

  renderer::draw_mesh(cmd, mesh_handle,
                     {.position = e->position,
                      .scale    = rc->scale,
                      .rotation = e->orientation + rc->rotation});
  return true;
}

bool draw_entity_in_editor(const entities::Entity *e,
                           overlay_renderer_t &renderer, uint32_t uid,
                           bool solid)
{
  // First: try the render component (common to all entity types).
  if (try_draw_render_component(e, renderer.get_command_buffer()))
    return true;

  // Second: per-entity trait specialization.
#define DISPATCH_DRAW(CLASS)                                                   \
  return Entity_Editor_Traits<CLASS>::draw_in_editor(                          \
      static_cast<const CLASS *>(e), renderer, uid, solid)
  ENTITY_DISPATCH(DISPATCH_DRAW)
#undef DISPATCH_DRAW

  return false;
}

// ===================================================================
// Selection highlight: pulsating pink <-> white wireframe
// ===================================================================

color_t compute_selection_pulse_color(float time)
{
  // Pulsate between hot pink and white at ~2 Hz.
  float t = std::sin(time * 4.0f) * 0.5f + 0.5f; // 0..1

  auto lerp_byte = [](uint8_t a, uint8_t b, float t) -> uint8_t
  {
    return (uint8_t)(a + (b - a) * t);
  };

  const color_t from = colors::hot_pink;
  const color_t to   = colors::white;
  return color_t{lerp_byte(from.r, to.r, t), lerp_byte(from.g, to.g, t),
                 lerp_byte(from.b, to.b, t), 255};
}

// Try to draw a mesh wireframe from the entity's render component.
static bool try_draw_mesh_selection_wireframe(const entities::Entity *e,
                                              VkCommandBuffer cmd,
                                              color_t color)
{
  const entities::Render *rc = entities::get_render(e);
  if (!rc || !rc->visible)
    return false;

  assets::asset_handle_t<assets::mesh_asset_t> mesh_handle = assets::get_mesh(rc->mesh);
  if (!mesh_handle.valid())
    return false;

  if (!renderer::WireframeSupported())
    return false;

  renderer::draw_mesh(cmd, mesh_handle,
                     {.position  = e->position,
                      .scale     = rc->scale,
                      .rotation  = e->orientation + rc->rotation,
                      .color     = color,
                      .wireframe = true});
  return true;
}

// Runtime dispatch for draw_selection_wireframe trait.
static bool dispatch_selection_wireframe(const entities::Entity *e,
                                         overlay_renderer_t &renderer,
                                         color_t color, float grid_step)
{
#define DISPATCH_WIREFRAME(CLASS)                                              \
  return Entity_Editor_Traits<CLASS>::draw_selection_wireframe(                \
      static_cast<const CLASS *>(e), renderer, color, grid_step)
  ENTITY_DISPATCH(DISPATCH_WIREFRAME)
#undef DISPATCH_WIREFRAME

  return false;
}

void draw_selection_highlight(const entities::Entity *e,
                              overlay_renderer_t &renderer, float time,
                              float grid_step)
{
  color_t color = compute_selection_pulse_color(time);

  // Push a very strong depth bias so the selection wireframe renders in front
  // of the solid barycentric mesh. The constant factor dominates for
  // flat-facing surfaces; the slope factor helps for oblique angles.
  renderer::SetLineDepthBias(-200.0f, -10.0f);

  // 1. Try mesh wireframe from render component
  if (try_draw_mesh_selection_wireframe(e, renderer.get_command_buffer(), color))
  {
    renderer::SetLineDepthBias(-2.0f, -1.0f);
    return;
  }

  // 2. Try per-entity shape wireframe (wedge, AABB, trigger volume, etc.)
  if (dispatch_selection_wireframe(e, renderer, color, grid_step))
  {
    renderer::SetLineDepthBias(-2.0f, -1.0f);
    return;
  }

  // 3. Fallback: AABB bounds wireframe
  auto bounds = shared::compute_entity_bounds(e);
  renderer.draw_wire_box((bounds.min + bounds.max) * 0.5f,
                         (bounds.max - bounds.min) * 0.5f, color);

  renderer::SetLineDepthBias(-2.0f, -1.0f);
}

// ===================================================================
// Default ghost drawing (render component -> wire box fallback)
// ===================================================================

void draw_default_ghost(const entities::Entity *e, overlay_renderer_t &renderer,
                        const linalg::vec3 &center)
{
  if (const entities::Render *rc = entities::get_render(e))
  {
    assets::asset_handle_t<assets::mesh_asset_t> mesh_handle = assets::get_mesh(rc->mesh);
    if (mesh_handle.valid())
    {
      renderer::draw_mesh(renderer.get_command_buffer(), mesh_handle,
                         {.position  = center,
                          .color     = colors::yellow,
                          .wireframe = true});
      return;
    }
  }

  // Fallback: wire box
  linalg::vec3 he = get_placement_half_extents(e);
  renderer.draw_wire_box(center, he, colors::yellow);
}

// ===================================================================
// Convenience
// ===================================================================

linalg::vec3 compute_placement_center(const entities::Entity *e,
                                      const linalg::vec3 &ghost_position)
{
  linalg::vec3 center = ghost_position;
  center.y += get_placement_half_extents(e).y;
  return center;
}

} // namespace client
