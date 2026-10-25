#include "subtract_tool.hpp"

#include "../../../shared/collision/collision_detection.hpp"
#include "../../../shared/log.hpp"
#include "../../../shared/map/map_geometry.hpp"
#include "../../../shared/map/map_geometry_mesh.hpp"
#include "../../../shared/map/map_group.hpp"
#include "../../hud.hpp"
#include "../transaction_system.hpp"
#include "imgui.h"

#include <algorithm>
#include <format>
#include <utility>
#include <variant>

namespace client
{

namespace
{

constexpr color_t WALL_FILL{80, 160, 255, 50};
constexpr color_t CUTTER_FILL{255, 150, 40, 70};
constexpr color_t CUTTER_OUTLINE{255, 150, 40, 255};
constexpr color_t RESULT_FILL{80, 220, 120, 70};
constexpr color_t HOLE_OUTLINE{255, 255, 255, 255};

std::optional<shared::entity_uid_t> try_pick_geometry(const editor_context_t &ctx,
                                                      const viewport_state_t &view)
{
  if (!ctx.bvh || ctx.bvh->nodes.empty())
    return std::nullopt;
  ray_hit_result_t hit{};
  if (!bvh_intersect_ray(*ctx.bvh, view.mouse_ray.origin, view.mouse_ray.direction, hit))
    return std::nullopt;
  if (hit.id.type != Collision_Id::Type::Static_Geometry)
    return std::nullopt;
  return shared::entity_uid_t(hit.id.index);
}

const shared::map_geometry_t *try_find_entry(const editor_context_t &ctx,
                                             shared::entity_uid_t uid)
{
  return ctx.map && uid != shared::null_entity_uid ? ctx.map->find_geometry_by_uid(uid) : nullptr;
}

const shared::mesh_geometry_t *try_find_mesh(const editor_context_t &ctx,
                                             shared::entity_uid_t uid)
{
  const shared::map_geometry_t *entry = try_find_entry(ctx, uid);
  return entry ? &entry->value : nullptr;
}

void draw_face_tinted(pass_builder_t &draws, const shared::mesh_geometry_t &mesh,
                      uint32_t face_index, color_t fill, const color_t *outline)
{
  const shared::mesh_face_t &face = mesh.faces[face_index];
  std::vector<linalg::vec3f> polygon;
  polygon.reserve(face.vertex_indices.size());
  for (uint32_t vertex : face.vertex_indices)
    polygon.push_back(shared::compute_mesh_world_vertex(mesh, vertex));
  draws.debug.filled_polygon(polygon, fill, 0.f, {.depth_bias = 0.5f});
  if (!outline)
    return;
  for (size_t i = 0; i < polygon.size(); ++i)
    draws.debug.line(polygon[i], polygon[(i + 1) % polygon.size()], *outline);
}

void draw_mesh_tinted(pass_builder_t &draws, const shared::mesh_geometry_t &mesh, color_t fill,
                      const color_t *outline)
{
  for (uint32_t face_index = 0; face_index < (uint32_t)mesh.faces.size(); ++face_index)
    draw_face_tinted(draws, mesh, face_index, fill, outline);
}

} // namespace

void Subtract_Tool::on_enable(editor_context_t &) { clear(); }

void Subtract_Tool::on_disable(editor_context_t &) { clear(); }

void Subtract_Tool::clear()
{
  wall_uid = shared::null_entity_uid;
  cutter_uid = shared::null_entity_uid;
  preview.reset();
  previewed_wall.reset();
  previewed_cutter.reset();
}

void Subtract_Tool::on_update(editor_context_t &ctx, const viewport_state_t &view, float)
{
  cached_view = view;
  if (wall_uid == shared::null_entity_uid)
    return;

  const shared::map_geometry_t *wall = try_find_entry(ctx, wall_uid);
  if (!wall)
  {
    hud::set_announcement("subtract: the wall is gone");
    clear();
    return;
  }
  if (cutter_uid == shared::null_entity_uid)
    return;

  const shared::map_geometry_t *cutter = try_find_entry(ctx, cutter_uid);
  if (!cutter)
  {
    hud::set_announcement("subtract: the cutter is gone");
    cutter_uid = shared::null_entity_uid;
    preview.reset();
    previewed_cutter.reset();
    return;
  }

  const bool wall_changed =
      previewed_wall && !shared::geometry_values_equal(*previewed_wall, wall->value);
  const bool cutter_changed =
      previewed_cutter && !shared::geometry_values_equal(*previewed_cutter, cutter->value);
  if (wall_changed || cutter_changed)
    rebuild_preview(ctx);
}

void Subtract_Tool::rebuild_preview(editor_context_t &ctx)
{
  preview.reset();
  previewed_wall.reset();
  previewed_cutter.reset();

  const shared::map_geometry_t *wall_entry = try_find_entry(ctx, wall_uid);
  const shared::map_geometry_t *cutter_entry = try_find_entry(ctx, cutter_uid);
  const shared::mesh_geometry_t *wall =
      wall_entry ? &wall_entry->value : nullptr;
  const shared::mesh_geometry_t *cutter =
      cutter_entry ? &cutter_entry->value : nullptr;
  if (!wall || !cutter)
    return;
  previewed_wall = wall_entry->value;
  previewed_cutter = cutter_entry->value;

  preview = shared::try_subtract_convex_mesh(*wall, *cutter);
  if (!preview)
    hud::set_announcement("subtract refused: see the log");
  else if (preview->missed)
    hud::set_announcement(std::format("subtract: mesh {} misses mesh {}", cutter_uid, wall_uid));
  else if (preview->result.faces.empty())
    hud::set_announcement(
        std::format("subtract: mesh {} swallows mesh {} - Enter removes both", cutter_uid, wall_uid));
  else
    hud::set_announcement(std::format("subtract: {} faces, {} on the cutter - Enter commits",
                                      preview->result.faces.size(),
                                      preview->result.faces.size() -
                                          std::count(preview->cutter_faces.begin(),
                                                     preview->cutter_faces.end(),
                                                     shared::MESH_SUBTRACTION_WALL_FACE)));
}

void Subtract_Tool::on_mouse_down(editor_context_t &ctx, const input::mouse_event_t &e)
{
  if (e.button == input::mouse_button_t::Right)
  {
    if (cutter_uid != shared::null_entity_uid)
    {
      cutter_uid = shared::null_entity_uid;
      preview.reset();
      previewed_cutter.reset();
    }
    else
      clear();
    return;
  }

  if (e.button != input::mouse_button_t::Left)
    return;

  const std::optional<shared::entity_uid_t> picked = try_pick_geometry(ctx, cached_view);
  if (!picked)
    return;
  const shared::mesh_geometry_t *mesh = try_find_mesh(ctx, *picked);
  if (!mesh)
  {
    hud::set_announcement("subtract: click a mesh");
    return;
  }
  if (std::holds_alternative<shared::asset_mesh_source_t>(mesh->source))
  {
    hud::set_announcement("subtract: an asset-sourced mesh has no faces of its own to cut");
    return;
  }

  if (wall_uid == shared::null_entity_uid || cutter_uid != shared::null_entity_uid)
  {
    clear();
    wall_uid = *picked;
    hud::set_announcement(std::format("subtract: wall is mesh {} - now click the cutter", wall_uid));
    return;
  }
  if (*picked == wall_uid)
  {
    hud::set_announcement("subtract: the cutter must be another mesh");
    return;
  }
  cutter_uid = *picked;
  rebuild_preview(ctx);
}

void Subtract_Tool::on_mouse_drag(editor_context_t &, const input::mouse_event_t &) {}

void Subtract_Tool::on_mouse_up(editor_context_t &, const input::mouse_event_t &) {}

void Subtract_Tool::on_key_down(editor_context_t &ctx, const key_event_t &e)
{
  switch (e.key)
  {
  case input::key_t::Tab:
    if (cutter_uid != shared::null_entity_uid)
    {
      std::swap(wall_uid, cutter_uid);
      rebuild_preview(ctx);
    }
    return;
  case input::key_t::Enter:
    commit(ctx);
    return;
  case input::key_t::Escape:
    clear();
    return;
  default:
    return;
  }
}

void Subtract_Tool::commit(editor_context_t &ctx)
{
  if (!preview || preview->missed)
  {
    hud::set_announcement("subtract: nothing to commit");
    return;
  }

  const shared::map_geometry_t *wall_entry = try_find_entry(ctx, wall_uid);
  const shared::map_geometry_t *cutter_entry = try_find_entry(ctx, cutter_uid);
  if (!wall_entry || !cutter_entry)
  {
    log_error("subtract: mesh {} or {} vanished before the commit", wall_uid, cutter_uid);
    clear();
    return;
  }

  transaction_t transaction;
  std::vector<shared::map_group_t> groups_before = ctx.map->groups;
  const shared::mesh_geometry_t removed_wall = wall_entry->value;
  const shared::mesh_geometry_t removed_cutter = cutter_entry->value;
  ctx.map->remove_geometry(wall_uid);
  ctx.map->remove_geometry(cutter_uid);
  transaction.add_geometry_removed(wall_uid, removed_wall);
  transaction.add_geometry_removed(cutter_uid, removed_cutter);

  // A cut that leaves the wall in two arms is two objects, as the clip's is.
  size_t placed = 0;
  for (const shared::mesh_geometry_t &shell : shared::separate_mesh_shells(preview->result))
  {
    const shared::entity_uid_t uid = ctx.map->add_geometry(shell);
    transaction.add_geometry_created(uid, shell);
    ++placed;
  }

  transaction.add_map_groups_modified(std::move(groups_before), ctx.map->groups);
  ctx.transaction_system.push("Subtract mesh", std::move(transaction));
  if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
  hud::set_announcement(placed == 0 ? std::string("subtract: both meshes removed")
                                    : std::format("subtract: {} mesh(es) placed", placed));
  clear();
}

void Subtract_Tool::on_draw_overlay(editor_context_t &ctx, pass_builder_t &draws)
{
  const shared::mesh_geometry_t *wall = try_find_mesh(ctx, wall_uid);
  const shared::mesh_geometry_t *cutter = try_find_mesh(ctx, cutter_uid);
  if (!wall)
    return;

  if (preview && !preview->missed)
  {
    for (uint32_t face_index = 0; face_index < (uint32_t)preview->result.faces.size(); ++face_index)
      draw_face_tinted(draws, preview->result, face_index, RESULT_FILL,
                       preview->cutter_faces[face_index] == shared::MESH_SUBTRACTION_WALL_FACE
                           ? nullptr
                           : &HOLE_OUTLINE);
  }
  else
    draw_mesh_tinted(draws, *wall, WALL_FILL, nullptr);

  if (cutter)
    draw_mesh_tinted(draws, *cutter, CUTTER_FILL, &CUTTER_OUTLINE);
}

void Subtract_Tool::on_draw_inspector(editor_context_t &)
{
  ImGui::TextUnformatted("Click the wall, then the cutter: a convex");
  ImGui::TextUnformatted("mesh. The wall less the cutter is previewed,");
  ImGui::TextUnformatted("its faces on the cutter outlined white.");
  ImGui::Separator();
  if (wall_uid == shared::null_entity_uid)
    ImGui::TextUnformatted("Wall: none");
  else
    ImGui::Text("Wall: mesh %u", wall_uid);
  if (cutter_uid == shared::null_entity_uid)
    ImGui::TextUnformatted("Cutter: none");
  else
    ImGui::Text("Cutter: mesh %u  (Tab swaps)", cutter_uid);
  if (!preview)
    ImGui::TextUnformatted(cutter_uid == shared::null_entity_uid ? "No cut yet" : "Refused: see the log");
  else if (preview->missed)
    ImGui::TextUnformatted("The cutter misses the wall");
  else if (preview->result.faces.empty())
    ImGui::TextUnformatted("The cutter swallows the wall: Enter removes both");
  else
    ImGui::Text("%zu faces; Enter commits, the cutter is consumed",
                preview->result.faces.size());
  ImGui::TextUnformatted("Right click steps back, Escape clears");
}

} // namespace client
