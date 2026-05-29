#include "entity_editor_traits.hpp"
#include "../../shared/asset.hpp"
#include "../../shared/editor_grid.hpp"
#include "../../shared/entities/entity_list.hpp"
#include "../../shared/map.hpp"
#include "../../shared/shapes.hpp"
#include "renderer.hpp"
#include <cmath>
#include <cstring>
#include <print>
#include <string>

// Pull in all entity headers so the X-macro can see every class.
#define ENTITIES_WANT_INCLUDES
#include "../../shared/entities/entity_list.hpp"
#undef ENTITIES_WANT_INCLUDES

namespace client
{

// ===================================================================
// Template specializations — one per entity type in SHARED_ENTITIES_LIST.
// If you add a new entity and forget to specialize, the linker will
// tell you exactly which instantiation is missing.
// ===================================================================

// -- AABB ---------------------------------------------------------------

template <>
linalg::vec3
Entity_Editor_Traits<network::AABB_Entity>::get_half_extents(
    const network::AABB_Entity *e)
{
  return e->volume.half_extents;
}

template <>
bool Entity_Editor_Traits<network::AABB_Entity>::draw_ghost(
    const network::AABB_Entity *, overlay_renderer_t &, const linalg::vec3 &)
{
  return false; // has render component, use default
}

template <>
bool Entity_Editor_Traits<network::AABB_Entity>::draw_in_editor(
    const network::AABB_Entity *e, overlay_renderer_t &renderer,
    uint32_t uid, bool)
{
  auto cmd = renderer.get_command_buffer();
  renderer::DrawAABB(cmd, e->position - e->volume.half_extents,
                     e->position + e->volume.half_extents, colors::white,
                     /*as_wireframe=*/false,
                     /*random_color=*/true,
                     /*random_seed=*/uid);
  return true;
}

template <>
bool Entity_Editor_Traits<network::AABB_Entity>::draw_selection_wireframe(
    const network::AABB_Entity *e, overlay_renderer_t &renderer, color_t color,
    float grid_step)
{
  const linalg::vec3 &p = e->position;
  const linalg::vec3 &h = e->volume.half_extents;
  const float x0 = p.x - h.x, x1 = p.x + h.x;
  const float y0 = p.y - h.y, y1 = p.y + h.y;
  const float z0 = p.z - h.z, z1 = p.z + h.z;

  // Draw grid lines on each of the 6 faces at world-aligned grid_step intervals.
  // Lines parallel to Z (varying x) and parallel to X (varying z) on Y faces.
  // Lines parallel to Z (varying y) and parallel to Y (varying z) on X faces.
  // Lines parallel to X (varying y) and parallel to Y (varying x) on Z faces.

  auto draw_grid_xz = [&](float y, float xa, float xb, float za, float zb)
  {
    float xs = std::ceil(xa / grid_step) * grid_step;
    for (float x = xs; x <= xb + 1e-3f; x += grid_step)
      renderer.draw_line({x, y, za}, {x, y, zb}, color);
    float zs = std::ceil(za / grid_step) * grid_step;
    for (float z = zs; z <= zb + 1e-3f; z += grid_step)
      renderer.draw_line({xa, y, z}, {xb, y, z}, color);
  };

  auto draw_grid_xz_outer = [&](float y, float xa, float xb, float za, float zb)
  {
    // Always draw the 4 outer edges regardless of grid alignment
    renderer.draw_line({xa, y, za}, {xb, y, za}, color);
    renderer.draw_line({xb, y, za}, {xb, y, zb}, color);
    renderer.draw_line({xb, y, zb}, {xa, y, zb}, color);
    renderer.draw_line({xa, y, zb}, {xa, y, za}, color);
    draw_grid_xz(y, xa, xb, za, zb);
  };

  auto draw_grid_yz = [&](float x, float ya, float yb, float za, float zb)
  {
    float ys = std::ceil(ya / grid_step) * grid_step;
    for (float y = ys; y <= yb + 1e-3f; y += grid_step)
      renderer.draw_line({x, y, za}, {x, y, zb}, color);
    float zs = std::ceil(za / grid_step) * grid_step;
    for (float z = zs; z <= zb + 1e-3f; z += grid_step)
      renderer.draw_line({x, ya, z}, {x, yb, z}, color);
  };

  auto draw_grid_xy = [&](float z, float xa, float xb, float ya, float yb)
  {
    float xs = std::ceil(xa / grid_step) * grid_step;
    for (float x = xs; x <= xb + 1e-3f; x += grid_step)
      renderer.draw_line({x, ya, z}, {x, yb, z}, color);
    float ys = std::ceil(ya / grid_step) * grid_step;
    for (float y = ys; y <= yb + 1e-3f; y += grid_step)
      renderer.draw_line({xa, y, z}, {xb, y, z}, color);
  };

  // Top and bottom (Y faces)
  draw_grid_xz_outer(y1, x0, x1, z0, z1);
  draw_grid_xz_outer(y0, x0, x1, z0, z1);
  // Left and right (X faces) — vertical edges already drawn above
  draw_grid_yz(x0, y0, y1, z0, z1);
  draw_grid_yz(x1, y0, y1, z0, z1);
  // Front and back (Z faces)
  draw_grid_xy(z0, x0, x1, y0, y1);
  draw_grid_xy(z1, x0, x1, y0, y1);

  return true;
}

// -- Wedge --------------------------------------------------------------

template <>
linalg::vec3
Entity_Editor_Traits<network::Wedge_Entity>::get_half_extents(
    const network::Wedge_Entity *e)
{
  return e->half_extents;
}

template <>
bool Entity_Editor_Traits<network::Wedge_Entity>::draw_ghost(
    const network::Wedge_Entity *e, overlay_renderer_t &renderer,
    const linalg::vec3 &center)
{
  shared::wedge_t w;
  w.center = center;
  w.half_extents = e->half_extents;
  w.orientation = e->orientation;

  auto points = shared::get_wedge_points(w);

  renderer.draw_line(points[0], points[1], colors::yellow);
  renderer.draw_line(points[1], points[2], colors::yellow);
  renderer.draw_line(points[2], points[3], colors::yellow);
  renderer.draw_line(points[3], points[0], colors::yellow);
  renderer.draw_line(points[4], points[5], colors::yellow);
  renderer.draw_line(points[0], points[4], colors::yellow);
  renderer.draw_line(points[1], points[5], colors::yellow);
  renderer.draw_line(points[3], points[4], colors::yellow);
  renderer.draw_line(points[2], points[5], colors::yellow);
  return true;
}

template <>
bool Entity_Editor_Traits<network::Wedge_Entity>::draw_in_editor(
    const network::Wedge_Entity *e, overlay_renderer_t &renderer,
    uint32_t uid, bool solid)
{
  shared::wedge_t w;
  w.center = e->position;
  w.half_extents = e->half_extents;
  w.orientation = e->orientation;
  renderer::draw_wedge(renderer.get_command_buffer(), w, colors::white);
  return true;
}

template <>
bool Entity_Editor_Traits<network::Wedge_Entity>::draw_selection_wireframe(
    const network::Wedge_Entity *e, overlay_renderer_t &renderer,
    color_t color, float)
{
  shared::wedge_t w;
  w.center = e->position;
  w.half_extents = e->half_extents;
  w.orientation = e->orientation;
  auto points = shared::get_wedge_points(w);
  renderer.draw_line(points[0], points[1], color);
  renderer.draw_line(points[1], points[2], color);
  renderer.draw_line(points[2], points[3], color);
  renderer.draw_line(points[3], points[0], color);
  renderer.draw_line(points[4], points[5], color);
  renderer.draw_line(points[0], points[4], color);
  renderer.draw_line(points[1], points[5], color);
  renderer.draw_line(points[3], points[4], color);
  renderer.draw_line(points[2], points[5], color);
  return true;
}

// -- Player Spawn -------------------------------------------------------

template <>
linalg::vec3
Entity_Editor_Traits<network::Player_Spawn_Entity>::get_half_extents(
    const network::Player_Spawn_Entity *)
{
  return {network::player_half_width, network::player_half_height,
          network::player_half_width};
}

template <>
bool Entity_Editor_Traits<network::Player_Spawn_Entity>::draw_ghost(
    const network::Player_Spawn_Entity *, overlay_renderer_t &renderer,
    const linalg::vec3 &center)
{
  const linalg::vec3 hull{network::player_half_width,
                          network::player_half_height,
                          network::player_half_width};
  renderer.draw_wire_box(center, hull, colors::pink);
  renderer.draw_line(center, center + linalg::vec3{0, 48, 0}, colors::pink);
  return true;
}

template <>
bool Entity_Editor_Traits<network::Player_Spawn_Entity>::draw_in_editor(
    const network::Player_Spawn_Entity *e, overlay_renderer_t &renderer,
    uint32_t, bool)
{
  const linalg::vec3 hull{network::player_half_width,
                          network::player_half_height,
                          network::player_half_width};
  renderer.draw_wire_box(e->position, hull, colors::pink);
  renderer.draw_line(e->position, e->position + linalg::vec3{0, 48, 0}, colors::pink);
  return true;
}

template <>
bool Entity_Editor_Traits<network::Player_Spawn_Entity>::draw_selection_wireframe(
    const network::Player_Spawn_Entity *, overlay_renderer_t &, color_t, float)
{
  return false; // use AABB fallback
}

// -- Particle Emitter ---------------------------------------------------

template <>
linalg::vec3
Entity_Editor_Traits<network::Particle_Emitter_Entity>::get_half_extents(
    const network::Particle_Emitter_Entity *)
{
  return {0, 0, 0}; // point entity
}

template <>
bool Entity_Editor_Traits<network::Particle_Emitter_Entity>::draw_ghost(
    const network::Particle_Emitter_Entity *, overlay_renderer_t &renderer,
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
bool Entity_Editor_Traits<network::Particle_Emitter_Entity>::draw_in_editor(
    const network::Particle_Emitter_Entity *e, overlay_renderer_t &renderer,
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
bool Entity_Editor_Traits<network::Particle_Emitter_Entity>::draw_selection_wireframe(
    const network::Particle_Emitter_Entity *, overlay_renderer_t &, color_t, float)
{
  return false; // use AABB fallback
}

// -- Static Mesh --------------------------------------------------------

template <>
linalg::vec3
Entity_Editor_Traits<network::Static_Mesh_Entity>::get_half_extents(
    const network::Static_Mesh_Entity *)
{
  return {editor::DEFAULT_HALF_EXTENT, editor::DEFAULT_HALF_EXTENT,
          editor::DEFAULT_HALF_EXTENT};
}

template <>
bool Entity_Editor_Traits<network::Static_Mesh_Entity>::draw_ghost(
    const network::Static_Mesh_Entity *, overlay_renderer_t &, const linalg::vec3 &)
{
  return false; // has render component, use default
}

template <>
bool Entity_Editor_Traits<network::Static_Mesh_Entity>::draw_in_editor(
    const network::Static_Mesh_Entity *e, overlay_renderer_t &renderer,
    uint32_t uid, bool solid)
{
  // No mesh resolved by render component — draw placeholder AABB
  auto bounds = shared::compute_entity_bounds(e);
  renderer::DrawAABB(renderer.get_command_buffer(), bounds.min, bounds.max,
                     colors::yellow, /*as_wireframe=*/!solid,
                     /*random_color=*/solid, /*random_seed=*/uid);
  return true;
}

template <>
bool Entity_Editor_Traits<network::Static_Mesh_Entity>::draw_selection_wireframe(
    const network::Static_Mesh_Entity *, overlay_renderer_t &, color_t, float)
{
  return false; // try render component mesh first in draw_selection_highlight
}

// -- Weapon -------------------------------------------------------------

template <>
linalg::vec3
Entity_Editor_Traits<network::Weapon_Entity>::get_half_extents(
    const network::Weapon_Entity *)
{
  return {editor::DEFAULT_HALF_EXTENT, editor::DEFAULT_HALF_EXTENT,
          editor::DEFAULT_HALF_EXTENT};
}

template <>
bool Entity_Editor_Traits<network::Weapon_Entity>::draw_ghost(
    const network::Weapon_Entity *, overlay_renderer_t &, const linalg::vec3 &)
{
  return false; // has render component, use default
}

template <>
bool Entity_Editor_Traits<network::Weapon_Entity>::draw_in_editor(
    const network::Weapon_Entity *, overlay_renderer_t &, uint32_t, bool)
{
  return false; // relies on render component
}

template <>
bool Entity_Editor_Traits<network::Weapon_Entity>::draw_selection_wireframe(
    const network::Weapon_Entity *, overlay_renderer_t &, color_t, float)
{
  return false; // try render component mesh first
}

// -- Player (runtime only, not placed in editor) ------------------------

template <>
linalg::vec3
Entity_Editor_Traits<network::Player_Entity>::get_half_extents(
    const network::Player_Entity *)
{
  return {network::player_half_width, network::player_half_height,
          network::player_half_width};
}

template <>
bool Entity_Editor_Traits<network::Player_Entity>::draw_ghost(
    const network::Player_Entity *, overlay_renderer_t &, const linalg::vec3 &)
{
  return false;
}

template <>
bool Entity_Editor_Traits<network::Player_Entity>::draw_in_editor(
    const network::Player_Entity *e, overlay_renderer_t &renderer,
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
bool Entity_Editor_Traits<network::Player_Entity>::draw_selection_wireframe(
    const network::Player_Entity *e, overlay_renderer_t &renderer, color_t color,
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

// -- Displacement -------------------------------------------------------

template <>
linalg::vec3
Entity_Editor_Traits<network::Displacement_Entity>::get_half_extents(
    const network::Displacement_Entity *e)
{
  return e->volume.half_extents;
}

template <>
bool Entity_Editor_Traits<network::Displacement_Entity>::draw_ghost(
    const network::Displacement_Entity *, overlay_renderer_t &,
    const linalg::vec3 &)
{
  return false; // use default wire box
}

template <>
bool Entity_Editor_Traits<network::Displacement_Entity>::draw_in_editor(
    const network::Displacement_Entity *e, overlay_renderer_t &renderer,
    uint32_t uid, bool)
{
  std::string displacement_key = "__displacement_" + std::to_string(uid);
  auto mesh_handle = assets::find_mesh_in_cache(displacement_key.c_str());
  if (!mesh_handle.valid())
  {
    auto mesh = network::generate_displacement_mesh(*e);
    mesh_handle = assets::register_dynamic_mesh(displacement_key.c_str(),
                                                std::move(mesh));
  }
  if (mesh_handle.valid())
  {
    renderer::draw_mesh(renderer.get_command_buffer(), mesh_handle,
                       {.position = e->position,
                        .rotation = e->orientation,
                        .shader = renderer::ShaderType::Textured});
    return true;
  }
  return false;
}

template <>
bool Entity_Editor_Traits<network::Displacement_Entity>::draw_selection_wireframe(
    const network::Displacement_Entity *, overlay_renderer_t &, color_t, float)
{
  return false; // use AABB fallback
}

// -- Trigger Volume -----------------------------------------------------

template <>
linalg::vec3
Entity_Editor_Traits<network::Trigger_Volume_Entity>::get_half_extents(
    const network::Trigger_Volume_Entity *e)
{
  return e->volume.half_extents;
}

template <>
bool Entity_Editor_Traits<network::Trigger_Volume_Entity>::draw_ghost(
    const network::Trigger_Volume_Entity *e, overlay_renderer_t &renderer,
    const linalg::vec3 &center)
{
  renderer.draw_wire_box(center, e->volume.half_extents, colors::red); // red
  return true;
}

template <>
bool Entity_Editor_Traits<network::Trigger_Volume_Entity>::draw_in_editor(
    const network::Trigger_Volume_Entity *e, overlay_renderer_t &renderer,
    uint32_t, bool)
{
  renderer.draw_wire_box(e->position, e->volume.half_extents, colors::red); // red
  return true;
}

template <>
bool Entity_Editor_Traits<network::Trigger_Volume_Entity>::draw_selection_wireframe(
    const network::Trigger_Volume_Entity *e, overlay_renderer_t &renderer,
    color_t color, float)
{
  renderer.draw_wire_box(e->position, e->volume.half_extents, color);
  return true;
}

// -- Rocket (runtime only, not placed in editor) ------------------------

template <>
linalg::vec3
Entity_Editor_Traits<network::Rocket_Entity>::get_half_extents(
    const network::Rocket_Entity *)
{
  return {editor::DEFAULT_HALF_EXTENT, editor::DEFAULT_HALF_EXTENT,
          editor::DEFAULT_HALF_EXTENT};
}

template <>
bool Entity_Editor_Traits<network::Rocket_Entity>::draw_ghost(
    const network::Rocket_Entity *, overlay_renderer_t &, const linalg::vec3 &)
{
  return false;
}

template <>
bool Entity_Editor_Traits<network::Rocket_Entity>::draw_in_editor(
    const network::Rocket_Entity *, overlay_renderer_t &, uint32_t, bool)
{
  return false; // runtime only
}

template <>
bool Entity_Editor_Traits<network::Rocket_Entity>::draw_selection_wireframe(
    const network::Rocket_Entity *, overlay_renderer_t &, color_t, float)
{
  return false;
}

// -- Light (point/spot/directional, drawn as a cross in editor) ------

template <>
linalg::vec3
Entity_Editor_Traits<network::Light_Entity>::get_half_extents(
    const network::Light_Entity *)
{
  return {editor::DEFAULT_HALF_EXTENT, editor::DEFAULT_HALF_EXTENT,
          editor::DEFAULT_HALF_EXTENT};
}

template <>
bool Entity_Editor_Traits<network::Light_Entity>::draw_ghost(
    const network::Light_Entity *, overlay_renderer_t &renderer,
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
bool Entity_Editor_Traits<network::Light_Entity>::draw_in_editor(
    const network::Light_Entity *e, overlay_renderer_t &renderer,
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
bool Entity_Editor_Traits<network::Light_Entity>::draw_selection_wireframe(
    const network::Light_Entity *e, overlay_renderer_t &renderer,
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
Entity_Editor_Traits<network::Physics_Body_Entity>::get_half_extents(
    const network::Physics_Body_Entity *e)
{
  return e ? e->size
           : linalg::vec3{editor::DEFAULT_HALF_EXTENT,
                          editor::DEFAULT_HALF_EXTENT,
                          editor::DEFAULT_HALF_EXTENT};
}

template <>
bool Entity_Editor_Traits<network::Physics_Body_Entity>::draw_ghost(
    const network::Physics_Body_Entity *, overlay_renderer_t &,
    const linalg::vec3 &)
{
  return false;
}

template <>
bool Entity_Editor_Traits<network::Physics_Body_Entity>::draw_in_editor(
    const network::Physics_Body_Entity *, overlay_renderer_t &, uint32_t, bool)
{
  return false; // runtime only — never appears in editor
}

template <>
bool Entity_Editor_Traits<network::Physics_Body_Entity>::draw_selection_wireframe(
    const network::Physics_Body_Entity *, overlay_renderer_t &, color_t, float)
{
  return false;
}

// ===================================================================
// X-macro dispatch: runtime entity* -> compile-time specialization
// ===================================================================

linalg::vec3 get_placement_half_extents(const network::Entity *e)
{
  // Box-volume entities (AABB, Trigger_Volume, Displacement, ...) share the
  // geometry path: any entity that owns a box_volume_t reports its extents
  // through the virtual hook, no per-class trait code required.
  if (const shared::box_volume_t *volume = e->get_box_volume())
    return volume->half_extents;

#define X(ENUM, CLASS, NAME, PATH)                                             \
  if (dynamic_cast<const CLASS *>(e))                                          \
    return Entity_Editor_Traits<CLASS>::get_half_extents(                       \
        static_cast<const CLASS *>(e));
  SHARED_ENTITIES_LIST(X)
#undef X
  return {editor::DEFAULT_HALF_EXTENT, editor::DEFAULT_HALF_EXTENT,
          editor::DEFAULT_HALF_EXTENT};
}

bool draw_entity_ghost(const network::Entity *e, overlay_renderer_t &renderer,
                       const linalg::vec3 &center)
{
#define X(ENUM, CLASS, NAME, PATH)                                             \
  if (dynamic_cast<const CLASS *>(e))                                          \
    return Entity_Editor_Traits<CLASS>::draw_ghost(                             \
        static_cast<const CLASS *>(e), renderer, center);
  SHARED_ENTITIES_LIST(X)
#undef X
  return false;
}

// ===================================================================
// Editor drawing: render-component path + per-entity trait fallback
// ===================================================================

// Try to draw an entity via its render_component_t. Returns true on success.
static bool try_draw_render_component(const network::Entity *e,
                                      VkCommandBuffer cmd)
{
  const auto *rc = e->get_component<network::render_component_t>();
  if (!rc || !rc->visible)
    return false;

  const char *mesh_path = rc->mesh_path.length > 0 ? rc->mesh_path.c_str() : nullptr;

  if (!mesh_path)
    return false;

  assets::asset_handle_t<assets::mesh_asset_t> mesh_handle;
  if (std::strncmp(mesh_path, "__primitive_", 12) == 0)
    mesh_handle = assets::get_primitive_mesh(mesh_path + 12);
  else
    mesh_handle = assets::load_mesh(mesh_path);

  if (!mesh_handle.valid())
    return false;

  renderer::draw_mesh(cmd, mesh_handle,
                     {.position = e->position,
                      .scale    = rc->scale,
                      .rotation = e->orientation + rc->rotation});
  return true;
}

bool draw_entity_in_editor(const network::Entity *e,
                           overlay_renderer_t &renderer, uint32_t uid,
                           bool solid)
{
  // First: try the render component (common to all entity types).
  if (try_draw_render_component(e, renderer.get_command_buffer()))
    return true;

  // Second: per-entity trait specialization.
#define X(ENUM, CLASS, NAME, PATH)                                             \
  if (dynamic_cast<const CLASS *>(e))                                          \
    return Entity_Editor_Traits<CLASS>::draw_in_editor(                         \
        static_cast<const CLASS *>(e), renderer, uid, solid);
  SHARED_ENTITIES_LIST(X)
#undef X
  return false;
}

// ===================================================================
// Selection highlight: pulsating pink <-> white wireframe
// ===================================================================

static color_t compute_pulsating_color(float time)
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
static bool try_draw_mesh_selection_wireframe(const network::Entity *e,
                                              VkCommandBuffer cmd,
                                              color_t color)
{
  const auto *rc = e->get_component<network::render_component_t>();
  if (!rc || !rc->visible)
    return false;

  const char *mesh_path = rc->mesh_path.length > 0 ? rc->mesh_path.c_str() : nullptr;

  if (!mesh_path)
    return false;

  assets::asset_handle_t<assets::mesh_asset_t> mesh_handle;
  if (std::strncmp(mesh_path, "__primitive_", 12) == 0)
    mesh_handle = assets::get_primitive_mesh(mesh_path + 12);
  else
    mesh_handle = assets::load_mesh(mesh_path);

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
static bool dispatch_selection_wireframe(const network::Entity *e,
                                         overlay_renderer_t &renderer,
                                         color_t color, float grid_step)
{
#define X(ENUM, CLASS, NAME, PATH)                                             \
  if (dynamic_cast<const CLASS *>(e))                                          \
    return Entity_Editor_Traits<CLASS>::draw_selection_wireframe(               \
        static_cast<const CLASS *>(e), renderer, color, grid_step);
  SHARED_ENTITIES_LIST(X)
#undef X
  return false;
}

void draw_selection_highlight(const network::Entity *e,
                              overlay_renderer_t &renderer, float time,
                              float grid_step)
{
  color_t color = compute_pulsating_color(time);

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

void draw_default_ghost(const network::Entity *e, overlay_renderer_t &renderer,
                        const linalg::vec3 &center)
{
  if (const auto *rc = e->get_component<network::render_component_t>())
  {
    const char *mesh_path = rc->mesh_path.length > 0 ? rc->mesh_path.c_str() : nullptr;

    if (mesh_path)
    {
      assets::asset_handle_t<assets::mesh_asset_t> mesh_handle;
      if (std::strncmp(mesh_path, "__primitive_", 12) == 0)
        mesh_handle = assets::get_primitive_mesh(mesh_path + 12);
      else
        mesh_handle = assets::load_mesh(mesh_path);

      if (mesh_handle.valid())
      {
        renderer::draw_mesh(renderer.get_command_buffer(), mesh_handle,
                           {.position  = center,
                            .color     = colors::yellow,
                            .wireframe = true});
        return;
      }
    }
  }

  // Fallback: wire box
  linalg::vec3 he = get_placement_half_extents(e);
  renderer.draw_wire_box(center, he, colors::yellow);
}

// ===================================================================
// Convenience
// ===================================================================

linalg::vec3 compute_placement_center(const network::Entity *e,
                                      const linalg::vec3 &ghost_position)
{
  linalg::vec3 center = ghost_position;
  center.y += get_placement_half_extents(e).y;
  return center;
}

} // namespace client
