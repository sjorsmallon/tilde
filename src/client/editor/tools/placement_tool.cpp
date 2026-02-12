#include "placement_tool.hpp"
#include "../../../shared/entities/static_entities.hpp"
#include "../../../shared/map.hpp" // For factory
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
  // Initialize default entity to AABB if not already valid/set
  if (!current_entity)
  {
    current_entity = shared::create_entity_by_classname("aabb_entity");
    if (auto *aabb =
            dynamic_cast<::network::AABB_Entity *>(current_entity.get()))
    {
      aabb->half_extents = {0.5f, 0.5f, 0.5f};
    }
  }
}

void Placement_Tool::on_disable(editor_context_t &ctx) { ghost_valid = false; }

void Placement_Tool::on_update(editor_context_t &ctx,
                               const viewport_state_t &view)
{
  // Raycast against ground plane y = -2.0
  linalg::vec3 plane_point = {0, 0.0f, 0};
  linalg::vec3 plane_normal = {0, 1.0f, 0};

  float t = 0.0f;
  if (linalg::intersect_ray_plane(view.mouse_ray.origin, view.mouse_ray.dir,
                                  plane_point, plane_normal, t))
  {
    ghost_pos = view.mouse_ray.origin + view.mouse_ray.dir * t;

    // Snap to grid (optional, say 1.0 units)
    ghost_pos.x = std::round(ghost_pos.x);
    ghost_pos.z = std::round(ghost_pos.z);

    ghost_valid = true;
  }
  else
  {
    ghost_valid = false;
  }
}

void Placement_Tool::on_mouse_down(editor_context_t &ctx,
                                   const mouse_event_t &e)
{
  if (e.button == 1 && ghost_valid && ctx.map && current_entity)
  {
    // Create a copy of the current template
    std::string classname =
        shared::get_classname_for_entity(current_entity.get());
    auto new_ent = shared::create_entity_by_classname(classname);
    if (!new_ent)
      return;

    // Init properties from current template
    new_ent->init_from_map(current_entity->get_all_properties());

    // Update position based on ghost
    // Center needs to be adjusted so the object sits ON the plane
    // For AABB and Wedge, center.y should be plane_y + half_extents.y
    // Assuming 1x1x1 size for now (half_extents = 0.5)

    linalg::vec3 center = ghost_pos;
    center.y += 0.5f;

    // Update the properties
    if (auto *aabb = dynamic_cast<::network::AABB_Entity *>(new_ent.get()))
    {
      aabb->center = center;
    }
    else if (auto *wedge =
                 dynamic_cast<::network::Wedge_Entity *>(new_ent.get()))
    {
      wedge->center = center;
    }
    else if (auto *player =
                 dynamic_cast<::network::Player_Entity *>(new_ent.get()))
    {
      player->position = center;
    }
    else if (auto *mesh =
                 dynamic_cast<::network::Static_Mesh_Entity *>(new_ent.get()))
    {
      mesh->position = center;
    }

    // 3. Add to map wrapped in entity_placement_t
    {
      // We need to ensure the transaction object lives until after the
      // push_back.
      client::Editor_Transaction transaction(ctx.transaction_system, ctx.map,
                                             "Place Object");

      shared::entity_placement_t placement;
      placement.entity = new_ent;
      placement.position = center;
      placement.scale = {1, 1, 1}; // Default scale
      placement.rotation = {0, 0, 0}; // Default rotation

      // For Static_Mesh_Entity, we might want to use asset-specific scale
      if (auto *mesh =
              dynamic_cast<::network::Static_Mesh_Entity *>(new_ent.get()))
      {
        // Could load default scale from asset here
        placement.scale = {1, 1, 1};
      }

      ctx.map->entities.push_back(placement);
    } // Transaction commits here.

    // 4. Trigger update (because we are pushing back geometry?)
    *ctx.geometry_updated = true;
  }
}

void Placement_Tool::on_mouse_drag(editor_context_t &ctx,
                                   const mouse_event_t &e)
{
  // Perhaps paint placement?
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
    // Switch to AABB
    current_entity = shared::create_entity_by_classname("aabb_entity");
    if (auto *aabb =
            dynamic_cast<::network::AABB_Entity *>(current_entity.get()))
    {
      aabb->half_extents = {0.5f, 0.5f, 0.5f};
    }
  }
  else if (e.scancode == SDL_SCANCODE_2)
  {
    renderer::draw_announcement("Wedge");
    // Switch to Wedge
    current_entity = shared::create_entity_by_classname("wedge_entity");
    if (auto *wedge =
            dynamic_cast<::network::Wedge_Entity *>(current_entity.get()))
    {
      wedge->half_extents = {0.5f, 0.5f, 0.5f};
      wedge->orientation = 0; // Default
    }
  }
  else if (e.scancode == SDL_SCANCODE_3)
  {
    renderer::draw_announcement("Player");
    // Switch to Cylinder
    current_entity = shared::create_entity_by_classname("player_start");
    if (auto *player =
            dynamic_cast<::network::Player_Entity *>(current_entity.get()))
    {
      player->health = 100;
    }
  }
  else if (e.scancode == SDL_SCANCODE_4)
  {
    renderer::draw_announcement("Weapon");
    current_entity = shared::create_entity_by_classname("weapon_basic");
    if (auto *weapon =
            dynamic_cast<::network::Weapon_Entity *>(current_entity.get()))
    {
    }
  }
  else if (e.scancode == SDL_SCANCODE_5)
  {
    renderer::draw_announcement("STATIC MESH");
    current_entity = shared::create_entity_by_classname("static_mesh_entity");
    if (auto *mesh = dynamic_cast<::network::Static_Mesh_Entity *>(current_entity.get()))
    {
       mesh->asset_id = 1;
     }
  }
}

void Placement_Tool::on_draw_overlay(editor_context_t &ctx,
                                     overlay_renderer_t &renderer)
{
  if (ghost_valid && current_entity)
  {
    linalg::vec3 center = ghost_pos;
    center.y += 0.5f;

    // Draw based on current selection type
    if (auto *aabb =
            dynamic_cast<::network::AABB_Entity *>(current_entity.get()))
    {
      renderer.draw_wire_box(center, aabb->half_extents,
                             0xFF00FFFF); // Magenta ghost
    }
    else if (auto *wedge =
                 dynamic_cast<::network::Wedge_Entity *>(current_entity.get()))
    {
      // Create a temporary wedge at the ghost position for drawing
      // But wait, shared::shapes methods take `wedge_t`.
      // Helper:
      shared::wedge_t ghost_wedge;
      ghost_wedge.center = center;
      ghost_wedge.half_extents = wedge->half_extents;
      ghost_wedge.orientation = wedge->orientation;

      auto points = shared::get_wedge_points(ghost_wedge);

      // Draw wireframe for wedge manually since renderer might not support it
      // directly
      // Points are: 0,1,2,3 (Base), 4,5 (Top Edge)
      // Base: 0-1, 1-2, 2-3, 3-0
      renderer.draw_line(points[0], points[1], 0xFF00FFFF);
      renderer.draw_line(points[1], points[2], 0xFF00FFFF);
      renderer.draw_line(points[2], points[3], 0xFF00FFFF);
      renderer.draw_line(points[3], points[0], 0xFF00FFFF);

      // Top Edge: 4-5
      renderer.draw_line(points[4], points[5], 0xFF00FFFF);

      renderer.draw_line(points[0], points[4], 0xFF00FFFF);
      renderer.draw_line(points[1], points[5], 0xFF00FFFF);
      renderer.draw_line(points[3], points[4], 0xFF00FFFF);
      renderer.draw_line(points[2], points[5], 0xFF00FFFF);
    }
    else if (auto *player =
                 dynamic_cast<::network::Player_Entity *>(current_entity.get()))
    {
      // Pyramid for player start!
      // Base:
      linalg::vec3 p0 = {center.x - 0.5f, center.y - 0.5f, center.z - 0.5f};
      linalg::vec3 p1 = {center.x + 0.5f, center.y - 0.5f, center.z - 0.5f};
      linalg::vec3 p2 = {center.x + 0.5f, center.y - 0.5f, center.z + 0.5f};
      linalg::vec3 p3 = {center.x - 0.5f, center.y - 0.5f, center.z + 0.5f};

      // Top:
      linalg::vec3 p4 = {center.x, center.y + 0.5f, center.z};

      renderer.draw_line(p0, p1, 0xFFFFFFFF);
      renderer.draw_line(p1, p2, 0xFFFFFFFF);
      renderer.draw_line(p2, p3, 0xFFFFFFFF);
      renderer.draw_line(p3, p0, 0xFFFFFFFF);

      renderer.draw_line(p0, p4, 0xFFFFFFFF);
      renderer.draw_line(p1, p4, 0xFFFFFFFF);
      renderer.draw_line(p2, p4, 0xFFFFFFFF);
      renderer.draw_line(p3, p4, 0xFFFFFFFF);
    }
  }
}

} // namespace client
