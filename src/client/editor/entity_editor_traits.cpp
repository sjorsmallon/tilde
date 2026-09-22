#include "../../shared/player_constants.hpp"
#include "../../shared/entities/entity_reflection.hpp"
#include "entity_editor_traits.hpp"
#include "../../shared/asset.hpp"
#include "../../shared/editor_grid.hpp"
#include "../../shared/map.hpp"
#include "../../shared/shapes.hpp"
#include "render_assets.hpp"
#include "renderer.hpp"
#include "../../shared/aabb.hpp"
#include "../../shared/array.hpp"
#include "../../shared/log.hpp"
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

// Where a launched player lands: the ballistic arc under `gravity`, cut where
// it drops below the pad.
void draw_jump_pad_arc(pass_builder_t& draws, const entities::Jump_Pad_Entity* pad,
                       const linalg::vec3& position, color_t color, float gravity)
{
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
                                 const linalg::vec3&, color_t,
                                 const entity_draw_settings_t&);

// The shape a type is drawn and picked as when no component says otherwise.
// Feet-versus-centred is implicit in the shape: the hull RISES from position,
// so a spawn placed on a floor lands with its feet on it rather than half a
// hull in the air.
enum class stand_in_shape_t
{
  none, // a point
  player_hull,
  spectate_frustum,
  pyramid_marker,
};

// A point type's pick box. Small enough that a lamp does not swallow the wall
// behind it, big enough to click; its screen-space icon is the visible thing.
constexpr float POINT_PICK_HALF_EXTENT = 16.f;

struct editor_data_per_entity_type_t
{
  entities::entity_type                type;
  color_t                              color         = colors::white; // stand-in and diagram
  std::optional<assets::texture_asset> icon;
  stand_in_shape_t                     stand_in      = stand_in_shape_t::none;
  draw_function_t                      draw_stand_in = nullptr;
  draw_function_t                      draw_diagram  = nullptr;
  draw_function_t                      draw_reach    = nullptr;
};

// ---- stand-ins ----------------------------------------------------------

void player_spawn_stand_in(const entities::Entity* e, pass_builder_t& draws,
                           const linalg::vec3& position, color_t color,
                           const entity_draw_settings_t&)
{
  draw_player_spawn_shape(draws, position, e->orientation, color);
}

void spectate_camera_stand_in(const entities::Entity* e, pass_builder_t& draws,
                              const linalg::vec3& position, color_t color,
                              const entity_draw_settings_t&)
{
  draw_spectate_camera_shape(draws, position, e->orientation, color);
}

void particle_emitter_stand_in(const entities::Entity*, pass_builder_t& draws,
                               const linalg::vec3& position, color_t color,
                               const entity_draw_settings_t&)
{
  draw_particle_emitter_shape(draws, position, color);
}

// Player_Entity is runtime-spawned and has no placeable representation of its
// own; the marker is the pyramid, by id.
void pyramid_marker_stand_in(const entities::Entity* e, pass_builder_t& draws,
                             const linalg::vec3& position, color_t color,
                             const entity_draw_settings_t&)
{
  push_mesh(draws, assets::get_mesh(assets::mesh_asset::Pyramid), position, e->orientation,
            {1, 1, 1}, color, renderer::fill_mode_t::wireframe);
}

// ---- diagrams and reach -------------------------------------------------

// Every Box_Volume owner draws its box the same way: a trigger, a reflection
// volume, a damageable's hitbox.
void box_volume_diagram(const entities::Entity* e, pass_builder_t& draws,
                        const linalg::vec3& position, color_t color,
                        const entity_draw_settings_t&)
{
  const entities::Box_Volume* volume = entities::get_box_volume(e);
  if (!volume)
  {
    log_error("box_volume_diagram: {} carries no Box_Volume",
              entities::entity_info(e->type).classname);
    return;
  }
  draws.debug.box(position + volume->position, volume->half_extents, color);
}

void jump_pad_diagram(const entities::Entity* e, pass_builder_t& draws,
                      const linalg::vec3& position, color_t color,
                      const entity_draw_settings_t&)
{
  draw_jump_pad_shape(draws, static_cast<const entities::Jump_Pad_Entity*>(e), position, color);
}

void jump_pad_reach(const entities::Entity* e, pass_builder_t& draws,
                    const linalg::vec3& position, color_t color,
                    const entity_draw_settings_t& settings)
{
  draw_jump_pad_arc(draws, static_cast<const entities::Jump_Pad_Entity*>(e), position, color,
                    settings.gravity);
}

void launcher_diagram(const entities::Entity* e, pass_builder_t& draws,
                      const linalg::vec3& position, color_t color,
                      const entity_draw_settings_t&)
{
  constexpr float LAUNCHER_AIM_ARROW_LENGTH = 96.f;
  draws.debug.arrow(position, position + linalg::forward(e->orientation) * LAUNCHER_AIM_ARROW_LENGTH,
                    color);
}

void point_light_diagram(const entities::Entity* e, pass_builder_t& draws,
                         const linalg::vec3& position, color_t color,
                         const entity_draw_settings_t&)
{
  draw_point_light_shape(
      draws, static_cast<const entities::Point_Light_Entity*>(e), position, color);
}

void spot_light_diagram(const entities::Entity* e, pass_builder_t& draws,
                        const linalg::vec3& position, color_t color,
                        const entity_draw_settings_t&)
{
  draw_spot_light_shape(
      draws, static_cast<const entities::Spot_Light_Entity*>(e), position, color);
}

void directional_light_diagram(const entities::Entity* e, pass_builder_t& draws,
                               const linalg::vec3& position, color_t color,
                               const entity_draw_settings_t&)
{
  draw_directional_light_shape(
      draws, static_cast<const entities::Directional_Light_Entity*>(e), position, color);
}

void point_light_reach(const entities::Entity* e, pass_builder_t& draws,
                       const linalg::vec3& position, color_t color,
                       const entity_draw_settings_t&)
{
  draw_point_light_reach(
      draws, static_cast<const entities::Point_Light_Entity*>(e), position, color);
}

void spot_light_reach(const entities::Entity* e, pass_builder_t& draws,
                      const linalg::vec3& position, color_t color,
                      const entity_draw_settings_t&)
{
  draw_spot_light_reach(
      draws, static_cast<const entities::Spot_Light_Entity*>(e), position, color);
}

void directional_light_reach(const entities::Entity* e, pass_builder_t& draws,
                             const linalg::vec3& position, color_t color,
                             const entity_draw_settings_t&)
{
  draw_directional_light_reach(
      draws, static_cast<const entities::Directional_Light_Entity*>(e), position, color);
}

// ---- the table ----------------------------------------------------------
//
// A row carries only what is constant for the TYPE. Anything read off the
// instance (a box, a mesh, a range) is editor_shape_at's business or the draw
// function's, never a row's -- which is why a row can be constexpr and why the
// table needs no switch to build.
//
// Lights get no stand-in and pick as a point whatever their reach: sizing the
// pick volume to a 512-unit falloff sphere would make one light swallow every
// click in the room it lights.

using entities::entity_type;

constexpr Enum_Array<entity_type, editor_data_per_entity_type_t> EDITOR_DATA_PER_ENTITY_TYPE = {{
    {.type = entity_type::Invalid},

    {.type          = entity_type::Player_Spawn_Entity,
     .color         = colors::pink,
     .stand_in      = stand_in_shape_t::player_hull,
     .draw_stand_in = &player_spawn_stand_in},

    {.type          = entity_type::Player_Spectate_Entity,
     .color         = colors::green,
     .stand_in      = stand_in_shape_t::spectate_frustum,
     .draw_stand_in = &spectate_camera_stand_in},

    {.type          = entity_type::Player_Entity,
     .stand_in      = stand_in_shape_t::pyramid_marker,
     .draw_stand_in = &pyramid_marker_stand_in},

    {.type = entity_type::Weapon_Entity, .draw_diagram = &box_volume_diagram},
    {.type = entity_type::Rocket_Entity}, // runtime only
    {.type = entity_type::Hook_Entity}, // runtime only
    {.type = entity_type::Kooh_Entity}, // runtime only
    {.type = entity_type::Ricochet_Entity}, // runtime only
    {.type = entity_type::Platform_Entity}, // runtime only
    {.type = entity_type::Canopy_Entity}, // runtime only
    {.type = entity_type::Bubble_Entity}, // runtime only
    {.type = entity_type::Physics_Body_Entity},

    {.type = entity_type::Damageable_Entity, .draw_diagram = &box_volume_diagram},

    {.type          = entity_type::Particle_Emitter_Entity,
     .color         = colors::gold,
     .draw_stand_in = &particle_emitter_stand_in},

    {.type = entity_type::Sound_Emitter_Entity, .icon = assets::texture_asset::audio},

    {.type         = entity_type::Point_Light_Entity,
     .color        = colors::yellow,
     .icon         = assets::texture_asset::point_light,
     .draw_diagram = &point_light_diagram,
     .draw_reach   = &point_light_reach},

    {.type         = entity_type::Spot_Light_Entity,
     .color        = colors::yellow,
     .icon         = assets::texture_asset::spot_light,
     .draw_diagram = &spot_light_diagram,
     .draw_reach   = &spot_light_reach},

    {.type         = entity_type::Directional_Light_Entity,
     .color        = colors::yellow,
     .icon         = assets::texture_asset::directional_light,
     .draw_diagram = &directional_light_diagram,
     .draw_reach   = &directional_light_reach},

    {.type = entity_type::Trigger_Volume_Entity, .color = colors::red, .draw_diagram = &box_volume_diagram},

    {.type         = entity_type::Jump_Pad_Entity,
     .color        = colors::orange,
     .draw_diagram = &jump_pad_diagram,
     .draw_reach   = &jump_pad_reach},

    {.type = entity_type::Reflection_Volume_Entity, .color = colors::cyan, .draw_diagram = &box_volume_diagram},

    {.type = entity_type::Game_Rules_Entity, .icon = assets::texture_asset::game_rules},
    {.type = entity_type::Logic_Counter_Entity, .icon = assets::texture_asset::counter},
    {.type = entity_type::Geometry_Owner_Entity, .icon = assets::texture_asset::wall_hammer},
    {.type = entity_type::Ping_Marker_Entity}, // runtime only; the render component draws it
    {.type = entity_type::Logic_Timer_Entity, .icon = assets::texture_asset::icon_timer},
    {.type = entity_type::Path_Node_Entity, .color = colors::green},
    {.type = entity_type::Mover_Entity, .color = colors::magenta, .icon = assets::texture_asset::move},
    {.type = entity_type::Launcher_Entity, .color = colors::orange, .draw_diagram = &launcher_diagram},
    {.type = entity_type::Movement_Modifier_Entity, .color = colors::green, .draw_diagram = &box_volume_diagram},
    {.type = entity_type::Remnant_Entity}, // runtime only
}};

static_assert(rows_in_enum_order<&editor_data_per_entity_type_t::type>(EDITOR_DATA_PER_ENTITY_TYPE),
              "EDITOR_DATA_PER_ENTITY_TYPE rows must sit at their own entity_type index");

const editor_data_per_entity_type_t& editor_data_for(const entities::Entity* e)
{
  // try_get, not [] : the type tag came out of a map file or off the wire.
  if (const editor_data_per_entity_type_t* row = EDITOR_DATA_PER_ENTITY_TYPE.try_get(e->type))
    return *row;

  log_error("editor_data_for: entity carries an invalid type tag ({})", (int)e->type);
  return EDITOR_DATA_PER_ENTITY_TYPE[entity_type::Invalid];
}

// ---- the instance shape -------------------------------------------------

shared::aabb_bounds_t scaled_mesh_bounds(const assets::mesh_asset_t& mesh,
                                         const linalg::vec3& scale,
                                         const linalg::vec3& position)
{
  const shared::aabb_bounds_t local  = assets::compute_mesh_bounds(&mesh);
  const linalg::vec3          center = (local.min + local.max) * 0.5f;
  const linalg::vec3          half   = (local.max - local.min) * 0.5f;
  const linalg::vec3          world_center =
      position + linalg::vec3{center.x * scale.x, center.y * scale.y, center.z * scale.z};
  const linalg::vec3 world_half{half.x * scale.x, half.y * scale.y, half.z * scale.z};
  return {world_center - world_half, world_center + world_half};
}

shared::aabb_bounds_t player_hull_rising_from(const linalg::vec3& feet)
{
  return {{feet.x - shared::player_half_width, feet.y, feet.z - shared::player_half_width},
          {feet.x + shared::player_half_width, feet.y + 2.f * shared::player_half_height,
           feet.z + shared::player_half_width}};
}

shared::aabb_bounds_t point_pick_box(const linalg::vec3& position)
{
  const linalg::vec3 half{POINT_PICK_HALF_EXTENT, POINT_PICK_HALF_EXTENT, POINT_PICK_HALF_EXTENT};
  return {position - half, position + half};
}

shared::aabb_bounds_t bounds_of_shape(const editor_shape_t& shape)
{
  if (const shared::spectate_frustum_t* frustum = std::get_if<shared::spectate_frustum_t>(&shape))
    return shared::get_bounds(*frustum);
  return std::get<shared::aabb_bounds_t>(shape);
}

} // namespace

editor_shape_t editor_shape_at(const entities::Entity* e, const linalg::vec3& position)
{
  if (const entities::Box_Volume* volume = entities::get_box_volume(e))
    return shared::get_bounds(*volume, position);

  // The same gate draw_art uses, so an invisible render draws AND picks as the
  // stand-in rather than as a mesh nobody can see.
  if (const entities::Render* render = entities::get_render(e); render && render->visible)
  {
    const assets::mesh_asset_t* mesh = assets::get(assets::get_mesh(render->mesh));
    if (mesh && !mesh->vertices.empty())
      return scaled_mesh_bounds(*mesh, render->scale, position);
  }

  switch (editor_data_for(e).stand_in)
  {
    case stand_in_shape_t::player_hull:
      return player_hull_rising_from(position);

    case stand_in_shape_t::spectate_frustum:
      return shared::make_spectate_frustum(position, e->orientation);

    case stand_in_shape_t::pyramid_marker:
    {
      const assets::mesh_asset_t* mesh = assets::get(assets::get_mesh(assets::mesh_asset::Pyramid));
      if (mesh && !mesh->vertices.empty())
        return scaled_mesh_bounds(*mesh, {1, 1, 1}, position);
      break;
    }

    case stand_in_shape_t::none:
      break;
  }

  return point_pick_box(position);
}

shared::aabb_bounds_t editor_bounds_of(const entities::Entity* e)
{
  return bounds_of_shape(editor_shape_at(e, e->position));
}

std::vector<Plane> editor_collision_planes_of(const entities::Entity* e)
{
  const editor_shape_t shape = editor_shape_at(e, e->position);
  if (const shared::spectate_frustum_t* frustum = std::get_if<shared::spectate_frustum_t>(&shape))
    return shared::compute_collision_planes(*frustum);
  return shared::compute_collision_planes(shared::to_aabb(std::get<shared::aabb_bounds_t>(shape)));
}

// ===================================================================
// The drivers. Every context draws the same three layers:
//   art      the render component, else the type's stand-in, else the shape as a wire box
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

// The shape as a wire box: the same bounds the pick uses, so what is drawn for
// a bare entity is exactly what a click hits.
void draw_shape_wire_box(const entities::Entity* e, pass_builder_t& draws,
                         const linalg::vec3& origin, color_t color, float depth_bias)
{
  const shared::aabb_bounds_t bounds = bounds_of_shape(editor_shape_at(e, origin));
  draws.debug.box((bounds.min + bounds.max) * 0.5f, (bounds.max - bounds.min) * 0.5f, color,
                  renderer::fill_mode_t::wireframe, depth_bias);
}

// The art ladder. `box_when_bare` is false in the editor pass only for a type
// the icon pass or a diagram already shows.
void draw_art(const entities::Entity* e, const editor_data_per_entity_type_t& row,
              pass_builder_t& draws, const linalg::vec3& origin, color_t color,
              renderer::fill_mode_t fill, bool box_when_bare,
              const entity_draw_settings_t& settings, float depth_bias = 0.f)
{
  if (try_draw_render_component(e, draws, origin, color, fill))
    return;
  if (row.draw_stand_in)
    row.draw_stand_in(e, draws, origin, color, settings);
  else if (box_when_bare)
    draw_shape_wire_box(e, draws, origin, color, depth_bias);
}

} // namespace

entity_icon_t get_entity_icon(const entities::Entity* e)
{
  const editor_data_per_entity_type_t& row = editor_data_for(e);
  return {.texture = row.icon, .fallback_color = row.color};
}

void draw_entity_ghost(const entities::Entity* e, pass_builder_t& draws,
                       const linalg::vec3& origin, const entity_draw_settings_t& settings)
{
  const editor_data_per_entity_type_t& row = editor_data_for(e);
  draw_art(e, row, draws, origin, row.color, renderer::fill_mode_t::wireframe, true, settings);
  if (row.draw_diagram)
    row.draw_diagram(e, draws, origin, row.color, settings);
  // Placing IS the moment the reach is the question -- a spot light is aimed by
  // where its cone lands, and finding that out after the click is a placement
  // you then have to undo.
  if (row.draw_reach)
    row.draw_reach(e, draws, origin, row.color, settings);
}

void draw_entity_in_editor(const entities::Entity* e, pass_builder_t& draws,
                           const entity_draw_settings_t& settings)
{
  const editor_data_per_entity_type_t& row = editor_data_for(e);
  const bool box_when_bare = !row.icon && !row.draw_diagram;
  draw_art(e, row, draws, e->position, row.color, renderer::fill_mode_t::solid, box_when_bare,
           settings);
  if (row.draw_diagram)
    row.draw_diagram(e, draws, e->position, row.color, settings);
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

void draw_selection_highlight(const entities::Entity* e, pass_builder_t& draws, float time,
                              float, const entity_draw_settings_t& settings,
                              bool art_is_outlined)
{
  const color_t color = compute_selection_pulse_color(time);

  // A very strong bias so the box renders in FRONT of the surface it traces.
  constexpr float highlight_bias = -200.0f;

  const editor_data_per_entity_type_t& row = editor_data_for(e);
  if (!art_is_outlined)
    draw_art(e, row, draws, e->position, color, renderer::fill_mode_t::wireframe, true, settings,
             highlight_bias);
  if (row.draw_diagram)
    row.draw_diagram(e, draws, e->position, color, settings);
  if (row.draw_reach)
    row.draw_reach(e, draws, e->position, color, settings);
}

// ===================================================================
// Convenience
// ===================================================================

linalg::vec3 compute_placement_origin(const entities::Entity* e,
                                      const linalg::vec3& ghost_position)
{
  // The shape at the origin says how far below position it reaches; lifting by
  // that puts its lowest point on the surface. A frustum is a diagram of where
  // a camera looks rather than a solid that rests, so it takes no lift.
  const editor_shape_t shape = editor_shape_at(e, {0, 0, 0});
  float                lift  = 0.f;
  if (const shared::aabb_bounds_t* bounds = std::get_if<shared::aabb_bounds_t>(&shape))
    lift = -bounds->min.y;
  return ghost_position + linalg::vec3{0, lift, 0};
}

} // namespace client
