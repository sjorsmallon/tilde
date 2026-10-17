#include "clip_tool.hpp"

#include "../../../shared/collision/collision_detection.hpp"
#include "../../../shared/log.hpp"
#include "../../../shared/map/map_geometry.hpp"
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
  linalg::vec3 normal;
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
  pick.normal = hit.normal;
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

void draw_half_tinted(pass_builder_t &draws, const shared::brush_geometry_t &half, color_t fill,
                      const Plane &cut_plane)
{
  const std::optional<shared::brush_polyhedron_t> hull =
      shared::try_build_brush_polyhedron(half.hull_points);
  if (!hull)
    return;

  const std::vector<std::vector<linalg::vec3>> polygons = shared::brush_face_polygons(*hull);
  for (size_t face = 0; face < polygons.size(); ++face)
  {
    const std::vector<linalg::vec3> &polygon = polygons[face];
    draws.debug.filled_polygon(Span<const linalg::vec3f>(polygon.data(), polygon.size()), fill,
                               0.f, {.depth_bias = 0.5f});

    const bool is_cap =
        std::abs(linalg::dot(hull->faces[face].plane.normal, cut_plane.normal)) > 0.999f;
    if (!is_cap)
      continue;
    for (size_t i = 0; i < polygon.size(); ++i)
      draws.debug.line(polygon[i], polygon[(i + 1) % polygon.size()], CAP_OUTLINE);
  }
}

} // namespace

void Clip_Tool::on_enable(editor_context_t &ctx) { clear(ctx); }

void Clip_Tool::on_disable(editor_context_t &ctx) { clear(ctx); }

void Clip_Tool::clear(editor_context_t &)
{
  target_uid = shared::null_entity_uid;
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
  if (!entry || !std::holds_alternative<shared::brush_geometry_t>(entry->value))
  {
    hud::set_announcement("clip: the brush is gone");
    clear(ctx);
    return;
  }

  if (previewed_value && !shared::geometry_values_equal(*previewed_value, entry->value))
    rebuild_preview(ctx);
}

void Clip_Tool::add_point(editor_context_t &ctx, const linalg::vec3 &point)
{
  points.push_back(point);
  const linalg::vec3 forward = get_orientation_vectors(cached_view.camera).forward;
  cut_plane = try_plane_through(Span<const linalg::vec3>(points.data(), points.size()), forward);
  rebuild_preview(ctx);
}

void Clip_Tool::rebuild_preview(editor_context_t &ctx)
{
  preview.reset();
  previewed_value.reset();
  if (!cut_plane || target_uid == shared::null_entity_uid)
    return;

  const shared::map_geometry_t *entry = ctx.map->find_geometry_by_uid(target_uid);
  const shared::brush_geometry_t *brush =
      entry ? std::get_if<shared::brush_geometry_t>(&entry->value) : nullptr;
  if (!brush)
    return;

  const shared::face_surface_t *clicked = shared::find_face_surface(*brush, cap_plane);
  shared::face_surface_t cap_surface = clicked ? *clicked : shared::face_surface_t{};
  preview = shared::try_split_brush_by_plane(*brush, *cut_plane, cap_surface);
  previewed_value = entry->value;
  if (!preview)
    hud::set_announcement("clip refused: see the log");
}

void Clip_Tool::on_mouse_down(editor_context_t &ctx, const input::mouse_event_t &e)
{
  if (e.button == input::mouse_button_t::Right)
  {
    if (points.size() <= 1)
    {
      clear(ctx);
      return;
    }
    points.pop_back();
    const linalg::vec3 forward = get_orientation_vectors(cached_view.camera).forward;
    cut_plane =
        try_plane_through(Span<const linalg::vec3>(points.data(), points.size()), forward);
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
    const shared::map_geometry_t *entry = ctx.map->find_geometry_by_uid(pick->uid);
    if (!entry || !std::holds_alternative<shared::brush_geometry_t>(entry->value))
    {
      hud::set_announcement("clip: the first point must be on a brush");
      return;
    }
    clear(ctx);
    target_uid = pick->uid;
    cap_plane = Plane{pick->point, pick->normal};
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
    clear(ctx);
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
    log_error("clip: brush {} vanished before the commit", target_uid);
    clear(ctx);
    return;
  }

  transaction_t transaction;
  const shared::geometry_value_t removed = entry->value;
  ctx.map->remove_geometry(target_uid);
  transaction.add_geometry_removed(target_uid, removed);

  if (keep != Keep::Back)
  {
    const shared::entity_uid_t uid = ctx.map->add_geometry(preview->front);
    transaction.add_geometry_created(uid, preview->front);
  }
  if (keep != Keep::Front)
  {
    const shared::entity_uid_t uid = ctx.map->add_geometry(preview->back);
    transaction.add_geometry_created(uid, preview->back);
  }

  ctx.transaction_system.push("Clip brush", std::move(transaction));
  if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
  clear(ctx);
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
                   *cut_plane);
  draw_half_tinted(draws, preview->back, keep == Keep::Front ? DISCARDED_FILL : KEPT_BACK_FILL,
                   *cut_plane);
}

void Clip_Tool::on_draw_ui(editor_context_t &)
{
  ImGui::SetNextWindowSize({260, 0}, ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Clip"))
  {
    ImGui::TextUnformatted("Click a brush, then a second point: the cut");
    ImGui::TextUnformatted("runs through both along the view. A third");
    ImGui::TextUnformatted("point sets the plane exactly.");
    ImGui::Separator();
    ImGui::Text("Points: %d / 3", (int)points.size());
    ImGui::Text("Keep: %s  (Tab)", keep_label((int)keep));
    ImGui::TextUnformatted(preview ? "Enter commits" : "No cut yet");
    ImGui::TextUnformatted("Right click removes a point, Escape clears");
  }
  ImGui::End();
}

} // namespace client
