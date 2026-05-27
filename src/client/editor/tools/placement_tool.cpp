#include "placement_tool.hpp"
#include "../../../shared/editor_grid.hpp"
#include "../../../shared/map.hpp"
#include "../../../shared/shapes.hpp"
#include "../entity_editor_traits.hpp"
#include "../transaction_system.hpp"

#define ENTITIES_WANT_INCLUDES
#include "../../../shared/entities/entity_list.hpp"
#undef ENTITIES_WANT_INCLUDES
#include "imgui.h"
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
    current_entity = std::make_shared<network::AABB_Entity>();
    if (shared::box_volume_t *volume = current_entity->get_box_volume())
    {
      volume->half_extents = {editor::DEFAULT_HALF_EXTENT,
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

  // Prefer hitting existing geometry so we can place entities on walls/ceilings,
  //  but if there's no geometry under the cursor, fall back to the ground plane.
  bool hit_geometry = false;

  if (ctx.bvh && !ctx.bvh->nodes.empty())
  {
    auto hit_result = ray_hit_result_t{};
    if (bvh_intersect_ray(*ctx.bvh, view.mouse_ray.origin, view.mouse_ray.dir,
                          hit_result))
    {
      ghost_position = view.mouse_ray.origin + view.mouse_ray.dir * hit_result.t;
      ghost_position.x = editor::snap(ghost_position.x, step);
      ghost_position.z = editor::snap(ghost_position.z, step);
      ghost_position.y = editor::snap(ghost_position.y, step);
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
      ghost_position = view.mouse_ray.origin + view.mouse_ray.dir * t;
      ghost_position.x = editor::snap(ghost_position.x, step);
      ghost_position.z = editor::snap(ghost_position.z, step);
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
  if (e.button == mouse_button::MOUSE_BUTTON_LEFT && ghost_valid && ctx.map && current_entity)
  {
    auto new_entity = shared::create_entity_by_type(current_entity->get_type());
    if (!new_entity)
      return;

    new_entity->init_from_map(current_entity->get_all_properties());
    new_entity->position = compute_placement_center(new_entity.get(), ghost_position);

    {
      auto uid = ctx.map->add_entity(new_entity);
      transaction_builder_t builder;
      builder.add_created(uid, snapshot_entity(new_entity.get()));
      ctx.transaction_system->push(builder.take());
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

// Entity type table for placement — auto-generated from the X-macro.
struct placeable_type_t
{
  const char *label;
  ::entity_type type;
};

static const placeable_type_t g_placeable_types[] = {
#define X(ENUM, CLASS, NAME, PATH) {#ENUM, ::entity_type::ENUM},
    SHARED_ENTITIES_LIST(X)
#undef X
};
static constexpr int g_placeable_count =
    sizeof(g_placeable_types) / sizeof(g_placeable_types[0]);

void Placement_Tool::select_entity_type(int index)
{
  if (index < 0 || index >= g_placeable_count)
    return;
  selected_type_index = index;
  const auto &type = g_placeable_types[index];
  renderer::draw_announcement(type.label);
  current_entity = shared::create_entity_by_type(type.type);
  if (!current_entity)
    return;

  // Set up defaults for types that need them.
  // Box-volume entities (AABB, Displacement, Trigger_Volume, ...) all flow
  // through the same path: default extents into their owned box_volume_t.
  if (shared::box_volume_t *volume = current_entity->get_box_volume())
  {
    volume->half_extents = {editor::DEFAULT_HALF_EXTENT,
                            editor::DEFAULT_HALF_EXTENT,
                            editor::DEFAULT_HALF_EXTENT};
    return;
  }

  switch (current_entity->get_type())
  {
  case ::entity_type::WEDGE:
  {
    auto *wedge = static_cast<network::Wedge_Entity *>(current_entity.get());
    wedge->half_extents = {editor::DEFAULT_HALF_EXTENT,
                           editor::DEFAULT_HALF_EXTENT,
                           editor::DEFAULT_HALF_EXTENT};
    wedge->orientation = 0;
    break;
  }
  case ::entity_type::WEAPON:
  {
    auto *weapon = static_cast<network::Weapon_Entity *>(current_entity.get());
    weapon->render.mesh_path.set("resources/obj/m4a1_s.obj");
    weapon->render.is_wireframe = true;
    break;
  }
  case ::entity_type::STATIC_MESH:
  {
    auto *mesh = static_cast<network::Static_Mesh_Entity *>(current_entity.get());
    mesh->render.mesh_path.set("resources/obj/m4a1_s.obj");
    break;
  }
  default:
    break;
  }
}

void Placement_Tool::on_key_down(editor_context_t &ctx, const key_event_t &e)
{
  for (int idx = 0; idx < g_placeable_count && idx < 9; ++idx)
  {
    if (e.scancode == SDL_SCANCODE_1 + idx)
    {
      select_entity_type(idx);
      return;
    }
  }
}

void Placement_Tool::on_draw_ui(editor_context_t &ctx)
{
  ImGui::SetNextWindowSize({200, 0}, ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Placement"))
  {
    for (int idx = 0; idx < g_placeable_count; ++idx)
    {
      bool selected = (idx == selected_type_index);
      if (ImGui::Selectable(g_placeable_types[idx].label, selected))
        select_entity_type(idx);
    }
  }
  ImGui::End();
}

void Placement_Tool::on_draw_overlay(editor_context_t &ctx,
                                     overlay_renderer_t &renderer)
{
  if (ghost_valid && current_entity)
  {
    linalg::vec3 center =
        compute_placement_center(current_entity.get(), ghost_position);

    if (!draw_entity_ghost(current_entity.get(), renderer, center))
      draw_default_ghost(current_entity.get(), renderer, center);
  }
}

} // namespace client
