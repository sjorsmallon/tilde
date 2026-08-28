#include "../../shared/player_constants.hpp"
#include "../../shared/entities/entity_reflection.hpp"
#include "entity_editor_traits.hpp"
#include "../../shared/asset.hpp"
#include "../../shared/editor_grid.hpp"
#include "../../shared/map.hpp"
#include "../../shared/shapes.hpp"
#include "render_assets.hpp"
#include "renderer.hpp"
#include <algorithm>
#include <cmath>

namespace client
{

namespace
{

// -- Shared per-type shapes (ghost + in-editor draw identically) --------

// FACING IS DATA ON BOTH SPAWN TYPES, not decoration. place_player_at_spawn
// copies a marker's orientation.y into view_angle_yaw AND body_yaw and its
// orientation.x into view_angle_pitch; try_pose_camera_at_spectate_spot copies
// the same two straight into camera.yaw / camera.pitch. Both are rotatable with
// the selection gizmo, so an axis-aligned box hid the one field being edited.
//
// Entity::orientation is the MODEL euler the rotation gizmo writes, not a
// yaw/pitch pair -- so every facing here comes off forward_from_model_euler,
// which is the +X column of that same matrix. Reading .y as a yaw instead is
// what made the frustum counter-rotate against the ring dragging it.
constexpr float SPAWN_SIGHTLINE_LENGTH   = 56.f;
constexpr float SPAWN_WEDGE_LENGTH       = 48.f;
constexpr float SPAWN_WEDGE_HALF_WIDTH   = 14.f;
constexpr float SPAWN_WEDGE_GROUND_LIFT  = 1.f;

void draw_player_spawn_shape(pass_builder_t &draws, const linalg::vec3 &position,
                             const linalg::vec3 &orientation, color_t color)
{
  // The hull stays: it is the volume that has to be clear of geometry, and a
  // spawn buried in a wall is a bug you want to see from across the map.
  const linalg::vec3 hull{shared::player_half_width,
                          shared::player_half_height,
                          shared::player_half_width};
  draws.debug.box(position + linalg::vec3{0, shared::player_half_height, 0},
                         hull, color);

  // The sightline the spawned player will actually look down -- from the EYE,
  // along yaw AND pitch. This is what replaced the vertical marker spike, which
  // carried no information at any scale. Pitch is almost always 0 on a spawn,
  // so this earns its ink on the case where it is not: staring into a wall or
  // at the sky was otherwise indistinguishable from facing down a corridor.
  const linalg::vec3 eye = position + linalg::vec3{0, shared::player_eye_height, 0};
  const linalg::basis_t basis = linalg::basis_from_model_euler(orientation);
  draws.debug.arrow(eye, eye + basis.forward * SPAWN_SIGHTLINE_LENGTH, color);

  // Yaw again, flat on the ground. Redundant with the arrow when pitch is 0 and
  // deliberately so: this is the one that reads from directly overhead, which is
  // how spawns actually get laid out. Filled rather than outlined, because a
  // solid triangle still says which end is the nose when it is nearly edge-on.
  // Flattened out of the same forward rather than re-derived from a yaw, so the
  // two markers cannot disagree about which way this thing faces.
  linalg::vec3 ground_forward{basis.forward.x, 0.f, basis.forward.z};
  ground_forward = (linalg::length(ground_forward) < 0.001f)
                       ? linalg::vec3{1.f, 0.f, 0.f}
                       : linalg::normalize(ground_forward);
  const linalg::vec3 ground_right{-ground_forward.z, 0.f, ground_forward.x};
  const linalg::vec3 base = position + linalg::vec3{0, SPAWN_WEDGE_GROUND_LIFT, 0};

  const linalg::vec3 wedge[3] = {
      base + ground_forward * SPAWN_WEDGE_LENGTH,
      base - ground_forward * (SPAWN_WEDGE_LENGTH * 0.35f) +
          ground_right * SPAWN_WEDGE_HALF_WIDTH,
      base - ground_forward * (SPAWN_WEDGE_LENGTH * 0.35f) -
          ground_right * SPAWN_WEDGE_HALF_WIDTH,
  };
  draws.debug.filled_polygon(wedge, with_alpha(color, 150));
}

// A spectate spot is a CAMERA, not a body -- play_state reads its position as
// camera.position with no eye offset and nothing ever stands there. Drawing it
// as a player hull claimed the opposite, and left the two spawn types telling
// each other apart by colour alone.
//
// So: the camera gizmo every DCC draws. Unmistakable next to a spawn box at any
// distance, and it shows the view VOLUME rather than a point.
void draw_spectate_camera_shape(pass_builder_t &draws, const linalg::vec3 &position,
                                const linalg::vec3 &orientation, color_t color)
{
  // shapes.hpp owns the frustum, because compute_entity_bounds picks with the
  // same one -- a shape authored here would be a second description of it.
  const shared::spectate_frustum_t frustum =
      shared::make_spectate_frustum(position, orientation);

  for (uint32_t i = 0; i < 4; ++i)
  {
    draws.debug.line(frustum.apex, frustum.far_corners[i], color);
    draws.debug.line(frustum.far_corners[i], frustum.far_corners[(i + 1) % 4], color);
  }

  // The up tick over the top edge. There is no roll to read, but a frustum
  // pitched steeply enough is otherwise the same picture upside down.
  draws.debug.line(frustum.far_corners[0], frustum.up_tick, color);
  draws.debug.line(frustum.far_corners[1], frustum.up_tick, color);
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

// -- Lights ------------------------------------------------------------
//
// Three types, three helpers, and that is the whole reason Light_Entity was
// split: the shape a light throws is the thing an author is placing, and one
// yellow cross showed none of it. Each of these draws the marker (so all three
// read as "a light" at a glance) plus the volume that kind actually affects.
//
// The volume is drawn DIMMED, because it is a diagram of reach rather than an
// object with a surface -- at range 512 an undimmed sphere buries the level it
// is lighting.
constexpr float LIGHT_MARKER_SIZE       = 15.f;
constexpr float DIRECTIONAL_RAY_LENGTH  = 128.f;
constexpr float DIRECTIONAL_RAY_SPACING = 24.f;
constexpr uint8_t LIGHT_VOLUME_ALPHA    = 110;

void draw_light_marker(pass_builder_t &draws, const linalg::vec3 &position,
                       color_t color)
{
  draws.debug.line(position - linalg::vec3{LIGHT_MARKER_SIZE, 0, 0},
                     position + linalg::vec3{LIGHT_MARKER_SIZE, 0, 0}, color);
  draws.debug.line(position - linalg::vec3{0, LIGHT_MARKER_SIZE, 0},
                     position + linalg::vec3{0, LIGHT_MARKER_SIZE, 0}, color);
  draws.debug.line(position - linalg::vec3{0, 0, LIGHT_MARKER_SIZE},
                     position + linalg::vec3{0, 0, LIGHT_MARKER_SIZE}, color);
}

// All three take the concrete entity and a caller-chosen position, because the
// ghost draws at the placement origin while the other two draw at the entity's
// own -- the same split draw_player_entity_mesh already makes.
void draw_point_light_shape(pass_builder_t &draws, const entities::Point_Light_Entity *light,
                            const linalg::vec3 &position, color_t color)
{
  draw_light_marker(draws, position, color);
  if (light->range > 0.f)
    draws.debug.wire_sphere(position, light->range, with_alpha(color, LIGHT_VOLUME_ALPHA));
}

void draw_spot_light_shape(pass_builder_t &draws, const entities::Spot_Light_Entity *light,
                           const linalg::vec3 &position, color_t color)
{
  draw_light_marker(draws, position, color);
  if (light->range <= 0.f)
    return;

  const linalg::basis_t basis = linalg::basis_from_model_euler(light->orientation);
  const linalg::vec3 cone_end = position + basis.forward * light->range;
  const color_t      dim      = with_alpha(color, LIGHT_VOLUME_ALPHA);

  // tan, not sin: the circles cap a cone of LENGTH `range` along the axis, which
  // is the quantity the falloff is expressed in. Clamped to (0, 89) because tan
  // runs away at a right angle -- a hand-typed 120 would draw a cone past the
  // far wall, and a negative one would mirror it behind the light.
  const auto cone_radius = [range = light->range](float half_angle_degrees) {
    return range * std::tan(linalg::to_radians(std::clamp(half_angle_degrees, 0.f, 89.f)));
  };
  const float outer_radius = cone_radius(light->outer_degrees);
  const float inner_radius = cone_radius(light->inner_degrees);

  draws.debug.wire_circle(cone_end, outer_radius, basis.forward, dim);
  draws.debug.wire_circle(cone_end, inner_radius, basis.forward, dim);

  // Four edges rather than a full skirt: enough to read the cone as a solid at
  // any angle, few enough that two overlapping spots are still separable.
  for (int quadrant = 0; quadrant < 4; ++quadrant)
  {
    const float        angle  = linalg::to_radians(90.f * (float)quadrant);
    const linalg::vec3 offset = basis.right * (std::cos(angle) * outer_radius) +
                                basis.up * (std::sin(angle) * outer_radius);
    draws.debug.line(position, cone_end + offset, dim);
  }

  draws.debug.line(position, cone_end, dim);
}

// No falloff volume to draw -- a directional light has no position that shading
// reads. So the gizmo says the one thing that IS true of it: parallel rays, all
// the same length, pointing the way the rotate gizmo put them.
void draw_directional_light_shape(pass_builder_t &draws,
                                  const entities::Directional_Light_Entity *light,
                                  const linalg::vec3 &position, color_t color)
{
  draw_light_marker(draws, position, color);

  const linalg::basis_t basis = linalg::basis_from_model_euler(light->orientation);
  const color_t      dim   = with_alpha(color, LIGHT_VOLUME_ALPHA);

  for (int x = -1; x <= 1; ++x)
  {
    for (int y = -1; y <= 1; ++y)
    {
      const linalg::vec3 start = position +
                                 basis.right * ((float)x * DIRECTIONAL_RAY_SPACING) +
                                 basis.up * ((float)y * DIRECTIONAL_RAY_SPACING);
      draws.debug.arrow(start, start + basis.forward * DIRECTIONAL_RAY_LENGTH,
                        (x == 0 && y == 0) ? color : dim);
    }
  }
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
    // The three lights included on purpose: a light PICKS as a point-sized box
    // whatever its reach. Sizing the pick volume to a 512-unit falloff sphere
    // would make one light swallow every click in the room it lights.
    case entities::entity_type::Weapon_Entity:
    case entities::entity_type::Rocket_Entity:
    case entities::entity_type::Point_Light_Entity:
    case entities::entity_type::Spot_Light_Entity:
    case entities::entity_type::Directional_Light_Entity:
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
      draw_spectate_camera_shape(draws, origin, e->orientation, colors::green);
      return true;
    case entities::entity_type::Player_Spawn_Entity:
      draw_player_spawn_shape(draws, origin, e->orientation, colors::pink);
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
    case entities::entity_type::Point_Light_Entity:
      draw_point_light_shape(
          draws, static_cast<const entities::Point_Light_Entity *>(e), origin, colors::yellow);
      return true;
    case entities::entity_type::Spot_Light_Entity:
      draw_spot_light_shape(
          draws, static_cast<const entities::Spot_Light_Entity *>(e), origin, colors::yellow);
      return true;
    case entities::entity_type::Directional_Light_Entity:
      draw_directional_light_shape(
          draws, static_cast<const entities::Directional_Light_Entity *>(e), origin, colors::yellow);
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
      draw_spectate_camera_shape(draws, e->position, e->orientation, colors::green);
      return true;
    case entities::entity_type::Player_Spawn_Entity:
      draw_player_spawn_shape(draws, e->position, e->orientation, colors::pink);
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
    case entities::entity_type::Point_Light_Entity:
      draw_point_light_shape(
          draws, static_cast<const entities::Point_Light_Entity *>(e), e->position, colors::yellow);
      return true;
    case entities::entity_type::Spot_Light_Entity:
      draw_spot_light_shape(
          draws, static_cast<const entities::Spot_Light_Entity *>(e), e->position, colors::yellow);
      return true;
    case entities::entity_type::Directional_Light_Entity:
      draw_directional_light_shape(
          draws, static_cast<const entities::Directional_Light_Entity *>(e), e->position, colors::yellow);
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

// Runtime dispatch for the shape-specific selection wireframe. Particle_Emitter
// deliberately declines here (its gizmo reads worse at selection scale than the
// AABB fallback does); everything else draws its own shape.
//
// The two spawn types used to decline too, which was fair while their gizmo was
// a vertical spike carrying no information. It stopped being fair once the gizmo
// IS the facing: the AABB fallback dropped the orientation at exactly the moment
// you had selected the thing in order to rotate it.
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
    case entities::entity_type::Point_Light_Entity:
      draw_point_light_shape(
          draws, static_cast<const entities::Point_Light_Entity *>(e), e->position, color);
      return true;
    case entities::entity_type::Spot_Light_Entity:
      draw_spot_light_shape(
          draws, static_cast<const entities::Spot_Light_Entity *>(e), e->position, color);
      return true;
    case entities::entity_type::Directional_Light_Entity:
      draw_directional_light_shape(
          draws, static_cast<const entities::Directional_Light_Entity *>(e), e->position, color);
      return true;
    case entities::entity_type::Player_Entity:
      return draw_player_entity_mesh(
          draws, static_cast<const entities::Player_Entity *>(e), color,
          /*tinted=*/true);
    case entities::entity_type::Player_Spawn_Entity:
      draw_player_spawn_shape(draws, e->position, e->orientation, color);
      return true;
    case entities::entity_type::Player_Spectate_Entity:
      draw_spectate_camera_shape(draws, e->position, e->orientation, color);
      return true;
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
    case entities::entity_type::Point_Light_Entity:
    case entities::entity_type::Spot_Light_Entity:
    case entities::entity_type::Directional_Light_Entity:
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
