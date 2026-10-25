#include "clip_tool.hpp"

#include "../../../shared/collision/collision_detection.hpp"
#include "../../../shared/log.hpp"
#include "../../../shared/map/map_geometry.hpp"
#include "../../../shared/map/map_geometry_mesh.hpp"
#include "../../../shared/map/map_group.hpp"
#include "../../camera.hpp"
#include "../../hud.hpp"
#include "../transaction_system.hpp"
#include "imgui.h"

#include <cmath>
#include <variant>

namespace client
{

namespace
{

constexpr color_t KEPT_FRONT_FILL{80, 220, 120, 70};
constexpr color_t KEPT_BACK_FILL{80, 160, 255, 70};
constexpr color_t DISCARDED_FILL{230, 60, 60, 40};
constexpr color_t CAP_OUTLINE{255, 255, 255, 255};
constexpr color_t POINT_COLOR{255, 220, 60, 255};
constexpr color_t REFUSED_POINT_COLOR{255, 40, 40, 255};
constexpr linalg::vec3 POINT_HALF_EXTENTS{2.f, 2.f, 2.f};

struct surface_pick_t
{
  shared::entity_uid_t uid = shared::null_entity_uid;
  linalg::vec3 point;
};

std::optional<surface_pick_t> try_pick_surface(const editor_context_t &ctx,
                                               const viewport_state_t &view)
{
  if (!ctx.bvh || ctx.bvh->nodes.empty())
    return std::nullopt;

  ray_hit_result_t hit{};
  if (!bvh_intersect_ray(*ctx.bvh, view.mouse_ray.origin, view.mouse_ray.direction, hit))
    return std::nullopt;

  const float step = ctx.grid ? ctx.grid->step() : editor::MAJOR_GRID_STEP;
  const linalg::vec3 point = view.mouse_ray.origin + view.mouse_ray.direction * hit.t;
  surface_pick_t pick;
  pick.uid = hit.id.type == Collision_Id::Type::Static_Geometry
                 ? shared::entity_uid_t(hit.id.index)
                 : shared::null_entity_uid;
  pick.point = {editor::snap(point.x, step), editor::snap(point.y, step),
                editor::snap(point.z, step)};
  return pick;
}

std::optional<Plane> try_plane_through(Span<const linalg::vec3> points,
                                       const linalg::vec3 &view_forward)
{
  if (points.count < 2)
    return std::nullopt;

  const linalg::vec3 along = points[1] - points[0];
  const linalg::vec3 across = points.count >= 3 ? points[2] - points[0] : view_forward;
  const linalg::vec3 normal = linalg::cross(along, across);
  const float length = linalg::length(normal);
  if (length < 1e-3f)
    return std::nullopt;
  return Plane{points[0], normal * (1.f / length)};
}

const char *keep_label(int keep)
{
  switch (keep)
  {
  case 0: return "both";
  case 1: return "front";
  default: return "back";
  }
}

const shared::mesh_geometry_t *try_find_mesh(const editor_context_t &ctx,
                                             shared::entity_uid_t uid)
{
  const shared::map_geometry_t *entry = ctx.map ? ctx.map->find_geometry_by_uid(uid) : nullptr;
  return entry ? &entry->value : nullptr;
}

// Every face through the pose, the caps (the last `cap_face_count`) outlined.
void draw_half_tinted(pass_builder_t &draws, const shared::mesh_geometry_t &half, color_t fill,
                      uint32_t cap_face_count)
{
  const size_t first_cap = half.faces.size() - std::min<size_t>(cap_face_count, half.faces.size());
  for (size_t face_index = 0; face_index < half.faces.size(); ++face_index)
  {
    const shared::mesh_face_t &face = half.faces[face_index];
    std::vector<linalg::vec3f> polygon;
    polygon.reserve(face.vertex_indices.size());
    for (uint32_t vertex : face.vertex_indices)
      polygon.push_back(shared::compute_mesh_world_vertex(half, vertex));
    draws.debug.filled_polygon(polygon, fill, 0.f, {.depth_bias = 0.5f});

    if (face_index < first_cap)
      continue;
    for (size_t i = 0; i < polygon.size(); ++i)
      draws.debug.line(polygon[i], polygon[(i + 1) % polygon.size()], CAP_OUTLINE);
  }
}

} // namespace

void Clip_Tool::on_enable(editor_context_t &) { clear(); }

void Clip_Tool::on_disable(editor_context_t &) { clear(); }

void Clip_Tool::clear()
{
  target_uid = shared::null_entity_uid;
  cap_face_index.reset();
  points.clear();
  cut_plane.reset();
  preview.reset();
  previewed_value.reset();
}

void Clip_Tool::on_update(editor_context_t &ctx, const viewport_state_t &view, float)
{
  cached_view = view;
  if (target_uid == shared::null_entity_uid)
    return;

  const shared::map_geometry_t *entry = ctx.map->find_geometry_by_uid(target_uid);
  if (!entry)
  {
    hud::set_announcement("clip: the mesh is gone");
    clear();
    return;
  }

  if (previewed_value && !shared::geometry_values_equal(*previewed_value, entry->value))
    rebuild_preview(ctx);
}

void Clip_Tool::rebuild_cut_plane()
{
  const linalg::vec3 forward = get_orientation_vectors(cached_view.camera).forward;
  cut_plane = try_plane_through(Span<const linalg::vec3>(points.data(), points.size()), forward);
}

void Clip_Tool::add_point(editor_context_t &ctx, const linalg::vec3 &point)
{
  points.push_back(point);
  rebuild_cut_plane();
  rebuild_preview(ctx);
}

void Clip_Tool::rebuild_preview(editor_context_t &ctx)
{
  preview.reset();
  previewed_value.reset();
  if (!cut_plane || target_uid == shared::null_entity_uid)
    return;

  const shared::map_geometry_t *entry = ctx.map->find_geometry_by_uid(target_uid);
  const shared::mesh_geometry_t *mesh =
      entry ? &entry->value : nullptr;
  if (!mesh)
    return;
  previewed_value = entry->value;

  const Plane local_plane = shared::compute_mesh_local_plane(*mesh, *cut_plane);
  if (shared::count_subdivided_faces_crossed_by_plane(*mesh, local_plane) > 0)
  {
    hud::set_announcement("clip refused: the cut crosses a subdivided face - flatten it first");
    return;
  }

  // The caps take the clicked face's material half with a fresh projection
  // for their own normal; a grid is the clicked face's shape, never the cap's.
  shared::face_surface_t cap_surface;
  if (cap_face_index && *cap_face_index < mesh->faces.size())
    cap_surface = mesh->faces[*cap_face_index].surface;
  shared::resize_face_grid(cap_surface, 0);
  cap_surface.uv = shared::default_face_uv(cut_plane->normal);

  preview = shared::try_split_mesh_by_plane(*mesh, local_plane, cap_surface);
  if (!preview)
    hud::set_announcement("clip refused: see the log");
}

void Clip_Tool::on_mouse_down(editor_context_t &ctx, const input::mouse_event_t &e)
{
  if (e.button == input::mouse_button_t::Right)
  {
    if (points.size() <= 1)
    {
      clear();
      return;
    }
    points.pop_back();
    rebuild_cut_plane();
    rebuild_preview(ctx);
    return;
  }

  if (e.button != input::mouse_button_t::Left)
    return;

  const std::optional<surface_pick_t> pick = try_pick_surface(ctx, cached_view);
  if (!pick)
    return;

  if (target_uid == shared::null_entity_uid || points.size() >= 3)
  {
    const shared::mesh_geometry_t *mesh = try_find_mesh(ctx, pick->uid);
    if (!mesh)
    {
      hud::set_announcement("clip: the first point must be on a mesh");
      return;
    }
    if (std::holds_alternative<shared::asset_mesh_source_t>(mesh->source))
    {
      hud::set_announcement("clip: an asset-sourced mesh has no vertices of its own to cut");
      return;
    }
    clear();
    target_uid = pick->uid;
    if (const std::optional<shared::mesh_face_hit_t> face_hit = shared::try_pick_mesh_face(
            *mesh, cached_view.mouse_ray.origin, cached_view.mouse_ray.direction))
      cap_face_index = face_hit->face_index;
  }

  add_point(ctx, pick->point);
}

void Clip_Tool::on_mouse_drag(editor_context_t &, const input::mouse_event_t &) {}

void Clip_Tool::on_mouse_up(editor_context_t &, const input::mouse_event_t &) {}

void Clip_Tool::on_key_down(editor_context_t &ctx, const key_event_t &e)
{
  switch (e.key)
  {
  case input::key_t::Tab:
    keep = keep == Keep::Both ? Keep::Front : keep == Keep::Front ? Keep::Back : Keep::Both;
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

void Clip_Tool::commit(editor_context_t &ctx)
{
  if (!preview)
  {
    hud::set_announcement("clip: nothing to commit");
    return;
  }

  const shared::map_geometry_t *entry = ctx.map->find_geometry_by_uid(target_uid);
  if (!entry)
  {
    log_error("clip: mesh {} vanished before the commit", target_uid);
    clear();
    return;
  }

  transaction_t transaction;
  const shared::mesh_geometry_t removed = entry->value;
  std::vector<shared::map_group_t> groups_before = ctx.map->groups;
  ctx.map->remove_geometry(target_uid);
  transaction.add_geometry_removed(target_uid, removed);

  // A half the cut left in two arms is two objects: what a half IS, not how
  // the split happened to number it.
  const auto place_half = [&](const shared::mesh_geometry_t &half)
  {
    for (const shared::mesh_geometry_t &shell : shared::separate_mesh_shells(half))
    {
      const shared::entity_uid_t uid = ctx.map->add_geometry(shell);
      transaction.add_geometry_created(uid, shell);
    }
  };
  if (keep != Keep::Back)
    place_half(preview->front);
  if (keep != Keep::Front)
    place_half(preview->back);

  transaction.add_map_groups_modified(std::move(groups_before), ctx.map->groups);
  ctx.transaction_system.push("Clip mesh", std::move(transaction));
  if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
  clear();
}

void Clip_Tool::on_draw_overlay(editor_context_t &, pass_builder_t &draws)
{
  const bool refused = cut_plane && !preview;
  for (const linalg::vec3 &point : points)
    draws.debug.box(point, POINT_HALF_EXTENTS, refused ? REFUSED_POINT_COLOR : POINT_COLOR,
                    renderer::fill_mode_t::solid);

  if (!preview)
    return;

  draw_half_tinted(draws, preview->front, keep == Keep::Back ? DISCARDED_FILL : KEPT_FRONT_FILL,
                   preview->cap_face_count);
  draw_half_tinted(draws, preview->back, keep == Keep::Front ? DISCARDED_FILL : KEPT_BACK_FILL,
                   preview->cap_face_count);
}

void Clip_Tool::on_draw_inspector(editor_context_t &)
{
  ImGui::TextUnformatted("Click a mesh, then a second point: the cut");
  ImGui::TextUnformatted("runs through both along the view. A third");
  ImGui::TextUnformatted("point sets the plane exactly.");
  ImGui::Separator();
  ImGui::Text("Points: %d / 3", (int)points.size());
  ImGui::Text("Keep: %s  (Tab)", keep_label((int)keep));
  ImGui::TextUnformatted(preview ? "Enter commits" : "No cut yet");
  ImGui::TextUnformatted("Right click removes a point, Escape clears");
}

} // namespace client
