#include "../../shared/player_constants.hpp"
#include "../../shared/entities/entity_reflection.hpp"
#include "entity_editor_traits.hpp"
#include "../../shared/asset.hpp"
#include "../../shared/editor_grid.hpp"
#include "../../shared/map.hpp"
#include "../../shared/shapes.hpp"
#include "render_assets.hpp"
#include "renderer.hpp"
#include "state_manager.hpp"
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

constexpr float JUMP_PAD_ARROW_SECONDS = 0.1f;
constexpr float JUMP_PAD_ARC_SECONDS   = 3.f;
constexpr int   JUMP_PAD_ARC_SEGMENTS  = 48;

// The launch as an arrow whose length is the speed over a tenth of a second: a
// pad is aimed with the rotate gizmo, so the arrow is the thing being edited.
void draw_jump_pad_shape(pass_builder_t& draws, const entities::Jump_Pad_Entity* pad,
                         const linalg::vec3& position, color_t color)
{
  draws.debug.box(position + pad->volume.position, pad->volume.half_extents, color);
  const linalg::vec3 launch = linalg::forward(pad->orientation) * pad->launch_speed;
  draws.debug.arrow(position, position + launch * JUMP_PAD_ARROW_SECONDS, color);
}

// Where a launched player lands: the ballistic arc under the live g_gravity,
// cut where it drops below the pad.
void draw_jump_pad_arc(pass_builder_t& draws, const entities::Jump_Pad_Entity* pad,
                       const linalg::vec3& position, color_t color)
{
  const float        gravity  = state_manager::get_client_context().cvars->g_gravity;
  const linalg::vec3 velocity = linalg::forward(pad->orientation) * pad->launch_speed;
  const float        floor_y  = position.y - pad->volume.half_extents.y;

  linalg::vec3 previous = position;
  for (int i = 1; i <= JUMP_PAD_ARC_SEGMENTS; ++i)
  {
    const float t = JUMP_PAD_ARC_SECONDS * static_cast<float>(i) / JUMP_PAD_ARC_SEGMENTS;
    const linalg::vec3 point =
        position + velocity * t - linalg::vec3{0.f, 0.5f * gravity * t * t, 0.f};
    draws.debug.line(previous, point, color);
    previous = point;
    if (point.y < floor_y)
      break;
  }
}

constexpr float LIGHT_DIRECTION_STUB    = 30.f;
constexpr float DIRECTIONAL_RAY_LENGTH  = 128.f;
constexpr float DIRECTIONAL_RAY_SPACING = 24.f;
constexpr uint8_t LIGHT_VOLUME_ALPHA    = 110;

// The EMITTER, drawn at full alpha inside the dimmed falloff volume: it is the
// thing casting the penumbra, and it is usually small enough beside `range` that a
// dimmed one would not be visible at all. Nothing to draw at zero, which is a
// punctual light and has no size to show.
void draw_source_sphere(pass_builder_t& draws, const entities::Light& light,
                        const linalg::vec3& position, color_t color)
{
  if (light.source_radius > 0.f)
    draws.debug.wire_sphere(position, light.source_radius, color);
}


void draw_light_direction_stub(pass_builder_t& draws, const linalg::quatf& orientation,
                               const linalg::vec3& position, color_t color)
{
  const linalg::basis_t basis = linalg::basis_from(orientation);
  draws.debug.arrow(position, position + basis.forward * LIGHT_DIRECTION_STUB, color);
}


void draw_point_light_shape(pass_builder_t& draws, const entities::Point_Light_Entity* light,
                            const linalg::vec3& position, color_t color)
{
  draw_source_sphere(draws, light->light, position, color);
}

void draw_point_light_reach(pass_builder_t& draws, const entities::Point_Light_Entity* light,
                            const linalg::vec3& position, color_t color)
{
  if (light->range > 0.f)
    draws.debug.wire_sphere(position, light->range, with_alpha(color, LIGHT_VOLUME_ALPHA));
}

void draw_spot_light_shape(pass_builder_t& draws, const entities::Spot_Light_Entity* light,
                           const linalg::vec3& position, color_t color)
{
  draw_source_sphere(draws, light->light, position, color);
  draw_light_direction_stub(draws, light->orientation, position, color);
}

void draw_spot_light_reach(pass_builder_t& draws, const entities::Spot_Light_Entity* light,
                           const linalg::vec3& position, color_t color)
{
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
// the same length, pointing the way the rotate gizmo put them. The middle ray
// is always on, since direction is all a directional light HAS.
void draw_directional_light_shape(pass_builder_t& draws,
                                  const entities::Directional_Light_Entity* light,
                                  const linalg::vec3& position, color_t color)
{
  draw_light_direction_stub(draws, light->orientation, position, color);
}

void draw_directional_light_reach(pass_builder_t& draws,
                                  const entities::Directional_Light_Entity* light,
                                  const linalg::vec3& position, color_t color)
{
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

using draw_function_t = void (*)(const entities::Entity*, pass_builder_t&,
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
  color_t            color               = colors::white; // stand-in and diagram colour
  // ART for a type with no mesh: the rung of the art ladder between the render
  // component and the wire box. Drawn INSTEAD of the mesh, never beside it.
  draw_function_t    draw_stand_in       = nullptr;
  // What the thing DOES -- a volume, a launch, an emitter. Drawn on top of
  // whatever drew the art, in every context, never instead of it. A type that
  // one day gets a mesh keeps its diagram; that is the whole point of the split.
  draw_function_t    draw_diagram        = nullptr;
  // The diagram that is bigger than the object -- a light's falloff -- drawn
  // ONLY for the selected entity and the one being placed. Always-on would be
  // the same as never, because every one of them overlaps every other.
  draw_function_t    draw_reach          = nullptr;
  // The screen-space icon, drawn at a constant pixel size by the icon pass.
  // Absent for every type whose own shape is what you need to see.
  std::optional<assets::texture_asset> icon;
};

// -- Adapters: the shapes above under the uniform signature --------------
//
// Two kinds, and the traits table says which each is. A STAND-IN is art for a
// type that has no mesh; a DIAGRAM is what the thing does, drawn on top of the
// art in every context.

void player_spawn_stand_in(const entities::Entity* e, pass_builder_t& draws,
                           const linalg::vec3& position, color_t color)
{
  draw_player_spawn_shape(draws, position, e->orientation, color);
}

void spectate_camera_stand_in(const entities::Entity* e, pass_builder_t& draws,
                              const linalg::vec3& position, color_t color)
{
  draw_spectate_camera_shape(draws, position, e->orientation, color);
}

void particle_emitter_stand_in(const entities::Entity*, pass_builder_t& draws,
                               const linalg::vec3& position, color_t color)
{
  draw_particle_emitter_shape(draws, position, color);
}

// Player_Entity has no placeable representation of its own (runtime-spawned);
// its stand-in is its actual mesh drawn in wireframe.
void player_mesh_stand_in(const entities::Entity* e, pass_builder_t& draws,
                          const linalg::vec3& position, color_t color)
{
  push_mesh(draws, assets::load_mesh("resources/obj/Pyramid.obj"), position,
            e->orientation, {1, 1, 1}, color, renderer::fill_mode_t::wireframe);
}

void trigger_volume_diagram(const entities::Entity* e, pass_builder_t& draws,
                            const linalg::vec3& position, color_t color)
{
  draw_trigger_volume_shape(
      draws, position,
      static_cast<const entities::Trigger_Volume_Entity*>(e)->volume.half_extents,
      color);
}

void reflection_volume_diagram(const entities::Entity* e, pass_builder_t& draws,
                               const linalg::vec3& position, color_t color)
{
  draw_trigger_volume_shape(
      draws, position,
      static_cast<const entities::Reflection_Volume_Entity*>(e)->volume.half_extents,
      color);
}

void jump_pad_diagram(const entities::Entity* e, pass_builder_t& draws,
                      const linalg::vec3& position, color_t color)
{
  draw_jump_pad_shape(draws, static_cast<const entities::Jump_Pad_Entity*>(e), position, color);
}

void jump_pad_reach(const entities::Entity* e, pass_builder_t& draws,
                    const linalg::vec3& position, color_t color)
{
  draw_jump_pad_arc(draws, static_cast<const entities::Jump_Pad_Entity*>(e), position, color);
}

void point_light_diagram(const entities::Entity* e, pass_builder_t& draws,
                         const linalg::vec3& position, color_t color)
{
  draw_point_light_shape(
      draws, static_cast<const entities::Point_Light_Entity*>(e), position, color);
}

void spot_light_diagram(const entities::Entity* e, pass_builder_t& draws,
                        const linalg::vec3& position, color_t color)
{
  draw_spot_light_shape(
      draws, static_cast<const entities::Spot_Light_Entity*>(e), position, color);
}

void directional_light_diagram(const entities::Entity* e, pass_builder_t& draws,
                               const linalg::vec3& position, color_t color)
{
  draw_directional_light_shape(
      draws, static_cast<const entities::Directional_Light_Entity*>(e), position, color);
}

void point_light_reach(const entities::Entity* e, pass_builder_t& draws,
                       const linalg::vec3& position, color_t color)
{
  draw_point_light_reach(
      draws, static_cast<const entities::Point_Light_Entity*>(e), position, color);
}

void spot_light_reach(const entities::Entity* e, pass_builder_t& draws,
                      const linalg::vec3& position, color_t color)
{
  draw_spot_light_reach(
      draws, static_cast<const entities::Spot_Light_Entity*>(e), position, color);
}

void directional_light_reach(const entities::Entity* e, pass_builder_t& draws,
                             const linalg::vec3& position, color_t color)
{
  draw_directional_light_reach(
      draws, static_cast<const entities::Directional_Light_Entity*>(e), position, color);
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
              .draw_stand_in = &player_spawn_stand_in};

    case entities::entity_type::Player_Spectate_Entity:
      return {.half_extents = player_hull,
              .origin       = placement_origin_t::feet,
              .color        = colors::green,
              .draw_stand_in = &spectate_camera_stand_in};

    case entities::entity_type::Player_Entity:
      return {.half_extents    = player_hull,
              .origin          = placement_origin_t::feet,
              .draw_stand_in   = &player_mesh_stand_in};

    case entities::entity_type::Particle_Emitter_Entity:
      return {.half_extents        = {0, 0, 0},
              .color               = colors::gold,
              .draw_stand_in       = &particle_emitter_stand_in};

    case entities::entity_type::Trigger_Volume_Entity:
      return {.half_extents = static_cast<const entities::Trigger_Volume_Entity*>(e)
                                  ->volume.half_extents,
              .color        = colors::red,
              .draw_diagram = &trigger_volume_diagram};

    case entities::entity_type::Reflection_Volume_Entity:
      return {.half_extents = static_cast<const entities::Reflection_Volume_Entity*>(e)
                                  ->volume.half_extents,
              .color        = colors::cyan,
              .draw_diagram = &reflection_volume_diagram};

    case entities::entity_type::Jump_Pad_Entity:
      return {.half_extents = static_cast<const entities::Jump_Pad_Entity*>(e)
                                  ->volume.half_extents,
              .color        = colors::orange,
              .draw_diagram = &jump_pad_diagram,
              .draw_reach   = &jump_pad_reach};

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
    case entities::entity_type::Sound_Emitter_Entity: // no gizmo yet
      return {.half_extents = point_pick,
              .color        = colors::white,
              .icon         = assets::texture_asset::audio};

    // Lights pick as a point-sized box whatever their reach -- sizing the pick
    // volume to a 512-unit falloff sphere would make one light swallow every
    // click in the room it lights.
    case entities::entity_type::Point_Light_Entity:
      return {.half_extents = point_pick,
              .color        = colors::yellow,
              .draw_diagram = &point_light_diagram,
              .draw_reach   = &point_light_reach,
              .icon         = assets::texture_asset::point_light};

    case entities::entity_type::Spot_Light_Entity:
      return {.half_extents = point_pick,
              .color        = colors::yellow,
              .draw_diagram = &spot_light_diagram,
              .draw_reach   = &spot_light_reach,
              .icon         = assets::texture_asset::spot_light};

    case entities::entity_type::Directional_Light_Entity:
      return {.half_extents = point_pick,
              .color        = colors::yellow,
              .draw_diagram = &directional_light_diagram,
              .draw_reach   = &directional_light_reach,
              .icon         = assets::texture_asset::directional_light};
      
    case entities::entity_type::Game_Rules_Entity:
      return {.half_extents = point_pick,
              .color        = colors::white,
              .icon         = assets::texture_asset::game_rules};
    case entities::entity_type::Logic_Counter_Entity:
     return {.half_extents = point_pick,
              .color        = colors::white,
              .icon         = assets::texture_asset::counter};

    case entities::entity_type::Invalid:
      break;
  }

  return default_entity_traits(e);
}

} // namespace

// ===================================================================
// The drivers. Every context draws the same three layers:
//   art      the render component, else the type's stand-in, else a wire box
//   diagram  always, on top of the art
//   reach    on top of that, selected and placing only
// ===================================================================

namespace
{

bool try_draw_render_component(const entities::Entity* e, pass_builder_t& draws,
                               const linalg::vec3& position, color_t color,
                               renderer::fill_mode_t fill)
{
  const entities::Render* rc = entities::get_render(e);
  if (!rc || !rc->visible)
    return false;

  if (fill == renderer::fill_mode_t::solid && rc->is_wireframe)
    fill = renderer::fill_mode_t::wireframe;

  const color_t tint = fill == renderer::fill_mode_t::solid ? color_from_vec3(rc->material.color)
                                                            : color;
  return push_mesh(draws, assets::get_mesh(rc->mesh), position,
                   linalg::compose_model_rotation(e->orientation, rc->rotation), rc->scale, tint,
                   fill, &rc->material);
}

// debug.box takes a CENTER, which is the origin only for centered-origin types
// -- a feet-origin one sits half a hull lower.
void draw_wire_box(const entity_editor_traits_t& traits, pass_builder_t& draws,
                   const linalg::vec3& origin, color_t color, float depth_bias)
{
  const float lift = traits.origin == placement_origin_t::feet ? traits.half_extents.y : 0.f;
  draws.debug.box(origin + linalg::vec3{0, lift, 0}, traits.half_extents, color,
                  renderer::fill_mode_t::wireframe, depth_bias);
}

// The art ladder. `box_when_bare` is false for the in-editor pass, where a
// point type with nothing to draw is what the icon pass exists for.
void draw_art(const entities::Entity* e, const entity_editor_traits_t& traits,
              pass_builder_t& draws, const linalg::vec3& origin, color_t color,
              renderer::fill_mode_t fill, bool box_when_bare, float depth_bias = 0.f)
{
  if (try_draw_render_component(e, draws, origin, color, fill))
    return;
  if (traits.draw_stand_in)
    traits.draw_stand_in(e, draws, origin, color);
  else if (box_when_bare)
    draw_wire_box(traits, draws, origin, color, depth_bias);
}

} // namespace

entity_icon_t get_entity_icon(const entities::Entity* e)
{
  const entity_editor_traits_t traits = editor_traits_for(e);
  return {.texture = traits.icon, .fallback_color = traits.color};
}

linalg::vec3 get_placement_half_extents(const entities::Entity* e)
{
  return editor_traits_for(e).half_extents;
}

float get_placement_origin_height(const entities::Entity* e)
{
  const entity_editor_traits_t traits = editor_traits_for(e);
  return traits.origin == placement_origin_t::feet ? 0.f : traits.half_extents.y;
}

void draw_entity_ghost(const entities::Entity* e, pass_builder_t& draws,
                       const linalg::vec3& origin)
{
  const entity_editor_traits_t traits = editor_traits_for(e);
  draw_art(e, traits, draws, origin, traits.color, renderer::fill_mode_t::wireframe, true);
  if (traits.draw_diagram)
    traits.draw_diagram(e, draws, origin, traits.color);
  // Placing IS the moment the reach is the question -- a spot light is aimed by
  // where its cone lands, and finding that out after the click is a placement
  // you then have to undo.
  if (traits.draw_reach)
    traits.draw_reach(e, draws, origin, traits.color);
}

void draw_entity_in_editor(const entities::Entity* e, pass_builder_t& draws)
{
  const entity_editor_traits_t traits = editor_traits_for(e);
  draw_art(e, traits, draws, e->position, traits.color, renderer::fill_mode_t::solid, false);
  if (traits.draw_diagram)
    traits.draw_diagram(e, draws, e->position, traits.color);
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

void draw_selection_highlight(const entities::Entity* e,
                              pass_builder_t& draws, float time,
                              float)
{
  const color_t color = compute_selection_pulse_color(time);

  // A very strong bias so the box renders in FRONT of the surface it traces.
  constexpr float highlight_bias = -200.0f;

  const entity_editor_traits_t traits = editor_traits_for(e);
  draw_art(e, traits, draws, e->position, color, renderer::fill_mode_t::wireframe, true,
           highlight_bias);
  if (traits.draw_diagram)
    traits.draw_diagram(e, draws, e->position, color);
  if (traits.draw_reach)
    traits.draw_reach(e, draws, e->position, color);
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
