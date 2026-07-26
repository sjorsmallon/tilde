#include "placement_tool.hpp"
#include "../../../shared/editor_grid.hpp"
#include "../../../shared/map.hpp"
#include "../../../shared/shapes.hpp"
#include "../entity_editor_traits.hpp"
#include "../geometry_editor.hpp"
#include "../transaction_system.hpp"

#define ENTITIES_WANT_INCLUDES
#include "../../../shared/entities/entity_list.hpp"
#undef ENTITIES_WANT_INCLUDES
#include "imgui.h"
#include "log.hpp"
#include "renderer.hpp"

namespace client
{

void Placement_Tool::on_enable(editor_context_t &ctx)
{
  ghost_valid = false;

  // Default to a box brush — index 0 of the placeable table, which is the first
  // geometry kind.
  if (!current_geometry && !current_entity)
    select_placeable(0);
}

void Placement_Tool::on_disable(editor_context_t &ctx) { ghost_valid = false; }

void Placement_Tool::on_update(editor_context_t &ctx,
                               const viewport_state_t &view, float /*dt*/)
{
  float step = ctx.grid ? ctx.grid->step() : editor::MAJOR_GRID_STEP;

  // Prefer hitting existing geometry so we can place entities on walls/ceilings,
  //  but if there's no geometry under the cursor, fall back to the ground plane.
  bool hit_geometry = false;

  if (ctx.bvh && !ctx.bvh->nodes.empty())
  {
    auto hit_result = ray_hit_result_t{};
    if (bvh_intersect_ray(*ctx.bvh, view.mouse_ray.origin, view.mouse_ray.direction,
                          hit_result))
    {
      ghost_position = view.mouse_ray.origin + view.mouse_ray.direction * hit_result.t;
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
    if (linalg::intersect_ray_plane(view.mouse_ray.origin, view.mouse_ray.direction,
                                    plane_point, plane_normal, t))
    {
      ghost_position = view.mouse_ray.origin + view.mouse_ray.direction * t;
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
                                   const input::mouse_event_t &e)
{
  if (e.button != input::mouse_button_t::Left || !ghost_valid || !ctx.map)
    return;

  // Geometry: the prototype IS the new object, so placing one is a copy plus a
  // position. No factory, no property round-trip.
  if (current_geometry)
  {
    shared::geometry_value_t placed = *current_geometry;
    shared::set_position(placed,
                         compute_geometry_placement_center(placed, ghost_position));

    const shared::entity_uid_t uid = ctx.map->add_geometry(placed);

    transaction_builder_t builder;
    builder.add_geometry_created(uid, std::move(placed));
    ctx.transaction_system->push(builder.take());

    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
    return;
  }

  if (current_entity)
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

    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
  }
}

void Placement_Tool::on_mouse_drag(editor_context_t &ctx,
                                   const input::mouse_event_t &e)
{
}

void Placement_Tool::on_mouse_up(editor_context_t &ctx, const input::mouse_event_t &e)
{
}

// What the placement menu offers, in one table across both regimes: the
// geometry kinds first (they're what a level is mostly made of, and the number
// keys should reach them), then every entity type from the X-macro.
struct placeable_t
{
  const char *label;
  bool is_geometry;
  shared::geometry_kind_t geometry_kind; // meaningful when is_geometry
  ::entity_type entity_type;             // meaningful otherwise
};

static const placeable_t g_placeables[] = {
    {"BOX", true, shared::geometry_kind_t::Box, ::entity_type::UNKNOWN},
    {"STATIC_MESH", true, shared::geometry_kind_t::Static_Mesh, ::entity_type::UNKNOWN},
    {"DISPLACEMENT", true, shared::geometry_kind_t::Displacement, ::entity_type::UNKNOWN},
#define X(ENUM, CLASS, NAME, PATH)                                             \
  {#ENUM, false, shared::geometry_kind_t::Box, ::entity_type::ENUM},
    SHARED_ENTITIES_LIST(X)
#undef X
};
static constexpr int g_placeable_count =
    sizeof(g_placeables) / sizeof(g_placeables[0]);

// A geometry prototype at default size, ready to place.
static shared::geometry_value_t
make_placement_prototype(shared::geometry_kind_t kind)
{
  const linalg::vec3 default_extents{editor::DEFAULT_HALF_EXTENT,
                                     editor::DEFAULT_HALF_EXTENT,
                                     editor::DEFAULT_HALF_EXTENT};

  switch (kind)
  {
  case shared::geometry_kind_t::Box:
  {
    shared::box_geometry_t box;
    box.half_extents = default_extents;
    return box;
  }

  case shared::geometry_kind_t::Static_Mesh:
  {
    shared::static_mesh_geometry_t static_mesh;
    static_mesh.surface.mesh_path = "resources/obj/m4a1_s.obj";
    return static_mesh;
  }

  case shared::geometry_kind_t::Displacement:
  {
    shared::displacement_geometry_t displacement;
    displacement.half_extents = default_extents;
    // Sized but flat: the displacement tool picks the face to sculpt.
    displacement.init_grid(shared::box_face_t::Invalid,
                           displacement.subdivision_level);
    return displacement;
  }
  }

  log_error("make_placement_prototype: unhandled geometry kind {} — using a box",
            (int)kind);
  return shared::box_geometry_t{};
}

void Placement_Tool::select_placeable(int index)
{
  if (index < 0 || index >= g_placeable_count)
    return;

  selected_type_index = index;
  const placeable_t &placeable = g_placeables[index];
  renderer::draw_announcement(placeable.label);

  // Engage exactly one regime.
  current_geometry.reset();
  current_entity.reset();

  if (placeable.is_geometry)
  {
    current_geometry = make_placement_prototype(placeable.geometry_kind);
    return;
  }

  current_entity = shared::create_entity_by_type(placeable.entity_type);
  if (!current_entity)
  {
    log_error("select_placeable: no entity registered for \"{}\"", placeable.label);
    return;
  }

  // Set up defaults for entity types that need them. Trigger_Volume is the only
  // box-volume entity left now that geometry has moved out.
  if (shared::box_volume_t *volume = current_entity->get_box_volume())
  {
    volume->half_extents = {editor::DEFAULT_HALF_EXTENT,
                            editor::DEFAULT_HALF_EXTENT,
                            editor::DEFAULT_HALF_EXTENT};
    return;
  }

  if (current_entity->get_type() == ::entity_type::WEAPON)
  {
    auto *weapon = static_cast<network::Weapon_Entity *>(current_entity.get());
    weapon->render.mesh_path.set("resources/obj/m4a1_s.obj");
    weapon->render.is_wireframe = true;
  }
}

void Placement_Tool::on_key_down(editor_context_t &ctx, const key_event_t &e)
{
  for (int idx = 0; idx < g_placeable_count && idx < 9; ++idx)
  {
    int key_index = static_cast<int>(input::key_t::Num_1) + idx;
    if (static_cast<int>(e.key) == key_index)
    {
      select_placeable(idx);
      return;
    }
  }
}

void Placement_Tool::on_draw_ui(editor_context_t &ctx)
{
  ImGui::SetNextWindowSize({200, 0}, ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Placement"))
  {
    // Geometry and entities in one list, separated so it's obvious which is
    // which — they behave differently once placed (undo flavor, save form).
    for (int idx = 0; idx < g_placeable_count; ++idx)
    {
      if (idx > 0 && g_placeables[idx - 1].is_geometry && !g_placeables[idx].is_geometry)
        ImGui::Separator();

      bool selected = (idx == selected_type_index);
      if (ImGui::Selectable(g_placeables[idx].label, selected))
        select_placeable(idx);
    }
  }
  ImGui::End();
}

void Placement_Tool::on_draw_overlay(editor_context_t &ctx,
                                     overlay_renderer_t &renderer)
{
  if (!ghost_valid)
    return;

  if (current_geometry)
  {
    const linalg::vec3 center =
        compute_geometry_placement_center(*current_geometry, ghost_position);
    draw_geometry_ghost(*current_geometry, renderer, center);
    return;
  }

  if (current_entity)
  {
    linalg::vec3 center =
        compute_placement_center(current_entity.get(), ghost_position);

    if (!draw_entity_ghost(current_entity.get(), renderer, center))
      draw_default_ghost(current_entity.get(), renderer, center);
  }
}

} // namespace client
