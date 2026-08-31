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

constexpr float SPAWN_SIGHTLINE_LENGTH   = 56.f;
constexpr float SPAWN_WEDGE_LENGTH       = 48.f;
constexpr float SPAWN_WEDGE_HALF_WIDTH   = 14.f;
constexpr float SPAWN_WEDGE_GROUND_LIFT  = 1.f;

void draw_player_spawn_shape(pass_builder_t& draws, const linalg::vec3& position,
                             const linalg::quatf& orientation, color_t color)
{
  const linalg::vec3 hull{shared::player_half_width,
                          shared::player_half_height,
                          shared::player_half_width};
  draws.debug.box(position + linalg::vec3{0, shared::player_half_height, 0},
                         hull, color);

                         const linalg::vec3 eye = position + linalg::vec3{0, shared::player_eye_height, 0};
  const linalg::basis_t basis = linalg::basis_from(orientation);
  draws.debug.arrow(eye, eye + basis.forward * SPAWN_SIGHTLINE_LENGTH, color);

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

void draw_spectate_camera_shape(pass_builder_t& draws, const linalg::vec3& position,
                                const linalg::quatf& orientation, color_t color)
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

void draw_particle_emitter_shape(pass_builder_t& draws,
                                 const linalg::vec3& position, color_t color)
{
  constexpr float r = 16.f;
  draws.debug.line(position + linalg::vec3{-r, 0, 0},
                     position + linalg::vec3{r, 0, 0}, color);
  draws.debug.line(position + linalg::vec3{0, 0, -r},
                     position + linalg::vec3{0, 0, r}, color);
  draws.debug.line(position, position + linalg::vec3{0, 32, 0}, color);
}

void draw_trigger_volume_shape(pass_builder_t& draws,
                               const linalg::vec3& position,
                               const linalg::vec3& half_extents, color_t color)
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

void draw_light_marker(pass_builder_t& draws, const linalg::vec3& position,
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
// own -- the same split the player mesh gizmo already makes.
void draw_point_light_shape(pass_builder_t& draws, const entities::Point_Light_Entity* light,
                            const linalg::vec3& position, color_t color)
{
  draw_light_marker(draws, position, color);
  if (light->range > 0.f)
    draws.debug.wire_sphere(position, light->range, with_alpha(color, LIGHT_VOLUME_ALPHA));
}

void draw_spot_light_shape(pass_builder_t& draws, const entities::Spot_Light_Entity* light,
                           const linalg::vec3& position, color_t color)
{
  draw_light_marker(draws, position, color);
  if (light->range <= 0.f)
    return;

  const linalg::basis_t basis = linalg::basis_from(light->orientation);
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
void draw_directional_light_shape(pass_builder_t& draws,
                                  const entities::Directional_Light_Entity* light,
                                  const linalg::vec3& position, color_t color)
{
  draw_light_marker(draws, position, color);

  const linalg::basis_t basis = linalg::basis_from(light->orientation);
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
//
// `material` is the entity's own, or null where the caller is drawing a GIZMO
// rather than the entity's appearance (a selection pulse, a ghost) -- there the
// tint IS the meaning and a base colour under it would only muddy it.
bool push_mesh(pass_builder_t& draws, assets::asset_handle_t<assets::mesh_asset_t> mesh_asset,
               const linalg::vec3f& position, const linalg::quatf& rotation,
               const linalg::vec3f& scale, color_t tint, renderer::fill_mode_t fill,
               const entities::Material* material = nullptr)
{
  if (fill == renderer::fill_mode_t::wireframe && !renderer::wireframe_supported())
    return false;

  const renderer::mesh_handle_t mesh = get_render_mesh(mesh_asset);
  if (!mesh.valid())
    return false;

  renderer::mesh_draw_t draw{};
  draw.mesh      = mesh;
  draw.transform = linalg::compose_transform(position, rotation, scale);
  draw.tint      = tint;
  draw.fill      = fill;
  if (material && fill == renderer::fill_mode_t::solid)
    draw.material_overrides = material_variant(mesh, state_for(*material));
  draws.meshes.push_back(draw);
  return true;
}

using draw_shape_function_t = bool (*)(const entities::Entity*, pass_builder_t&,
                                 const linalg::vec3&, color_t);

// Where `position` sits when placed on a surface: centered lifts by half the
// height so the shape rests on it; feet means position IS the surface point.
// Adding half a hull to a feet-origin type is what left editor-placed spawns
// 36 units in the air, since the runtime reads a spawn's position as the feet.
enum class placement_origin_t
{
  centered,
  feet,
};

struct entity_editor_traits_t
{
  // Pick + placement volume. NOT necessarily the drawn shape: a light picks
  // point-sized whatever its reach, or a 512-unit falloff sphere would swallow
  // every click in the room it lights.
  linalg::vec3       half_extents        = {};
  placement_origin_t origin              = placement_origin_t::centered;
  color_t            color               = colors::white; // gizmo colour (ghost + in-editor)
  draw_shape_function_t draw_shape       = nullptr;       // null: contexts use their defaults
  bool shape_for_ghost     = true;
  bool shape_for_selection = true;
};

// -- Gizmo adapters: the shapes above under the uniform signature -------

bool player_spawn_gizmo(const entities::Entity* e, pass_builder_t& draws,
                        const linalg::vec3& position, color_t color)
{
  draw_player_spawn_shape(draws, position, e->orientation, color);
  return true;
}

bool spectate_camera_gizmo(const entities::Entity* e, pass_builder_t& draws,
                           const linalg::vec3& position, color_t color)
{
  draw_spectate_camera_shape(draws, position, e->orientation, color);
  return true;
}

bool particle_emitter_gizmo(const entities::Entity*, pass_builder_t& draws,
                            const linalg::vec3& position, color_t color)
{
  draw_particle_emitter_shape(draws, position, color);
  return true;
}

bool trigger_volume_gizmo(const entities::Entity* e, pass_builder_t& draws,
                          const linalg::vec3& position, color_t color)
{
  draw_trigger_volume_shape(
      draws, position,
      static_cast<const entities::Trigger_Volume_Entity*>(e)->volume.half_extents,
      color);
  return true;
}

bool point_light_gizmo(const entities::Entity* e, pass_builder_t& draws,
                       const linalg::vec3& position, color_t color)
{
  draw_point_light_shape(
      draws, static_cast<const entities::Point_Light_Entity*>(e), position, color);
  return true;
}

bool spot_light_gizmo(const entities::Entity* e, pass_builder_t& draws,
                      const linalg::vec3& position, color_t color)
{
  draw_spot_light_shape(
      draws, static_cast<const entities::Spot_Light_Entity*>(e), position, color);
  return true;
}

bool directional_light_gizmo(const entities::Entity* e, pass_builder_t& draws,
                             const linalg::vec3& position, color_t color)
{
  draw_directional_light_shape(
      draws, static_cast<const entities::Directional_Light_Entity*>(e), position, color);
  return true;
}

// Player_Entity has no placeable representation of its own (runtime-spawned);
// its "gizmo" is its actual mesh drawn in wireframe.
bool player_mesh_gizmo(const entities::Entity* e, pass_builder_t& draws,
                       const linalg::vec3& position, color_t color)
{
  return push_mesh(draws, assets::load_mesh("resources/obj/Pyramid.obj"), position,
                   e->orientation, {1, 1, 1}, color,
                   renderer::fill_mode_t::wireframe);
}

entity_editor_traits_t default_entity_traits(const entities::Entity* e)
{
  entity_editor_traits_t traits{};
  if (const entities::Box_Volume* volume = entities::get_box_volume(e))
  {
    traits.half_extents = volume->half_extents;
  }
  else
  {
    const shared::aabb_bounds_t bounds = shared::compute_entity_bounds(e);
    traits.half_extents = (bounds.max - bounds.min) * 0.5f;
    traits.half_extents.x = std::max(traits.half_extents.x, editor::DEFAULT_HALF_EXTENT);
    traits.half_extents.y = std::max(traits.half_extents.y, editor::DEFAULT_HALF_EXTENT);
    traits.half_extents.z = std::max(traits.half_extents.z, editor::DEFAULT_HALF_EXTENT);
  }
  return traits;
}

entity_editor_traits_t editor_traits_for(const entities::Entity* e)
{
  const linalg::vec3 player_hull{shared::player_half_width,
                                 shared::player_half_height,
                                 shared::player_half_width};
  const linalg::vec3 point_pick{editor::DEFAULT_HALF_EXTENT,
                                editor::DEFAULT_HALF_EXTENT,
                                editor::DEFAULT_HALF_EXTENT};

  switch (e->type)
  {
    case entities::entity_type::Player_Spawn_Entity:
      return {.half_extents = player_hull,
              .origin       = placement_origin_t::feet,
              .color        = colors::pink,
              .draw_shape   = &player_spawn_gizmo};

    case entities::entity_type::Player_Spectate_Entity:
      return {.half_extents = player_hull,
              .origin       = placement_origin_t::feet,
              .color        = colors::green,
              .draw_shape   = &spectate_camera_gizmo};

    // Ghost declines: runtime-spawned, so the default box is the honest
    // placement preview; the mesh wireframe serves in-editor and selection.
    case entities::entity_type::Player_Entity:
      return {.half_extents    = player_hull,
              .origin          = placement_origin_t::feet,
              .draw_shape      = &player_mesh_gizmo,
              .shape_for_ghost = false};

    // Point entity. Selection declines on purpose: the gizmo reads worse at
    // selection scale than the AABB fallback does.
    case entities::entity_type::Particle_Emitter_Entity:
      return {.half_extents        = {0, 0, 0},
              .color               = colors::gold,
              .draw_shape          = &particle_emitter_gizmo,
              .shape_for_selection = false};

    case entities::entity_type::Trigger_Volume_Entity:
      return {.half_extents = static_cast<const entities::Trigger_Volume_Entity*>(e)
                                  ->volume.half_extents,
              .color        = colors::red,
              .draw_shape   = &trigger_volume_gizmo};

    case entities::entity_type::Physics_Body_Entity:
      return {.half_extents =
                  static_cast<const entities::Physics_Body_Entity*>(e)->size};

    // Sized by the volume you SHOOT rather than the mesh you see: the hitbox
    // is what an author is placing. No gizmo on purpose: the render component
    // draws the art, and the AABB fallback traces the hitbox around it --
    // exactly the pair an author wants when the two disagree.
    case entities::entity_type::Damageable_Entity:
      return {.half_extents = static_cast<const entities::Damageable_Entity*>(e)
                                  ->hitbox_half_extents};

    case entities::entity_type::Weapon_Entity: // render component draws it
    case entities::entity_type::Rocket_Entity: // runtime only
      return {.half_extents = point_pick};

    // Lights pick as a point-sized box whatever their reach -- sizing the pick
    // volume to a 512-unit falloff sphere would make one light swallow every
    // click in the room it lights.
    case entities::entity_type::Point_Light_Entity:
      return {.half_extents = point_pick,
              .color        = colors::yellow,
              .draw_shape   = &point_light_gizmo};

    case entities::entity_type::Spot_Light_Entity:
      return {.half_extents = point_pick,
              .color        = colors::yellow,
              .draw_shape   = &spot_light_gizmo};

    case entities::entity_type::Directional_Light_Entity:
      return {.half_extents = point_pick,
              .color        = colors::yellow,
              .draw_shape   = &directional_light_gizmo};

    case entities::entity_type::Invalid:
      break;
  }

  return default_entity_traits(e);
}

} // namespace

// ===================================================================
// The drivers: each context's fallback ladder, written once
// ===================================================================

linalg::vec3 get_placement_half_extents(const entities::Entity* e)
{
  return editor_traits_for(e).half_extents;
}

float get_placement_origin_height(const entities::Entity* e)
{
  const entity_editor_traits_t traits = editor_traits_for(e);
  return traits.origin == placement_origin_t::feet ? 0.f : traits.half_extents.y;
}

bool draw_entity_ghost(const entities::Entity* e, pass_builder_t& draws,
                       const linalg::vec3& origin)
{
  const entity_editor_traits_t traits = editor_traits_for(e);
  if (!traits.draw_shape || !traits.shape_for_ghost)
    return false;
  return traits.draw_shape(e, draws, origin, traits.color);
}

static bool try_draw_render_component(const entities::Entity* e, pass_builder_t& draws)
{
  const entities::Render* rc = entities::get_render(e);
  if (!rc || !rc->visible)
    return false;

  if (rc->is_wireframe)
    return push_mesh(draws, assets::get_mesh(rc->mesh), e->position,
                     linalg::compose_model_rotation(e->orientation, rc->rotation), rc->scale, colors::white,
                     renderer::fill_mode_t::wireframe);

  return push_mesh(draws, assets::get_mesh(rc->mesh), e->position,
                   linalg::compose_model_rotation(e->orientation, rc->rotation), rc->scale,
                   color_from_vec3(rc->material.color), renderer::fill_mode_t::solid,
                   &rc->material);
}

bool draw_entity_in_editor(const entities::Entity* e,
                           pass_builder_t& draws, uint32_t, bool)
{
  // First: try the render component (common to all entity types).
  if (try_draw_render_component(e, draws))
    return true;

  // Second: per-type gizmo.
  const entity_editor_traits_t traits = editor_traits_for(e);
  if (traits.draw_shape)
    return traits.draw_shape(e, draws, e->position, traits.color);
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
static bool try_draw_mesh_selection_wireframe(const entities::Entity* e, pass_builder_t& draws,
                                              color_t color)
{
  const entities::Render* rc = entities::get_render(e);
  if (!rc || !rc->visible)
    return false;

  return push_mesh(draws, assets::get_mesh(rc->mesh), e->position,
                   linalg::compose_model_rotation(e->orientation, rc->rotation), rc->scale, color,
                   renderer::fill_mode_t::wireframe);
}

void draw_selection_highlight(const entities::Entity* e,
                              pass_builder_t& draws, float time,
                              float)
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

  // 2. Per-type gizmo in the pulse colour. The two spawn types used to decline
  // here too, which was fair while their gizmo was a vertical spike carrying no
  // information. It stopped being fair once the gizmo IS the facing: the AABB
  // fallback dropped the orientation at exactly the moment you had selected the
  // thing in order to rotate it.
  const entity_editor_traits_t traits = editor_traits_for(e);
  if (traits.draw_shape && traits.shape_for_selection &&
      traits.draw_shape(e, draws, e->position, color))
    return;

  // 3. Fallback: AABB bounds wireframe
  const shared::aabb_bounds_t bounds = shared::compute_entity_bounds(e);
  draws.debug.box((bounds.min + bounds.max) * 0.5f, (bounds.max - bounds.min) * 0.5f, color,
                  renderer::fill_mode_t::wireframe, highlight_bias);
}

// ===================================================================
// Default ghost drawing (render component -> wire box fallback)
// ===================================================================

void draw_default_ghost(const entities::Entity* e, pass_builder_t& draws,
                        const linalg::vec3& origin)
{
  if (const entities::Render* rc = entities::get_render(e))
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

linalg::vec3 compute_placement_origin(const entities::Entity* e,
                                      const linalg::vec3& ghost_position)
{
  linalg::vec3 origin = ghost_position;
  origin.y += get_placement_origin_height(e);
  return origin;
}

} // namespace client
