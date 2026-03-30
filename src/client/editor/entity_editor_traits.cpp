#include "entity_editor_traits.hpp"
#include "../../shared/asset.hpp"
#include "../../shared/editor_grid.hpp"
#include "../../shared/entities/entity_list.hpp"
#include "../../shared/shapes.hpp"
#include "renderer.hpp"
#include <cstring>
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
  return e->half_extents;
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
    uint32_t uid, bool solid)
{
  auto cmd = renderer.get_command_buffer();
  renderer::DrawAABB(cmd, e->position - e->half_extents,
                     e->position + e->half_extents, 0xFFFFFFFF,
                     /*as_wireframe=*/!solid,
                     /*random_color=*/solid,
                     /*random_seed=*/uid);
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

  renderer.draw_line(points[0], points[1], 0xFF00FFFF);
  renderer.draw_line(points[1], points[2], 0xFF00FFFF);
  renderer.draw_line(points[2], points[3], 0xFF00FFFF);
  renderer.draw_line(points[3], points[0], 0xFF00FFFF);
  renderer.draw_line(points[4], points[5], 0xFF00FFFF);
  renderer.draw_line(points[0], points[4], 0xFF00FFFF);
  renderer.draw_line(points[1], points[5], 0xFF00FFFF);
  renderer.draw_line(points[3], points[4], 0xFF00FFFF);
  renderer.draw_line(points[2], points[5], 0xFF00FFFF);
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
  renderer::draw_wedge(renderer.get_command_buffer(), w, 0xFFFFFFFF);
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
  renderer.draw_wire_box(center, hull, 0xFF8800FF);
  renderer.draw_line(center, center + linalg::vec3{0, 48, 0}, 0xFF8800FF);
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
  renderer.draw_wire_box(e->position, hull, 0xFF8800FF);
  renderer.draw_line(e->position, e->position + linalg::vec3{0, 48, 0}, 0xFF8800FF);
  return true;
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
                     center + linalg::vec3{r, 0, 0}, 0xFF00CCFF);
  renderer.draw_line(center + linalg::vec3{0, 0, -r},
                     center + linalg::vec3{0, 0, r}, 0xFF00CCFF);
  renderer.draw_line(center, center + linalg::vec3{0, 32, 0}, 0xFF00CCFF);
  return true;
}

template <>
bool Entity_Editor_Traits<network::Particle_Emitter_Entity>::draw_in_editor(
    const network::Particle_Emitter_Entity *e, overlay_renderer_t &renderer,
    uint32_t, bool)
{
  constexpr float r = 16.f;
  renderer.draw_line(e->position + linalg::vec3{-r, 0, 0},
                     e->position + linalg::vec3{r, 0, 0}, 0xFF00CCFF);
  renderer.draw_line(e->position + linalg::vec3{0, 0, -r},
                     e->position + linalg::vec3{0, 0, r}, 0xFF00CCFF);
  renderer.draw_line(e->position, e->position + linalg::vec3{0, 32, 0}, 0xFF00CCFF);
  return true;
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
                     0xFF00FFFF, /*as_wireframe=*/!solid,
                     /*random_color=*/solid, /*random_seed=*/uid);
  return true;
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
  const char *mesh_path = assets::get_mesh_path(2); // pyramid
  if (mesh_path)
  {
    auto mesh_handle = assets::load_mesh(mesh_path);
    if (mesh_handle.valid())
    {
      renderer::DrawMeshWireframe(renderer.get_command_buffer(),
                                  e->position, {32, 72, 32}, mesh_handle,
                                  0xFFFFFFFF, e->orientation);
      return true;
    }
  }
  return false;
}

// -- Displacement -------------------------------------------------------

template <>
linalg::vec3
Entity_Editor_Traits<network::Displacement_Entity>::get_half_extents(
    const network::Displacement_Entity *e)
{
  return e->half_extents;
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
    renderer::DrawMeshTextured(renderer.get_command_buffer(), e->position,
                               {1, 1, 1}, mesh_handle, e->orientation);
    return true;
  }
  return false;
}

// -- Trigger Volume -----------------------------------------------------

template <>
linalg::vec3
Entity_Editor_Traits<network::Trigger_Volume_Entity>::get_half_extents(
    const network::Trigger_Volume_Entity *e)
{
  return e->half_extents;
}

template <>
bool Entity_Editor_Traits<network::Trigger_Volume_Entity>::draw_ghost(
    const network::Trigger_Volume_Entity *e, overlay_renderer_t &renderer,
    const linalg::vec3 &center)
{
  renderer.draw_wire_box(center, e->half_extents, 0xFF0000FF); // red
  return true;
}

template <>
bool Entity_Editor_Traits<network::Trigger_Volume_Entity>::draw_in_editor(
    const network::Trigger_Volume_Entity *e, overlay_renderer_t &renderer,
    uint32_t, bool)
{
  renderer.draw_wire_box(e->position, e->half_extents, 0xFF0000FF); // red
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

// ===================================================================
// X-macro dispatch: runtime entity* -> compile-time specialization
// ===================================================================

linalg::vec3 get_placement_half_extents(const network::Entity *e)
{
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

  const char *mesh_path = nullptr;
  if (rc->mesh_path.length > 0)
    mesh_path = rc->mesh_path.c_str();
  else if (rc->mesh_id >= 0)
    mesh_path = assets::get_mesh_path(rc->mesh_id);

  if (!mesh_path)
    return false;

  assets::asset_handle_t<assets::mesh_asset_t> mesh_handle;
  if (std::strncmp(mesh_path, "__primitive_", 12) == 0)
    mesh_handle = assets::get_primitive_mesh(mesh_path + 12);
  else
    mesh_handle = assets::load_mesh(mesh_path);

  if (!mesh_handle.valid())
    return false;

  if (rc->is_wireframe)
    renderer::DrawMeshWireframe(cmd, e->position, rc->scale, mesh_handle,
                                0xFFFFFFFF, e->orientation + rc->rotation);
  else
    renderer::DrawMesh(cmd, e->position, rc->scale, mesh_handle,
                       0xFFFFFFFF, e->orientation + rc->rotation);
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
// Default ghost drawing (render component -> wire box fallback)
// ===================================================================

void draw_default_ghost(const network::Entity *e, overlay_renderer_t &renderer,
                        const linalg::vec3 &center)
{
  if (const auto *rc = e->get_component<network::render_component_t>())
  {
    const char *mesh_path = nullptr;

    if (rc->mesh_path.length > 0)
      mesh_path = rc->mesh_path.c_str();
    else if (rc->mesh_id >= 0)
      mesh_path = assets::get_mesh_path(rc->mesh_id);

    if (mesh_path)
    {
      assets::asset_handle_t<assets::mesh_asset_t> mesh_handle;
      if (std::strncmp(mesh_path, "__primitive_", 12) == 0)
        mesh_handle = assets::get_primitive_mesh(mesh_path + 12);
      else
        mesh_handle = assets::load_mesh(mesh_path);

      if (mesh_handle.valid())
      {
        renderer::DrawMeshWireframe(renderer.get_command_buffer(), center,
                                    {1, 1, 1}, mesh_handle, 0xFF00FFFF);
        return;
      }
    }
  }

  // Fallback: wire box
  linalg::vec3 he = get_placement_half_extents(e);
  renderer.draw_wire_box(center, he, 0xFF00FFFF);
}

// ===================================================================
// Convenience
// ===================================================================

linalg::vec3 compute_placement_center(const network::Entity *e,
                                      const linalg::vec3 &ghost_pos)
{
  linalg::vec3 center = ghost_pos;
  center.y += get_placement_half_extents(e).y;
  return center;
}

} // namespace client
