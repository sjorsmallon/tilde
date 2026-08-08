#include "../../shared/player_constants.hpp"
#include "../../shared/entities/entity_reflection.hpp"
#include "entity_editor_traits.hpp"
#include "../../shared/asset.hpp"
#include "../../shared/editor_grid.hpp"
#include "../../shared/map.hpp"
#include "../../shared/shapes.hpp"
#include "renderer.hpp"
#include <cmath>

namespace client
{

// ===================================================================
// Per-type drawing, as one exhaustive switch per function rather than a
// template specialized per entity type. The old version required four
// specializations per entity (get_half_extents / draw_ghost / draw_in_editor
// / draw_selection_wireframe) purely so a forgotten one became a linker
// error — but ENTITY_DISPATCH was already a switch over the closed enum, so
// that exhaustiveness came for free from -Wswitch too. Collapsing to plain
// switches also surfaces what was already true: Player_Spawn, Particle_
// Emitter, Trigger_Volume and Light draw the *same* shape for both the
// placement ghost and the in-editor view, just at a different position —
// they were being duplicated by the trait mechanism, not expressed by it.
// ===================================================================

namespace
{

// -- Shared per-type shapes (ghost + in-editor draw identically) --------

void draw_player_spawn_shape(overlay_renderer_t &renderer,
                             const linalg::vec3 &position, color_t color)
{
  // `position` is the entity ORIGIN, which for a spawn is where the player's
  // FEET go -- the same convention as player_eye_height and the hitbox table.
  // draw_wire_box takes a CENTER, so the hull is lifted half its height; it is
  // not centered on the origin.
  const linalg::vec3 hull{shared::player_half_width,
                          shared::player_half_height,
                          shared::player_half_width};
  renderer.draw_wire_box(position + linalg::vec3{0, shared::player_half_height, 0},
                         hull, color);

  // Marker spike, drawn from the top of the hull upward so it stays visible
  // instead of being buried inside the box.
  const float hull_top = 2.f * shared::player_half_height;
  renderer.draw_line(position + linalg::vec3{0, hull_top, 0},
                     position + linalg::vec3{0, hull_top + 24.f, 0}, color);
}

void draw_particle_emitter_shape(overlay_renderer_t &renderer,
                                 const linalg::vec3 &position, color_t color)
{
  constexpr float r = 16.f;
  renderer.draw_line(position + linalg::vec3{-r, 0, 0},
                     position + linalg::vec3{r, 0, 0}, color);
  renderer.draw_line(position + linalg::vec3{0, 0, -r},
                     position + linalg::vec3{0, 0, r}, color);
  renderer.draw_line(position, position + linalg::vec3{0, 32, 0}, color);
}

void draw_trigger_volume_shape(overlay_renderer_t &renderer,
                               const linalg::vec3 &position,
                               const linalg::vec3 &half_extents, color_t color)
{
  renderer.draw_wire_box(position, half_extents, color);
}

void draw_light_cross(overlay_renderer_t &renderer, const linalg::vec3 &position,
                      color_t color, float size)
{
  renderer.draw_line(position - linalg::vec3{size, 0, 0},
                     position + linalg::vec3{size, 0, 0}, color);
  renderer.draw_line(position - linalg::vec3{0, size, 0},
                     position + linalg::vec3{0, size, 0}, color);
  renderer.draw_line(position - linalg::vec3{0, 0, size},
                     position + linalg::vec3{0, 0, size}, color);
}

// Player_Entity has no placeable representation of its own (runtime-spawned);
// its "gizmo" is its actual mesh drawn in wireframe, used for the in-editor
// view and the selection outline (ghost falls back to the default box).
bool draw_player_entity_mesh(overlay_renderer_t &renderer,
                             const entities::Player_Entity *e, color_t color,
                             bool tinted)
{
  const char *mesh_path = "resources/obj/pyramid.obj";
  auto mesh_handle = assets::load_mesh(mesh_path);
  if (!mesh_handle.valid())
    return false;
  if (tinted && !renderer::WireframeSupported())
    return false;

  renderer::mesh_draw_parameters_t parameters{
      .position  = e->position,
      .rotation  = e->orientation,
      .wireframe = true,
  };
  if (tinted)
    parameters.color = color;

  renderer::draw_mesh(renderer.get_command_buffer(), mesh_handle, parameters);
  return true;
}

} // namespace

linalg::vec3 get_placement_half_extents(const entities::Entity *e)
{
  // Box-volume entities share one path: any entity that owns a Box_Volume
  // component reports its extents through it, no per-type code required.
  // Trigger_Volume is the only such entity left now that geometry has moved
  // out, so its entity_type case below is unreachable in practice.
  if (const entities::Box_Volume *volume = entities::get_box_volume(e))
    return volume->half_extents;

  switch (e->type)
  {
    case entities::entity_type::Player_Spawn_Entity:
    case entities::entity_type::Player_Entity:
      return {shared::player_half_width, shared::player_half_height,
              shared::player_half_width};
    case entities::entity_type::Particle_Emitter_Entity:
      return {0, 0, 0}; // point entity
    case entities::entity_type::Trigger_Volume_Entity:
      return static_cast<const entities::Trigger_Volume_Entity *>(e)
          ->volume.half_extents;
    case entities::entity_type::Physics_Body_Entity:
      return static_cast<const entities::Physics_Body_Entity *>(e)->size;
    case entities::entity_type::Weapon_Entity:
    case entities::entity_type::Rocket_Entity:
    case entities::entity_type::Light_Entity:
      return {editor::DEFAULT_HALF_EXTENT, editor::DEFAULT_HALF_EXTENT,
              editor::DEFAULT_HALF_EXTENT};
    case entities::entity_type::Invalid:
      break;
  }

  return {editor::DEFAULT_HALF_EXTENT, editor::DEFAULT_HALF_EXTENT,
          editor::DEFAULT_HALF_EXTENT};
}

bool draw_entity_ghost(const entities::Entity *e, overlay_renderer_t &renderer,
                       const linalg::vec3 &origin)
{
  switch (e->type)
  {
    case entities::entity_type::Player_Spawn_Entity:
      draw_player_spawn_shape(renderer, origin, colors::pink);
      return true;
    case entities::entity_type::Particle_Emitter_Entity:
      draw_particle_emitter_shape(renderer, origin, colors::gold);
      return true;
    case entities::entity_type::Trigger_Volume_Entity:
      draw_trigger_volume_shape(
          renderer, origin,
          static_cast<const entities::Trigger_Volume_Entity *>(e)
              ->volume.half_extents,
          colors::red);
      return true;
    case entities::entity_type::Light_Entity:
      draw_light_cross(renderer, origin, colors::yellow, 0.3f);
      return true;
    case entities::entity_type::Weapon_Entity:   // has render component
    case entities::entity_type::Player_Entity:   // falls back to default box
    case entities::entity_type::Rocket_Entity:   // runtime only
    case entities::entity_type::Physics_Body_Entity: // runtime only
    case entities::entity_type::Invalid:
      break;
  }

  return false;
}

// ===================================================================
// Editor drawing: render-component path + per-entity gizmo fallback
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
                           overlay_renderer_t &renderer, uint32_t, bool)
{
  // First: try the render component (common to all entity types).
  if (try_draw_render_component(e, renderer.get_command_buffer()))
    return true;

  // Second: per-type gizmo.
  switch (e->type)
  {
    case entities::entity_type::Player_Spawn_Entity:
      draw_player_spawn_shape(renderer, e->position, colors::pink);
      return true;
    case entities::entity_type::Particle_Emitter_Entity:
      draw_particle_emitter_shape(renderer, e->position, colors::gold);
      return true;
    case entities::entity_type::Trigger_Volume_Entity:
      draw_trigger_volume_shape(
          renderer, e->position,
          static_cast<const entities::Trigger_Volume_Entity *>(e)
              ->volume.half_extents,
          colors::red);
      return true;
    case entities::entity_type::Light_Entity:
      draw_light_cross(renderer, e->position, colors::yellow, 0.3f);
      return true;
    case entities::entity_type::Player_Entity:
      return draw_player_entity_mesh(
          renderer, static_cast<const entities::Player_Entity *>(e),
          colors::white, /*tinted=*/false);
    case entities::entity_type::Weapon_Entity:       // relies on render component
    case entities::entity_type::Rocket_Entity:       // runtime only
    case entities::entity_type::Physics_Body_Entity: // never appears in editor
    case entities::entity_type::Invalid:
      break;
  }

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

// Runtime dispatch for the shape-specific selection wireframe. Player_Spawn
// and Particle_Emitter deliberately decline here (their gizmo reads worse at
// selection scale than the AABB fallback does) — only Trigger_Volume, Light
// and Player_Entity draw a shape-specific outline.
static bool dispatch_selection_wireframe(const entities::Entity *e,
                                         overlay_renderer_t &renderer,
                                         color_t color, float)
{
  switch (e->type)
  {
    case entities::entity_type::Trigger_Volume_Entity:
      draw_trigger_volume_shape(
          renderer, e->position,
          static_cast<const entities::Trigger_Volume_Entity *>(e)
              ->volume.half_extents,
          color);
      return true;
    case entities::entity_type::Light_Entity:
      draw_light_cross(renderer, e->position, color, 0.4f);
      return true;
    case entities::entity_type::Player_Entity:
      return draw_player_entity_mesh(
          renderer, static_cast<const entities::Player_Entity *>(e), color,
          /*tinted=*/true);
    case entities::entity_type::Player_Spawn_Entity:
    case entities::entity_type::Particle_Emitter_Entity:
    case entities::entity_type::Weapon_Entity:
    case entities::entity_type::Rocket_Entity:
    case entities::entity_type::Physics_Body_Entity:
    case entities::entity_type::Invalid:
      break;
  }

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
  renderer::set_line_depth_bias(-200.0f, -10.0f);

  // 1. Try mesh wireframe from render component
  if (try_draw_mesh_selection_wireframe(e, renderer.get_command_buffer(), color))
  {
    renderer::set_line_depth_bias(-2.0f, -1.0f);
    return;
  }

  // 2. Try per-entity shape wireframe (wedge, AABB, trigger volume, etc.)
  if (dispatch_selection_wireframe(e, renderer, color, grid_step))
  {
    renderer::set_line_depth_bias(-2.0f, -1.0f);
    return;
  }

  // 3. Fallback: AABB bounds wireframe
  auto bounds = shared::compute_entity_bounds(e);
  renderer.draw_wire_box((bounds.min + bounds.max) * 0.5f,
                         (bounds.max - bounds.min) * 0.5f, color);

  renderer::set_line_depth_bias(-2.0f, -1.0f);
}

// ===================================================================
// Default ghost drawing (render component -> wire box fallback)
// ===================================================================

void draw_default_ghost(const entities::Entity *e, overlay_renderer_t &renderer,
                        const linalg::vec3 &origin)
{
  if (const entities::Render *rc = entities::get_render(e))
  {
    assets::asset_handle_t<assets::mesh_asset_t> mesh_handle = assets::get_mesh(rc->mesh);
    if (mesh_handle.valid())
    {
      renderer::draw_mesh(renderer.get_command_buffer(), mesh_handle,
                         {.position  = origin,
                          .color     = colors::yellow,
                          .wireframe = true});
      return;
    }
  }

  // Fallback: wire box. draw_wire_box takes a CENTER, which is the origin only
  // for centered-origin types -- a feet-origin one sits half a hull lower.
  const linalg::vec3 half_extents = get_placement_half_extents(e);
  const float lift = half_extents.y - get_placement_origin_height(e);
  renderer.draw_wire_box(origin + linalg::vec3{0, lift, 0}, half_extents,
                         colors::yellow);
}

// ===================================================================
// Convenience
// ===================================================================

float get_placement_origin_height(const entities::Entity *e)
{
  switch (e->type)
  {
    // Feet origin: the entity's position IS the surface point, no lift. Adding
    // half a hull here is what left editor-placed spawns 36 units in the air,
    // since the runtime reads a spawn's position as the player's feet.
    case entities::entity_type::Player_Spawn_Entity:
    case entities::entity_type::Player_Entity:
      return 0.f;

    // Centered origin: lift by half the height so the shape rests on the
    // surface rather than sinking half-way through it.
    case entities::entity_type::Particle_Emitter_Entity:
    case entities::entity_type::Trigger_Volume_Entity:
    case entities::entity_type::Physics_Body_Entity:
    case entities::entity_type::Weapon_Entity:
    case entities::entity_type::Rocket_Entity:
    case entities::entity_type::Light_Entity:
    case entities::entity_type::Invalid:
      break;
  }

  return get_placement_half_extents(e).y;
}

linalg::vec3 compute_placement_origin(const entities::Entity *e,
                                      const linalg::vec3 &ghost_position)
{
  linalg::vec3 origin = ghost_position;
  origin.y += get_placement_origin_height(e);
  return origin;
}

} // namespace client
