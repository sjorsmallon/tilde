#include "placement_tool.hpp"
#include "../../../shared/asset.hpp"
#include "../../../shared/editor_grid.hpp"
#include "../../../shared/entities/static_entities.hpp"
#include "../../../shared/map.hpp" // For factory
#include "../transaction_system.hpp"
#include "entities/player_entity.hpp"
#include "entities/weapon_entity.hpp"
#include "log.hpp"
#include "renderer.hpp"
#include <SDL.h>
#include <cstring>

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
    if (bvh_intersect_ray(*ctx.bvh, view.mouse_ray.origin, view.mouse_ray.dir, hit))
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
    // Place entity so its bottom sits on the ground plane
    if (auto *aabb = dynamic_cast<::network::AABB_Entity *>(new_ent.get()))
      center.y += aabb->half_extents.y;
    else if (auto *wedge = dynamic_cast<::network::Wedge_Entity *>(new_ent.get()))
      center.y += wedge->half_extents.y;
    else if (dynamic_cast<::network::Player_Spawn_Entity *>(new_ent.get()))
      center.y += network::player_half_height;
    else
      center.y += editor::DEFAULT_HALF_EXTENT;

    new_ent->position = center;

    // Entity-specific setup (Player_Spawn_Entity has no render component)

    // Add to map with undo recording
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
      aabb->half_extents = {editor::DEFAULT_HALF_EXTENT,
                            editor::DEFAULT_HALF_EXTENT,
                            editor::DEFAULT_HALF_EXTENT};
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
      wedge->half_extents = {editor::DEFAULT_HALF_EXTENT,
                             editor::DEFAULT_HALF_EXTENT,
                             editor::DEFAULT_HALF_EXTENT};
      wedge->orientation = 0; // Default
    }
  }
  else if (e.scancode == SDL_SCANCODE_3)
  {
    renderer::draw_announcement("Player Spawn");
    current_entity = shared::create_entity_by_classname("player_start");
    // Player_Spawn_Entity has no render fields; ghost is drawn as a hull outline
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
    if (auto *mesh = dynamic_cast<::network::Static_Mesh_Entity *>(current_entity.get()))
    {
       mesh->render.mesh_id = 1;
     }
  }
}

void Placement_Tool::on_draw_overlay(editor_context_t &ctx,
                                     overlay_renderer_t &renderer)
{
  if (ghost_valid && current_entity)
  {
    linalg::vec3 center = ghost_pos;
    if (auto *aabb = dynamic_cast<::network::AABB_Entity *>(current_entity.get()))
      center.y += aabb->half_extents.y;
    else if (auto *wedge = dynamic_cast<::network::Wedge_Entity *>(current_entity.get()))
      center.y += wedge->half_extents.y;
    else if (dynamic_cast<::network::Player_Spawn_Entity *>(current_entity.get()))
      center.y += network::player_half_height;
    else
      center.y += editor::DEFAULT_HALF_EXTENT;

    // Try render component mesh first
    bool drew_mesh = false;
    if (const auto *rc = current_entity->get_component<network::render_component_t>())
    {
      const char *mesh_path = nullptr;

      // Check if mesh_path string is set (for primitives or direct paths)
      if (rc->mesh_path.length > 0)
      {
        mesh_path = rc->mesh_path.c_str();
      }
      // Fallback to mesh_id lookup
      else if (rc->mesh_id >= 0)
      {
        mesh_path = assets::get_mesh_path(rc->mesh_id);
      }

      if (mesh_path)
      {
        // Check if it's a primitive (starts with __primitive_)
        assets::asset_handle_t<assets::mesh_asset_t> mesh_handle;
        if (std::strncmp(mesh_path, "__primitive_", 12) == 0)
        {
          // Extract primitive name (after "__primitive_")
          const char *prim_name = mesh_path + 12;
          mesh_handle = assets::get_primitive_mesh(prim_name);
        }
        else
        {
          // Regular OBJ file
          mesh_handle = assets::load_mesh(mesh_path);
        }

        if (mesh_handle.valid())
        {
          renderer::DrawMeshWireframe(renderer.get_command_buffer(),
              center, {1, 1, 1}, mesh_handle, 0xFF00FFFF);
          drew_mesh = true;
        }
      }
    }

    // Fallback: entity-specific primitives
    if (!drew_mesh)
    {
      if (dynamic_cast<::network::Player_Spawn_Entity *>(current_entity.get()))
      {
        // Draw player hull outline + a short upward spike to mark the spawn
        const linalg::vec3 hull{network::player_half_width, network::player_half_height, network::player_half_width};
        renderer.draw_wire_box(center, hull, 0xFF8800FF);
        renderer.draw_line(center, center + linalg::vec3{0, 48, 0}, 0xFF8800FF);
      }
      else if (auto *aabb =
              dynamic_cast<::network::AABB_Entity *>(current_entity.get()))
      {
        renderer.draw_wire_box(center, aabb->half_extents, 0xFF00FFFF);
      }
      else if (auto *wedge =
                   dynamic_cast<::network::Wedge_Entity *>(current_entity.get()))
      {
        shared::wedge_t ghost_wedge;
        ghost_wedge.center = center;
        ghost_wedge.half_extents = wedge->half_extents;
        ghost_wedge.orientation = wedge->orientation;

        auto points = shared::get_wedge_points(ghost_wedge);

        renderer.draw_line(points[0], points[1], 0xFF00FFFF);
        renderer.draw_line(points[1], points[2], 0xFF00FFFF);
        renderer.draw_line(points[2], points[3], 0xFF00FFFF);
        renderer.draw_line(points[3], points[0], 0xFF00FFFF);

        renderer.draw_line(points[4], points[5], 0xFF00FFFF);

        renderer.draw_line(points[0], points[4], 0xFF00FFFF);
        renderer.draw_line(points[1], points[5], 0xFF00FFFF);
        renderer.draw_line(points[3], points[4], 0xFF00FFFF);
        renderer.draw_line(points[2], points[5], 0xFF00FFFF);
      }
    }
  }
}

} // namespace client
