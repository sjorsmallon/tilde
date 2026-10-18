#include "primitive_tool.hpp"

#include "../../../shared/log.hpp"
#include "../../../shared/map/brush/brush_smoothing.hpp"
#include "../../../shared/map/brush/brush_primitives.hpp"
#include "../../../shared/map/map_geometry.hpp"
#include "../../../shared/map/map_group.hpp"
#include "../../hud.hpp"
#include "../geometry_editor.hpp"
#include "../transaction_system.hpp"
#include "imgui.h"

#include <algorithm>
#include <format>

namespace client
{

namespace
{

constexpr color_t GHOST_FILL{120, 200, 255, 60};
constexpr color_t GHOST_EDGE{200, 240, 255, 255};
constexpr linalg::vec3 AXES[3] = {{1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}};
constexpr const char *AXIS_LABELS[3] = {"X", "Y", "Z"};

std::vector<linalg::vec3> translated(const std::vector<linalg::vec3> &points,
                                     const linalg::vec3 &translation)
{
  std::vector<linalg::vec3> moved;
  moved.reserve(points.size());
  for (const linalg::vec3 &point : points)
    moved.push_back(point + translation);
  return moved;
}

} // namespace

void Primitive_Tool::on_enable(editor_context_t &)
{
  cursor_is_over_surface = false;
  rebuild_if_changed();
}

void Primitive_Tool::on_disable(editor_context_t &) { cursor_is_over_surface = false; }

void Primitive_Tool::on_update(editor_context_t &ctx, const viewport_state_t &view, float)
{
  rebuild_if_changed();
  const std::optional<linalg::vec3> point = try_pick_placement_point(ctx, view);
  cursor_is_over_surface = point.has_value();
  if (point)
    ghost_position = *point;
}

void Primitive_Tool::rebuild_if_changed()
{
  if (!hulls.empty() && settings == built_settings)
    return;

  settings.prism_sides =
      std::clamp(settings.prism_sides, shared::MIN_PRISM_SIDES, shared::MAX_PRISM_SIDES);
  settings.arch_segments =
      std::clamp(settings.arch_segments, shared::MIN_ARCH_SEGMENTS, shared::MAX_ARCH_SEGMENTS);
  settings.prism_radius = std::max(settings.prism_radius, 1.f);
  settings.prism_half_length = std::max(settings.prism_half_length, 1.f);
  settings.arch_inner_radius = std::max(settings.arch_inner_radius, 1.f);
  settings.arch_thickness = std::max(settings.arch_thickness, 1.f);
  settings.arch_half_depth = std::max(settings.arch_half_depth, 1.f);

  const linalg::vec3 axis = AXES[settings.axis_index];
  point_sets.clear();
  switch (settings.kind)
  {
    case Kind::Prism:
      point_sets.push_back(shared::make_prism_brush_points(
          {0.f, 0.f, 0.f}, axis, settings.prism_radius, settings.prism_half_length,
          settings.prism_sides));
      break;
    case Kind::Arch:
      point_sets = shared::make_arch_brush_point_sets(
          {0.f, 0.f, 0.f}, axis, settings.arch_inner_radius, settings.arch_thickness,
          settings.arch_half_depth, settings.arch_segments);
      break;
  }

  hulls.clear();
  std::vector<linalg::vec3> every_point;
  for (const std::vector<linalg::vec3> &points : point_sets)
  {
    const std::optional<shared::brush_polyhedron_t> hull =
        shared::try_build_brush_polyhedron(points);
    if (!hull)
      fatal_error("primitive: a primitive produced a point set that does not hull");
    hulls.push_back(*hull);
    every_point.insert(every_point.end(), points.begin(), points.end());
  }
  bounds = shared::compute_brush_bounds(every_point);
  built_settings = settings;
}

linalg::vec3 Primitive_Tool::compute_translation(const editor_context_t &ctx) const
{
  const linalg::vec3 half_extents = (bounds.max - bounds.min) * 0.5f;
  linalg::vec3 low_corner{ghost_position.x - half_extents.x, ghost_position.y,
                          ghost_position.z - half_extents.z};

  const float grid_step = ctx.grid ? ctx.grid->step() : 0.f;
  if (grid_step > 0.f)
    low_corner = {editor::snap(low_corner.x, grid_step), editor::snap(low_corner.y, grid_step),
                  editor::snap(low_corner.z, grid_step)};

  return low_corner - bounds.min;
}

void Primitive_Tool::on_mouse_down(editor_context_t &ctx, const input::mouse_event_t &e)
{
  if (e.button != input::mouse_button_t::Left || !cursor_is_over_surface)
    return;
  place(ctx);
}

void Primitive_Tool::on_mouse_drag(editor_context_t &, const input::mouse_event_t &) {}
void Primitive_Tool::on_mouse_up(editor_context_t &, const input::mouse_event_t &) {}
void Primitive_Tool::on_key_down(editor_context_t &, const key_event_t &) {}

void Primitive_Tool::place(editor_context_t &ctx)
{
  const linalg::vec3 translation = compute_translation(ctx);
  const char *label = settings.kind == Kind::Prism ? "prism" : "arch";

  const int32_t smoothing_group = shared::next_free_smoothing_group(
      Span<const shared::map_geometry_t>(ctx.map->geometry.data(), ctx.map->geometry.size()));
  const linalg::vec3 axis = AXES[settings.axis_index];

  transaction_t transaction;
  std::vector<shared::entity_uid_t> members;
  for (size_t piece = 0; piece < point_sets.size(); ++piece)
  {
    shared::brush_geometry_t brush;
    brush.hull_points = translated(point_sets[piece], translation);
    shared::sync_face_surfaces(brush);
    for (const shared::brush_face_t &face : hulls[piece].faces)
    {
      if (!shared::primitive_face_is_curved(hulls[piece], face, {0.f, 0.f, 0.f}, axis))
        continue;
      const Plane placed{hulls[piece].vertices[face.vertex_indices[0]] + translation,
                         face.plane.normal};
      shared::face_surface_for(brush, placed).smoothing_group = smoothing_group;
    }
    const shared::entity_uid_t uid = ctx.map->add_geometry(brush);
    transaction.add_geometry_created(uid, std::move(brush));
    members.push_back(uid);
  }

  if (members.size() > 1)
  {
    std::vector<shared::map_group_t> groups_before = ctx.map->groups;
    const shared::entity_uid_t group_uid =
        shared::group_objects(*ctx.map, members, std::format("{} {}", label, members[0]));
    if (group_uid == shared::null_entity_uid)
      log_error("primitive: the {} brushes of the {} did not group", members.size(), label);
    transaction.add_map_groups_modified(std::move(groups_before), ctx.map->groups);
  }

  ctx.transaction_system.push(std::format("Place {}", label), std::move(transaction));
  if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
  hud::set_announcement(std::format("Placed a {} ({} brushes)", label, members.size()));
}

void Primitive_Tool::on_draw_overlay(editor_context_t &ctx, pass_builder_t &draws)
{
  if (!cursor_is_over_surface || hulls.empty())
    return;

  const linalg::vec3 translation = compute_translation(ctx);
  for (const shared::brush_polyhedron_t &hull : hulls)
  {
    const std::vector<std::vector<linalg::vec3>> polygons = shared::brush_face_polygons(hull);
    for (const std::vector<linalg::vec3> &polygon : polygons)
    {
      const std::vector<linalg::vec3> moved = translated(polygon, translation);
      draws.debug.filled_polygon(Span<const linalg::vec3f>(moved.data(), moved.size()),
                                 GHOST_FILL, 0.f, {.depth_bias = 0.5f});
    }
    draw_brush_hull_wireframe(draws, hull, translation, GHOST_EDGE, 0.5f);
  }
}

void Primitive_Tool::on_draw_ui(editor_context_t &)
{
  ImGui::SetNextWindowSize({260, 0}, ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Primitive"))
  {
    int kind = (int)settings.kind;
    if (ImGui::Combo("Kind", &kind, "Prism\0Arch\0"))
      settings.kind = (Kind)kind;

    ImGui::TextUnformatted("Axis");
    for (int axis = 0; axis < 3; ++axis)
    {
      ImGui::SameLine();
      ImGui::RadioButton(AXIS_LABELS[axis], &settings.axis_index, axis);
    }

    ImGui::Separator();
    switch (settings.kind)
    {
      case Kind::Prism:
        ImGui::SliderInt("Sides", &settings.prism_sides, shared::MIN_PRISM_SIDES,
                         shared::MAX_PRISM_SIDES);
        ImGui::DragFloat("Radius", &settings.prism_radius, 1.f, 1.f, 4096.f);
        ImGui::DragFloat("Half length", &settings.prism_half_length, 1.f, 1.f, 4096.f);
        break;
      case Kind::Arch:
        ImGui::SliderInt("Segments", &settings.arch_segments, shared::MIN_ARCH_SEGMENTS,
                         shared::MAX_ARCH_SEGMENTS);
        ImGui::DragFloat("Inner radius", &settings.arch_inner_radius, 1.f, 1.f, 4096.f);
        ImGui::DragFloat("Thickness", &settings.arch_thickness, 1.f, 1.f, 4096.f);
        ImGui::DragFloat("Half depth", &settings.arch_half_depth, 1.f, 1.f, 4096.f);
        break;
    }

    ImGui::Separator();
    ImGui::Text("%d brush%s", (int)point_sets.size(), point_sets.size() == 1 ? "" : "es");
    ImGui::TextUnformatted("Click places; the low corner lands on the grid");
  }
  ImGui::End();
}

} // namespace client
