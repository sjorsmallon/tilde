#include "placement_tool.hpp"
#include "../../../shared/editor_grid.hpp"
#include "../../../shared/entities/particle_emitter_entity.hpp"
#include "../../../shared/entities/static_entities.hpp"
#include "../../../shared/map.hpp"
#include "../entity_editor_traits.hpp"
#include "../transaction_system.hpp"
#include "entities/player_entity.hpp"
#include "entities/weapon_entity.hpp"
#include "log.hpp"
#include "renderer.hpp"
#include <SDL.h>

namespace client
{

void Placement_Tool::on_enable(editor_context_t &ctx)
{
  ghost_valid = false;
  if (!current_entity)
  {
    current_entity = shared::create_entity_by_classname("aabb_entity");
    if (auto *aabb =
            dynamic_cast<::network::AABB_Entity *>(current_entity.get()))
    {
      aabb->half_extents = {editor::DEFAULT_HALF_EXTENT,
                            editor::DEFAULT_HALF_EXTENT,
                            editor::DEFAULT_HALF_EXTENT};
    }
  }
}

void Placement_Tool::on_disable(editor_context_t &ctx) { ghost_valid = false; }

void Placement_Tool::on_update(editor_context_t &ctx,
                               const viewport_state_t &view)
{
  float step = ctx.grid ? ctx.grid->step() : editor::MAJOR_GRID_STEP;

  // Prefer hitting existing geometry so entities can be stacked
  bool hit_geometry = false;
  if (ctx.bvh && !ctx.bvh->nodes.empty())
  {
    Ray_Hit hit{};
    if (bvh_intersect_ray(*ctx.bvh, view.mouse_ray.origin, view.mouse_ray.dir,
                          hit))
    {
      ghost_pos = view.mouse_ray.origin + view.mouse_ray.dir * hit.t;
      ghost_pos.x = editor::snap(ghost_pos.x, step);
      ghost_pos.z = editor::snap(ghost_pos.z, step);
      ghost_pos.y = editor::snap(ghost_pos.y, step);
      ghost_valid = true;
      hit_geometry = true;
    }
  }

  // Fallback: intersect with the Y=0 plane
  if (!hit_geometry)
  {
    linalg::vec3 plane_point = {0, 0.0f, 0};
    linalg::vec3 plane_normal = {0, 1.0f, 0};
    float t = 0.0f;
    if (linalg::intersect_ray_plane(view.mouse_ray.origin, view.mouse_ray.dir,
                                    plane_point, plane_normal, t))
    {
      ghost_pos = view.mouse_ray.origin + view.mouse_ray.dir * t;
      ghost_pos.x = editor::snap(ghost_pos.x, step);
      ghost_pos.z = editor::snap(ghost_pos.z, step);
      ghost_valid = true;
    }
    else
    {
      ghost_valid = false;
    }
  }
}

void Placement_Tool::on_mouse_down(editor_context_t &ctx,
                                   const mouse_event_t &e)
{
  if (e.button == 1 && ghost_valid && ctx.map && current_entity)
  {
    std::string classname =
        shared::get_classname_for_entity(current_entity.get());
    auto new_ent = shared::create_entity_by_classname(classname);
    if (!new_ent)
      return;

    new_ent->init_from_map(current_entity->get_all_properties());
    new_ent->position = compute_placement_center(new_ent.get(), ghost_pos);

    {
      Edit_Recorder edit(*ctx.map);
      edit.add(new_ent);
      if (auto txn = edit.take())
        ctx.transaction_system->push(*txn);
    }

    *ctx.geometry_updated = true;
  }
}

void Placement_Tool::on_mouse_drag(editor_context_t &ctx,
                                   const mouse_event_t &e)
{
}

void Placement_Tool::on_mouse_up(editor_context_t &ctx, const mouse_event_t &e)
{
}

void Placement_Tool::on_key_down(editor_context_t &ctx, const key_event_t &e)
{
  renderer::draw_announcement(
      SDL_GetScancodeName(static_cast<SDL_Scancode>(e.scancode)));
  if (e.scancode == SDL_SCANCODE_1)
  {
    renderer::draw_announcement("AABB");
    current_entity = shared::create_entity_by_classname("aabb_entity");
    if (auto *aabb =
            dynamic_cast<::network::AABB_Entity *>(current_entity.get()))
    {
      aabb->half_extents = {editor::DEFAULT_HALF_EXTENT,
                            editor::DEFAULT_HALF_EXTENT,
                            editor::DEFAULT_HALF_EXTENT};
    }
  }
  else if (e.scancode == SDL_SCANCODE_2)
  {
    renderer::draw_announcement("Wedge");
    current_entity = shared::create_entity_by_classname("wedge_entity");
    if (auto *wedge =
            dynamic_cast<::network::Wedge_Entity *>(current_entity.get()))
    {
      wedge->half_extents = {editor::DEFAULT_HALF_EXTENT,
                             editor::DEFAULT_HALF_EXTENT,
                             editor::DEFAULT_HALF_EXTENT};
      wedge->orientation = 0;
    }
  }
  else if (e.scancode == SDL_SCANCODE_3)
  {
    renderer::draw_announcement("Player Spawn");
    current_entity = shared::create_entity_by_classname("player_start");
  }
  else if (e.scancode == SDL_SCANCODE_4)
  {
    renderer::draw_announcement("Weapon");
    current_entity = shared::create_entity_by_classname("weapon_basic");
    if (auto *weapon =
            dynamic_cast<::network::Weapon_Entity *>(current_entity.get()))
    {
      weapon->render.mesh_id = 1;
      weapon->render.is_wireframe = true;
    }
  }
  else if (e.scancode == SDL_SCANCODE_5)
  {
    renderer::draw_announcement("STATIC MESH");
    current_entity = shared::create_entity_by_classname("static_mesh_entity");
    if (auto *mesh = dynamic_cast<::network::Static_Mesh_Entity *>(
            current_entity.get()))
    {
      mesh->render.mesh_id = 1;
    }
  }
  else if (e.scancode == SDL_SCANCODE_6)
  {
    renderer::draw_announcement("PARTICLE EMITTER");
    current_entity = shared::create_entity_by_classname("particle_emitter");
  }
}

void Placement_Tool::on_draw_overlay(editor_context_t &ctx,
                                     overlay_renderer_t &renderer)
{
  if (ghost_valid && current_entity)
  {
    linalg::vec3 center =
        compute_placement_center(current_entity.get(), ghost_pos);

    if (!draw_entity_ghost(current_entity.get(), renderer, center))
      draw_default_ghost(current_entity.get(), renderer, center);
  }
}

} // namespace client
