#include "../../shared/player_constants.hpp"
#include "../../shared/entities/entity_reflection.hpp"
#include "entity_editor_traits.hpp"
#include "../../shared/asset.hpp"
#include "../../shared/editor_grid.hpp"
#include "../../shared/map.hpp"
#include "../../shared/shapes.hpp"
#include "render_assets.hpp"
#include "renderer.hpp"
#include <cmath>

namespace client
{

namespace
{

// -- Shared per-type shapes (ghost + in-editor draw identically) --------

void draw_player_spawn_shape(pass_builder_t &draws,
                             const linalg::vec3 &position, color_t color)
{
  const linalg::vec3 hull{shared::player_half_width,
                          shared::player_half_height,
                          shared::player_half_width};
  draws.debug.box(position + linalg::vec3{0, shared::player_half_height, 0},
                         hull, color);

  // Marker spike, drawn from the top of the hull upward so it stays visible
  // instead of being buried inside the box.
  const float hull_top = 2.f * shared::player_half_height;
  draws.debug.line(position + linalg::vec3{0, hull_top, 0},
                     position + linalg::vec3{0, hull_top + 24.f, 0}, color);
}

void draw_particle_emitter_shape(pass_builder_t &draws,
                                 const linalg::vec3 &position, color_t color)
{
  constexpr float r = 16.f;
  draws.debug.line(position + linalg::vec3{-r, 0, 0},
                     position + linalg::vec3{r, 0, 0}, color);
  draws.debug.line(position + linalg::vec3{0, 0, -r},
                     position + linalg::vec3{0, 0, r}, color);
  draws.debug.line(position, position + linalg::vec3{0, 32, 0}, color);
}

void draw_trigger_volume_shape(pass_builder_t &draws,
                               const linalg::vec3 &position,
                               const linalg::vec3 &half_extents, color_t color)
{
  draws.debug.box(position, half_extents, color);
}

void draw_light_cross(pass_builder_t &draws, const linalg::vec3 &position,
                      color_t color, float size)
{
  draws.debug.line(position - linalg::vec3{size, 0, 0},
                     position + linalg::vec3{size, 0, 0}, color);
  draws.debug.line(position - linalg::vec3{0, size, 0},
                     position + linalg::vec3{0, size, 0}, color);
  draws.debug.line(position - linalg::vec3{0, 0, size},
                     position + linalg::vec3{0, 0, size}, color);
}

// Append a mesh draw. False means the mesh did not resolve and the caller
// should fall back to a box -- the one place the editor turns an asset handle
// into a draw, so the wireframe-support check lives here rather than at each of
// the five call sites that used to make it.
bool push_mesh(pass_builder_t &draws, assets::asset_handle_t<assets::mesh_asset_t> mesh_asset,
               const linalg::vec3f &position, const linalg::vec3f &rotation,
               const linalg::vec3f &scale, color_t tint, renderer::fill_mode_t fill)
{
  if (fill == renderer::fill_mode_t::wireframe && !renderer::wireframe_supported())
    return false;

  const renderer::mesh_handle_t mesh = get_render_mesh(mesh_asset);
  if (!mesh.valid())
    return false;

  renderer::mesh_draw_t draw{};
  draw.mesh      = mesh;
  draw.transform = linalg::compose_transform_euler(position, rotation, scale);
  draw.tint      = tint;
  draw.fill      = fill;
  draws.meshes.push_back(draw);
  return true;
}

// Player_Entity has no placeable representation of its own (runtime-spawned);
// its "gizmo" is its actual mesh drawn in wireframe, used for the in-editor
// view and the selection outline (ghost falls back to the default box).
bool draw_player_entity_mesh(pass_builder_t &draws,
                             const entities::Player_Entity *e, color_t color,
                             bool tinted)
{
  return push_mesh(draws, assets::load_mesh("resources/obj/Pyramid.obj"), e->position,
                   e->orientation, {1, 1, 1}, tinted ? color : colors::white,
                   renderer::fill_mode_t::wireframe);
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
    case entities::entity_type::Player_Spectate_Entity:
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

bool draw_entity_ghost(const entities::Entity *e, pass_builder_t &draws,
                       const linalg::vec3 &origin)
{
  switch (e->type)
  { 
    case entities::entity_type::Player_Spectate_Entity:
      draw_player_spawn_shape(draws, origin, colors::green);
      return true;
    case entities::entity_type::Player_Spawn_Entity:
      draw_player_spawn_shape(draws, origin, colors::pink);
      return true;
    case entities::entity_type::Particle_Emitter_Entity:
      draw_particle_emitter_shape(draws, origin, colors::gold);
      return true;
    case entities::entity_type::Trigger_Volume_Entity:
      draw_trigger_volume_shape(
          draws, origin,
          static_cast<const entities::Trigger_Volume_Entity *>(e)
              ->volume.half_extents,
          colors::red);
      return true;
    case entities::entity_type::Light_Entity:
      draw_light_cross(draws, origin, colors::yellow, 0.3f);
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
static bool try_draw_render_component(const entities::Entity *e, pass_builder_t &draws)
{
  const entities::Render *rc = entities::get_render(e);
  if (!rc || !rc->visible)
    return false;

  return push_mesh(draws, assets::get_mesh(rc->mesh), e->position,
                   e->orientation + rc->rotation, rc->scale, colors::white,
                   renderer::fill_mode_t::solid);
}

bool draw_entity_in_editor(const entities::Entity *e,
                           pass_builder_t &draws, uint32_t, bool)
{
  // First: try the render component (common to all entity types).
  if (try_draw_render_component(e, draws))
    return true;

  // Second: per-type gizmo.
  switch (e->type)
  {
    case entities::entity_type::Player_Spectate_Entity:
      draw_player_spawn_shape(draws, e->position, colors::green);
      return true;
    case entities::entity_type::Player_Spawn_Entity:
      draw_player_spawn_shape(draws, e->position, colors::pink);
      return true;
    case entities::entity_type::Particle_Emitter_Entity:
      draw_particle_emitter_shape(draws, e->position, colors::gold);
      return true;
    case entities::entity_type::Trigger_Volume_Entity:
      draw_trigger_volume_shape(
          draws, e->position,
          static_cast<const entities::Trigger_Volume_Entity *>(e)
              ->volume.half_extents,
          colors::red);
      return true;
    case entities::entity_type::Light_Entity:
      draw_light_cross(draws, e->position, colors::yellow, 0.3f);
      return true;
    case entities::entity_type::Player_Entity:
      return draw_player_entity_mesh(
          draws, static_cast<const entities::Player_Entity *>(e),
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
  float t = std::sin(time * 2.0f) * 0.5f + 0.5f; // 0..1

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
static bool try_draw_mesh_selection_wireframe(const entities::Entity *e, pass_builder_t &draws,
                                              color_t color)
{
  const entities::Render *rc = entities::get_render(e);
  if (!rc || !rc->visible)
    return false;

  return push_mesh(draws, assets::get_mesh(rc->mesh), e->position,
                   e->orientation + rc->rotation, rc->scale, color,
                   renderer::fill_mode_t::wireframe);
}

// Runtime dispatch for the shape-specific selection wireframe. Player_Spawn
// and Particle_Emitter deliberately decline here (their gizmo reads worse at
// selection scale than the AABB fallback does) — only Trigger_Volume, Light
// and Player_Entity draw a shape-specific outline.
static bool dispatch_selection_wireframe(const entities::Entity *e,
                                         pass_builder_t &draws,
                                         color_t color, float)
{
  switch (e->type)
  {
    case entities::entity_type::Trigger_Volume_Entity:
      draw_trigger_volume_shape(
          draws, e->position,
          static_cast<const entities::Trigger_Volume_Entity *>(e)
              ->volume.half_extents,
          color);
      return true;
    case entities::entity_type::Light_Entity:
      draw_light_cross(draws, e->position, color, 0.4f);
      return true;
    case entities::entity_type::Player_Entity:
      return draw_player_entity_mesh(
          draws, static_cast<const entities::Player_Entity *>(e), color,
          /*tinted=*/true);
    case entities::entity_type::Player_Spawn_Entity:
    case entities::entity_type::Player_Spectate_Entity:
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
                              pass_builder_t &draws, float time,
                              float grid_step)
{
  color_t color = compute_selection_pulse_color(time);

  // A very strong bias so the outline renders in FRONT of the surface it
  // traces. It rides the lines that need it instead of being set and restored
  // around three early-return paths -- each of which had to remember the
  // restore, and one getting it wrong was invisible.
  constexpr float highlight_bias = -200.0f;

  // 1. Try mesh wireframe from render component
  if (try_draw_mesh_selection_wireframe(e, draws, color))
    return;

  // 2. Try per-entity shape wireframe (wedge, AABB, trigger volume, etc.)
  if (dispatch_selection_wireframe(e, draws, color, grid_step))
    return;

  // 3. Fallback: AABB bounds wireframe
  auto bounds = shared::compute_entity_bounds(e);
  draws.debug.box((bounds.min + bounds.max) * 0.5f, (bounds.max - bounds.min) * 0.5f, color,
                  renderer::fill_mode_t::wireframe, highlight_bias);
}

// ===================================================================
// Default ghost drawing (render component -> wire box fallback)
// ===================================================================

void draw_default_ghost(const entities::Entity *e, pass_builder_t &draws,
                        const linalg::vec3 &origin)
{
  if (const entities::Render *rc = entities::get_render(e))
  {
    if (push_mesh(draws, assets::get_mesh(rc->mesh), origin, {0, 0, 0}, {1, 1, 1},
                  colors::yellow, renderer::fill_mode_t::wireframe))
      return;
  }

  // Fallback: wire box. debug.box takes a CENTER, which is the origin only for
  // centered-origin types -- a feet-origin one sits half a hull lower.
  const linalg::vec3 half_extents = get_placement_half_extents(e);
  const float lift = half_extents.y - get_placement_origin_height(e);
  draws.debug.box(origin + linalg::vec3{0, lift, 0}, half_extents,
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
    case entities::entity_type::Player_Spectate_Entity:
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
