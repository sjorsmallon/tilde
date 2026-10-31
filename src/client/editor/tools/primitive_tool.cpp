#include "primitive_tool.hpp"

#include "../../../shared/collision/collision_detection.hpp"
#include "../../../shared/curve.hpp"
#include "../../../shared/log.hpp"
#include "../../../shared/map/map_geometry_mesh.hpp"
#include "../../../shared/map/mesh/mesh_primitives.hpp"
#include "../../camera.hpp"
#include "../../hud.hpp"
#include "../../input.hpp"
#include "../geometry_editor.hpp"
#include "../transaction_system.hpp"
#include "imgui.h"

#include <format>

namespace client
{

namespace
{

constexpr color_t GHOST_FILL{120, 200, 255, 60};
constexpr color_t GHOST_EDGE{200, 240, 255, 255};
constexpr color_t EDIT_EDGE{255, 200, 90, 255};
constexpr color_t HOVER_EDGE{255, 240, 180, 255};
constexpr color_t CURVE_POINT{255, 255, 255, 255};
constexpr color_t CURVE_LINE{255, 160, 60, 255};
constexpr float CURVE_POINT_RADIUS = 3.f;
constexpr const char *AXIS_LABELS[3] = {"X", "Y", "Z"};
using Kind = shared::primitive_kind_t;

const char *primitive_kind_label(Kind kind)
{
  switch (kind)
  {
    case Kind::Prism: return "prism";
    case Kind::Arch: return "arch";
    case Kind::Straight_Stairs: return "flight of stairs";
    case Kind::Spiral_Stairs: return "spiral stairs";
    case Kind::Curve: return "curve wall";
  }
  fatal_error("primitive_kind_label: unknown kind {}", (int)kind);
}

const shared::mesh_geometry_t *try_recipe_mesh_by_uid(const shared::map_t &map,
                                                      shared::entity_uid_t uid)
{
  const shared::map_geometry_t *entry = map.find_geometry_by_uid(uid);
  const shared::mesh_geometry_t *mesh =
      entry ? &entry->value : nullptr;
  if (mesh == nullptr || !std::holds_alternative<shared::recipe_mesh_source_t>(mesh->source))
    return nullptr;
  return mesh;
}

// A ghost's faces filled, through its pose.
void draw_mesh_fill(draw_lists_t &draws, const shared::mesh_geometry_t &mesh, color_t color)
{
  std::vector<linalg::vec3> polygon;
  for (const shared::mesh_face_t &face : mesh.faces)
  {
    polygon.clear();
    for (uint32_t index : face.vertex_indices)
      polygon.push_back(mesh.position + linalg::rotate(mesh.orientation, mesh.vertices[index]));
    draws.debug.filled_polygon(Span<const linalg::vec3f>(polygon.data(), polygon.size()), color,
                               0.f, {.depth_bias = 0.5f});
  }
}

void draw_centre_line(draw_lists_t &draws, Span<const linalg::vec3> points,
                      int samples_per_segment)
{
  if (points.size() < 2)
    return;
  const std::vector<linalg::vec3> centre_line = shared::sample_catmull_rom(points, samples_per_segment);
  for (size_t i = 0; i + 1 < centre_line.size(); ++i)
    draws.debug.line(centre_line[i], centre_line[i + 1], CURVE_LINE, 0.5f, 0.f, true);
}

} // namespace

void Primitive_Tool::on_enable(editor_context_t &ctx)
{
  cursor_is_over_surface = false;
  hovered_recipe_uid.reset();
  rebuild_if_changed();
  if (!ctx.map)
    return;
  for (shared::entity_uid_t uid : ctx.selection_handed_over)
  {
    if (try_recipe_mesh_by_uid(*ctx.map, uid) != nullptr)
    {
      begin_edit(ctx, uid);
      return;
    }
  }
}

void Primitive_Tool::on_disable(editor_context_t &ctx)
{
  end_edit(ctx);
  curve_points_in_progress.clear();
  cursor_is_over_surface = false;
  hovered_recipe_uid.reset();
}

bool Primitive_Tool::is_drawing_curve() const
{
  return settings.kind == Kind::Curve && !edited_uid;
}

bool Primitive_Tool::is_editing_curve() const
{
  return settings.kind == Kind::Curve && edited_uid.has_value();
}

void Primitive_Tool::add_curve_point(const linalg::vec3 &point)
{
  if (!curve_points_in_progress.empty() && curve_points_in_progress.back() == point)
  {
    hud::set_announcement("curve: that is already the last point");
    return;
  }
  if (curve_points_in_progress.size() >= shared::MAX_CURVE_POINTS)
  {
    hud::set_announcement(
        std::format("curve: {} points is the most a wall takes", shared::MAX_CURVE_POINTS));
    return;
  }
  curve_points_in_progress.push_back(point);
}

void Primitive_Tool::remove_last_curve_point()
{
  if (curve_points_in_progress.empty())
    return;
  curve_points_in_progress.pop_back();
}

void Primitive_Tool::cancel_curve()
{
  if (curve_points_in_progress.empty())
    return;
  curve_points_in_progress.clear();
  hud::set_announcement("curve cancelled");
}

Span<const shared::entity_uid_t> Primitive_Tool::selected_objects() const
{
  return Span<const shared::entity_uid_t>(edited_as_selection.data(), edited_as_selection.size());
}

const shared::mesh_geometry_t *Primitive_Tool::try_edited_mesh(const editor_context_t &ctx) const
{
  if (!edited_uid || !ctx.map)
    return nullptr;
  return try_recipe_mesh_by_uid(*ctx.map, *edited_uid);
}

std::optional<shared::entity_uid_t>
Primitive_Tool::try_pick_recipe_mesh(const editor_context_t &ctx,
                                     const viewport_state_t &view) const
{
  if (!ctx.map || !ctx.bvh || ctx.bvh->nodes.empty())
    return std::nullopt;
  ray_hit_result_t hit{};
  if (!bvh_intersect_ray(*ctx.bvh, view.mouse_ray.origin, view.mouse_ray.direction, hit) ||
      hit.id.type != Collision_Id::Type::Static_Geometry)
    return std::nullopt;
  const shared::entity_uid_t uid = hit.id.index;
  if (!ctx.object_is_visible(uid) || try_recipe_mesh_by_uid(*ctx.map, uid) == nullptr)
    return std::nullopt;
  return uid;
}

void Primitive_Tool::on_update(editor_context_t &ctx, const viewport_state_t &view, float)
{
  cached_view = view;
  hovered_recipe_uid = try_pick_recipe_mesh(ctx, view);

  if (edited_uid)
  {
    cursor_is_over_surface = false;
    const shared::mesh_geometry_t *mesh = try_edited_mesh(ctx);
    if (mesh == nullptr)
    {
      end_edit(ctx);
      return;
    }
    const shared::primitive_recipe_t &stored =
        std::get<shared::recipe_mesh_source_t>(mesh->source).recipe;
    if (!value_before_edit && stored != applied_settings)
    {
      settings = stored;
      applied_settings = settings;
    }
    if (settings != applied_settings)
      regenerate(ctx);
    rebuild_if_changed();

    hovered_curve_point.reset();
    if (is_editing_curve())
    {
      const float grid_step = ctx.grid ? ctx.grid->step() : editor::MAJOR_GRID_STEP;
      gizmo.snap_step = input::current_modifiers().alt ? 0.f : grid_step;
      if (gizmo.is_dragging())
      {
        if (const std::optional<gizmo_drag_t> drag =
                gizmo.try_update_drag(view.mouse_ray, make_gizmo_view()))
          set_curve_point_from_world(*mesh, *selected_curve_point,
                                     selected_point_at_drag_start + drag->translation);
        hovered_curve_point = selected_curve_point;
        hovered_recipe_uid.reset();
        return;
      }
      if (selected_curve_point && *selected_curve_point < settings.curve_points.size())
      {
        const linalg::vec3 at = edited_curve_points_in_world(ctx)[*selected_curve_point];
        const linalg::vec3 reach{CURVE_POINT_RADIUS, CURVE_POINT_RADIUS, CURVE_POINT_RADIUS};
        gizmo.set_target({at - reach, at + reach}, {}, make_gizmo_view(), at);
        gizmo.update_hover(view.mouse_ray);
      }
      else
      {
        selected_curve_point.reset();
        gizmo.clear_target();
      }

      if (dragged_curve_point)
        hovered_curve_point = dragged_curve_point;
      else if (gizmo.is_hovered())
        hovered_curve_point = selected_curve_point;
      else if (const std::optional<linalg::vec2> cursor = try_cursor_screen_position(view))
        hovered_curve_point = try_pick_curve_handle(ctx, *cursor);
      if (hovered_curve_point)
        hovered_recipe_uid.reset();
    }
    else
    {
      selected_curve_point.reset();
      gizmo.clear_target();
    }
    return;
  }

  const std::optional<linalg::vec3> point = try_pick_placement_point(ctx, view);
  if (point)
    ghost_position = *point;
  if (is_drawing_curve())
  {
    if (!curve_points_in_progress.empty())
      hovered_recipe_uid.reset();
    cursor_is_over_surface = point.has_value() && !hovered_recipe_uid;
    settings.quarter_turns = 0;
    settings.curve_points = curve_points_in_progress;
    if (cursor_is_over_surface &&
        (settings.curve_points.empty() || !(settings.curve_points.back() == *point)))
      settings.curve_points.push_back(*point);
  }
  else
    cursor_is_over_surface = point.has_value() && !hovered_recipe_uid;
  rebuild_if_changed();
}

void Primitive_Tool::begin_edit(editor_context_t &ctx, shared::entity_uid_t uid)
{
  end_edit(ctx);
  const shared::mesh_geometry_t *mesh = try_recipe_mesh_by_uid(*ctx.map, uid);
  if (mesh == nullptr)
    return;
  edited_uid = uid;
  edited_as_selection = {uid};
  settings = std::get<shared::recipe_mesh_source_t>(mesh->source).recipe;
  applied_settings = settings;
  rebuild_if_changed();
}

void Primitive_Tool::end_edit(editor_context_t &ctx)
{
  end_curve_drag(ctx);
  if (value_before_edit)
    commit_edit(ctx);
  edited_uid.reset();
  edited_as_selection.clear();
  hovered_curve_point.reset();
  selected_curve_point.reset();
  gizmo.clear_target();
}

std::vector<linalg::vec3>
Primitive_Tool::edited_curve_points_in_world(const editor_context_t &ctx) const
{
  std::vector<linalg::vec3> in_world;
  const shared::mesh_geometry_t *mesh = try_edited_mesh(ctx);
  if (mesh == nullptr)
    return in_world;
  in_world.reserve(settings.curve_points.size());
  for (const linalg::vec3 &point : settings.curve_points)
    in_world.push_back(mesh->position +
                       linalg::rotate(mesh->orientation,
                                      shared::turn_about_up(point, settings.quarter_turns)));
  return in_world;
}

std::optional<size_t>
Primitive_Tool::try_pick_curve_handle(const editor_context_t &ctx,
                                      const linalg::vec2 &screen_position) const
{
  const std::vector<linalg::vec3> handles = edited_curve_points_in_world(ctx);
  return try_pick_handle_under_cursor(Span<const linalg::vec3>(handles.data(), handles.size()),
                                      cached_view, screen_position);
}

void Primitive_Tool::drag_curve_point(const editor_context_t &ctx, const input::mouse_event_t &e)
{
  const size_t index = *dragged_curve_point;
  const shared::mesh_geometry_t *mesh = try_edited_mesh(ctx);
  if (mesh == nullptr || index >= settings.curve_points.size())
  {
    dragged_curve_point.reset();
    return;
  }
  const linalg::vec3 handle = edited_curve_points_in_world(ctx)[index];
  const linalg::ray_t &ray = cached_view.mouse_ray;

  linalg::vec3 plane_normal{0.f, 1.f, 0.f};
  if (e.mods.shift)
  {
    plane_normal = linalg::vec3{ray.direction.x, 0.f, ray.direction.z};
    if (linalg::length_squared(plane_normal) < 1e-6f)
      return;
    plane_normal = linalg::normalize(plane_normal);
  }
  float along = 0.f;
  if (!linalg::intersect_ray_plane(ray.origin, ray.direction, handle, plane_normal, along))
    return;
  linalg::vec3 moved = ray.origin + ray.direction * along;
  if (e.mods.shift)
    moved = {handle.x, moved.y, handle.z};
  else
    moved.y = handle.y;

  const float grid_step = ctx.grid ? ctx.grid->step() : 0.f;
  if (grid_step > 0.f)
    moved = {editor::snap(moved.x, grid_step), editor::snap(moved.y, grid_step),
             editor::snap(moved.z, grid_step)};

  set_curve_point_from_world(*mesh, index, moved);
}

void Primitive_Tool::set_curve_point_from_world(const shared::mesh_geometry_t &mesh, size_t index,
                                                const linalg::vec3 &world)
{
  const linalg::vec3 local =
      linalg::rotate(linalg::conjugate(mesh.orientation), world - mesh.position);
  settings.curve_points[index] = shared::turn_about_up(local, (4 - settings.quarter_turns) % 4);
}

gizmo_view_t Primitive_Tool::make_gizmo_view() const
{
  const camera_t &camera = cached_view.camera;
  return {camera.position, get_orientation_vectors(camera).forward, camera.orthographic,
          camera.ortho_height, camera.fov_degrees};
}

// The release rebases the points to the wall's new corner and moves the pose
// by the same amount, so nothing in the world moves and the stored frame is
// the corner frame again.
void Primitive_Tool::end_curve_drag(editor_context_t &ctx)
{
  if (!dragged_curve_point && !gizmo.is_dragging())
    return;
  dragged_curve_point.reset();
  gizmo.end_drag();
  if (!ctx.map || !edited_uid)
    return;
  shared::map_geometry_t *entry = ctx.map->find_geometry_by_uid(*edited_uid);
  shared::mesh_geometry_t *mesh = entry ? &entry->value : nullptr;
  if (mesh == nullptr)
    return;

  if (settings != applied_settings)
    regenerate(ctx);

  shared::primitive_recipe_t rebased = settings;
  shared::rebase_curve_points_to_wall_corner(rebased);
  if (!rebased.curve_points.empty() && rebased.curve_points != settings.curve_points)
  {
    const linalg::vec3 corner = settings.curve_points[0] - rebased.curve_points[0];
    settings = rebased;
    if (!value_before_edit)
      value_before_edit = entry->value;
    shared::regenerate_mesh_from_recipe(*mesh, settings);
    mesh->position = mesh->position + linalg::rotate(mesh->orientation,
                                                     shared::turn_about_up(corner, settings.quarter_turns));
    applied_settings = settings;
  }
  if (value_before_edit)
    commit_edit(ctx);
}

void Primitive_Tool::regenerate(editor_context_t &ctx)
{
  shared::map_geometry_t *entry = ctx.map->find_geometry_by_uid(*edited_uid);
  shared::mesh_geometry_t *mesh = entry ? &entry->value : nullptr;
  if (mesh == nullptr)
    return;
  shared::clamp_primitive_recipe(settings);
  if (shared::make_primitive_mesh(settings).faces.empty())
  {
    hud::set_announcement(std::format("{}: builds nothing from these settings, change kept back",
                                      primitive_kind_label(settings.kind)));
    settings = applied_settings;
    return;
  }
  if (!value_before_edit)
    value_before_edit = entry->value;
  shared::regenerate_mesh_from_recipe(*mesh, settings);
  applied_settings = settings;
}

void Primitive_Tool::commit_edit(editor_context_t &ctx)
{
  const shared::map_geometry_t *entry = ctx.map ? ctx.map->find_geometry_by_uid(*edited_uid) : nullptr;
  if (entry != nullptr && !shared::geometry_values_equal(*value_before_edit, entry->value))
  {
    transaction_t transaction;
    transaction.add_geometry_modified(*edited_uid, *value_before_edit, entry->value);
    ctx.transaction_system.push(std::format("Edit {}", primitive_kind_label(settings.kind)),
                                std::move(transaction));
  }
  value_before_edit.reset();
}

void Primitive_Tool::rebuild_if_changed()
{
  if (built && settings == built_settings)
    return;
  shared::clamp_primitive_recipe(settings);
  built_mesh = shared::make_primitive_mesh(settings);
  built_settings = settings;
  built = true;
}

// Where the ghost's pose goes: the built mesh's corner is its local origin,
// so the pose IS the low corner, snapped to the grid. A curve's points are
// world points while it is drawn, so its pose is the origin.
linalg::vec3 Primitive_Tool::compute_placement_position(const editor_context_t &ctx) const
{
  if (settings.kind == Kind::Curve)
    return {0.f, 0.f, 0.f};
  const shared::aabb_bounds_t bounds = shared::compute_mesh_local_bounds(built_mesh);
  const linalg::vec3 half_extents = (bounds.max - bounds.min) * 0.5f;
  linalg::vec3 low_corner{ghost_position.x - half_extents.x, ghost_position.y,
                          ghost_position.z - half_extents.z};

  const float grid_step = ctx.grid ? ctx.grid->step() : 0.f;
  if (grid_step > 0.f)
    low_corner = {editor::snap(low_corner.x, grid_step), editor::snap(low_corner.y, grid_step),
                  editor::snap(low_corner.z, grid_step)};
  return low_corner;
}

void Primitive_Tool::on_mouse_down(editor_context_t &ctx, const input::mouse_event_t &e)
{
  if (e.button == input::mouse_button_t::Right && is_drawing_curve())
  {
    remove_last_curve_point();
    return;
  }
  if (e.button != input::mouse_button_t::Left)
    return;
  if (is_editing_curve())
  {
    if (selected_curve_point && gizmo.try_begin_drag(cached_view.mouse_ray, make_gizmo_view()))
    {
      selected_point_at_drag_start = edited_curve_points_in_world(ctx)[*selected_curve_point];
      return;
    }
    const linalg::vec2 screen_position{(float)e.position.x, (float)e.position.y};
    if (const std::optional<size_t> handle = try_pick_curve_handle(ctx, screen_position))
    {
      selected_curve_point = handle;
      dragged_curve_point = handle;
      return;
    }
  }
  if (hovered_recipe_uid)
  {
    if (edited_uid != hovered_recipe_uid)
      begin_edit(ctx, *hovered_recipe_uid);
    return;
  }
  if (edited_uid)
  {
    end_edit(ctx);
    return;
  }
  if (!cursor_is_over_surface)
    return;
  if (is_drawing_curve())
    add_curve_point(ghost_position);
  else
    place(ctx);
}

void Primitive_Tool::on_mouse_drag(editor_context_t &ctx, const input::mouse_event_t &e)
{
  if (dragged_curve_point && is_editing_curve())
    drag_curve_point(ctx, e);
}

void Primitive_Tool::on_mouse_up(editor_context_t &ctx, const input::mouse_event_t &e)
{
  if (e.button == input::mouse_button_t::Left)
    end_curve_drag(ctx);
}

void Primitive_Tool::on_key_down(editor_context_t &ctx, const key_event_t &e)
{
  if (e.key == input::key_t::Escape)
  {
    if (edited_uid)
      end_edit(ctx);
    else
      cancel_curve();
    return;
  }
  if (e.key == input::key_t::Enter && is_drawing_curve())
  {
    if (curve_points_in_progress.size() < 2)
    {
      hud::set_announcement("curve: click at least two points first");
      return;
    }
    settings.curve_points = curve_points_in_progress;
    rebuild_if_changed();
    place(ctx);
    return;
  }
  if (e.key != input::key_t::R)
    return;
  if (is_drawing_curve())
  {
    hud::set_announcement("curve: turn the wall after placing it");
    return;
  }
  const int direction = e.mods.shift ? 3 : 1;
  settings.quarter_turns = (settings.quarter_turns + direction) % 4;
}

bool Primitive_Tool::try_undo_pending_step(editor_context_t &ctx)
{
  if (!is_drawing_curve() || curve_points_in_progress.empty())
    return false;
  remove_last_curve_point();
  hud::set_announcement(std::format("curve: point taken back, {} left",
                                    curve_points_in_progress.size()));
  return true;
}

void Primitive_Tool::place(editor_context_t &ctx)
{
  const char *label = primitive_kind_label(settings.kind);
  if (built_mesh.faces.empty())
  {
    hud::set_announcement(std::format("{}: nothing to place", label));
    return;
  }

  shared::mesh_geometry_t mesh;
  if (settings.kind == Kind::Curve)
  {
    shared::primitive_recipe_t recipe = settings;
    shared::rebase_curve_points_to_wall_corner(recipe);
    mesh = shared::make_primitive_mesh(recipe);
    mesh.position = settings.curve_points[0] - recipe.curve_points[0];
  }
  else
  {
    mesh = built_mesh;
    mesh.position = compute_placement_position(ctx);
  }

  const shared::entity_uid_t uid = ctx.map->add_geometry(mesh);
  transaction_t transaction;
  transaction.add_geometry_created(uid, std::move(mesh));
  ctx.transaction_system.push(std::format("Place {}", label), std::move(transaction));
  hud::set_announcement(std::format("Placed a {} ({} faces)", label, built_mesh.faces.size()));
  curve_points_in_progress.clear();
}

void Primitive_Tool::on_draw_overlay(editor_context_t &ctx, draw_lists_t &draws)
{
  if (hovered_recipe_uid && ctx.map && hovered_recipe_uid != edited_uid)
  {
    const shared::aabb_bounds_t box =
        shared::get_bounds(ctx.map->find_geometry_by_uid(*hovered_recipe_uid)->value);
    draws.debug.aabb(box.min, box.max, HOVER_EDGE);
  }

  if (edited_uid)
  {
    if (const shared::mesh_geometry_t *mesh = try_edited_mesh(ctx))
      draw_mesh_wireframe(draws, *mesh, EDIT_EDGE, 0.5f);
    if (is_editing_curve())
    {
      const std::vector<linalg::vec3> handles = edited_curve_points_in_world(ctx);
      for (size_t index = 0; index < handles.size(); ++index)
      {
        const bool lit = hovered_curve_point == index || selected_curve_point == index;
        const float reach = lit ? CURVE_POINT_RADIUS * 2.f : CURVE_POINT_RADIUS;
        draws.debug.box(handles[index], {reach, reach, reach}, lit ? HOVER_EDGE : CURVE_POINT,
                        renderer::fill_mode_t::wireframe, 0.f, 0.f, true);
      }
      draw_centre_line(draws, Span<const linalg::vec3>(handles.data(), handles.size()),
                       settings.curve_samples_per_segment);
      gizmo.draw(draws);
    }
    return;
  }

  if (is_drawing_curve())
  {
    for (const linalg::vec3 &point : curve_points_in_progress)
      draws.debug.wire_sphere(point, CURVE_POINT_RADIUS, CURVE_POINT);
    if (cursor_is_over_surface)
      draws.debug.wire_sphere(ghost_position, CURVE_POINT_RADIUS, GHOST_EDGE);
    draw_centre_line(draws,
                     Span<const linalg::vec3>(settings.curve_points.data(), settings.curve_points.size()),
                     settings.curve_samples_per_segment);
  }
  else if (!cursor_is_over_surface)
    return;
  if (built_mesh.faces.empty())
    return;

  shared::mesh_geometry_t ghost = built_mesh;
  ghost.position = compute_placement_position(ctx);
  draw_mesh_fill(draws, ghost, GHOST_FILL);
  draw_mesh_wireframe(draws, ghost, GHOST_EDGE, 0.5f);
}

void Primitive_Tool::on_draw_inspector(editor_context_t &ctx)
{
  if (edited_uid)
  {
    ImGui::Text("Editing %s (uid %u)", primitive_kind_label(settings.kind), *edited_uid);
    if (ImGui::Button("Done (Esc)"))
      end_edit(ctx);
    ImGui::Separator();
  }

  // An edit is committed when its widget is let go: a drag or slider on the
  // frame it is released after a change, a click the frame it is made.
  bool widget_committed = false;
  const auto dragged = [&widget_committed](bool) {
    widget_committed |= ImGui::IsItemDeactivatedAfterEdit();
  };
  const auto clicked = [&widget_committed](bool changed) {
    widget_committed |= changed;
    return changed;
  };

  int kind = (int)settings.kind;
  if (clicked(ImGui::Combo("Kind", &kind, "Prism\0Arch\0Straight stairs\0Spiral stairs\0Curve wall\0")))
    settings.kind = (Kind)kind;

  if (shared::primitive_kind_has_axis(settings.kind))
  {
    ImGui::TextUnformatted(settings.kind == Kind::Straight_Stairs ? "Climbs along" : "Axis");
    for (int axis = 0; axis < 3; ++axis)
    {
      if (settings.kind == Kind::Straight_Stairs && axis == 1)
        continue;
      ImGui::SameLine();
      clicked(ImGui::RadioButton(AXIS_LABELS[axis], &settings.axis_index, axis));
    }
  }

  ImGui::Separator();
  switch (settings.kind)
  {
    case Kind::Prism:
      dragged(ImGui::SliderInt("Sides", &settings.prism_sides, shared::MIN_PRISM_SIDES,
                               shared::MAX_PRISM_SIDES));
      dragged(ImGui::DragFloat("Radius", &settings.prism_radius, 1.f, 1.f, 4096.f));
      dragged(ImGui::DragFloat("Half length", &settings.prism_half_length, 1.f, 1.f, 4096.f));
      break;
    case Kind::Arch:
      dragged(ImGui::SliderInt("Segments", &settings.arch_segments, shared::MIN_ARCH_SEGMENTS,
                               shared::MAX_ARCH_SEGMENTS));
      dragged(ImGui::DragFloat("Inner radius", &settings.arch_inner_radius, 1.f, 1.f, 4096.f));
      dragged(ImGui::DragFloat("Thickness", &settings.arch_thickness, 1.f, 1.f, 4096.f));
      dragged(ImGui::DragFloat("Half depth", &settings.arch_half_depth, 1.f, 1.f, 4096.f));
      break;
    case Kind::Straight_Stairs:
      dragged(ImGui::SliderInt("Steps", &settings.stair_steps, shared::MIN_STAIR_STEPS,
                               shared::MAX_STAIR_STEPS));
      dragged(ImGui::DragFloat("Rise", &settings.stair_rise, 1.f, 1.f, 256.f));
      dragged(ImGui::DragFloat("Tread depth", &settings.stair_tread_depth, 1.f, 1.f, 1024.f));
      dragged(ImGui::DragFloat("Width", &settings.stair_width, 1.f, 1.f, 4096.f));
      break;
    case Kind::Spiral_Stairs:
      dragged(ImGui::SliderInt("Steps", &settings.stair_steps, shared::MIN_STAIR_STEPS,
                               shared::MAX_STAIR_STEPS));
      dragged(ImGui::DragFloat("Rise", &settings.stair_rise, 1.f, 1.f, 256.f));
      dragged(ImGui::DragFloat("Inner radius", &settings.spiral_inner_radius, 1.f, 1.f, 4096.f));
      dragged(ImGui::DragFloat("Tread width", &settings.spiral_tread_width, 1.f, 1.f, 4096.f));
      dragged(ImGui::DragFloat("Degrees per step", &settings.spiral_degrees_per_step, 1.f,
                               shared::MIN_SPIRAL_DEGREES_PER_STEP,
                               shared::MAX_SPIRAL_DEGREES_PER_STEP));
      clicked(ImGui::Checkbox("Clockwise", &settings.spiral_clockwise));
      break;
    case Kind::Curve:
      dragged(ImGui::DragFloat("Width", &settings.curve_width, 1.f, 1.f, 4096.f));
      dragged(ImGui::DragFloat("Height", &settings.curve_height, 1.f, 1.f, 4096.f));
      dragged(ImGui::SliderInt("Samples per segment", &settings.curve_samples_per_segment,
                               shared::MIN_CURVE_SAMPLES_PER_SEGMENT,
                               shared::MAX_CURVE_SAMPLES_PER_SEGMENT));
      if (is_drawing_curve())
        ImGui::Text("%d control point%s clicked", (int)curve_points_in_progress.size(),
                    curve_points_in_progress.size() == 1 ? "" : "s");
      else
        ImGui::Text("%d control point%s", (int)settings.curve_points.size(),
                    settings.curve_points.size() == 1 ? "" : "s");
      break;
  }

  ImGui::Separator();
  ImGui::Text("%d vertices, %d faces", (int)built_mesh.vertices.size(), (int)built_mesh.faces.size());
  if (is_drawing_curve())
    ImGui::TextUnformatted("Turns apply after placing (R in edit)");
  else
    ImGui::Text("Turned %d x 90 degrees (R, shift-R back)", settings.quarter_turns);
  if (is_editing_curve())
    ImGui::TextUnformatted("Drag a point along the floor, Shift drags it up and down");
  else if (edited_uid)
    ImGui::TextUnformatted("Changes regenerate the mesh where it stands");
  else if (is_drawing_curve())
    ImGui::TextUnformatted("Click adds a point, right click takes one back,\nEnter places, Esc cancels");
  else
    ImGui::TextUnformatted("Click places; a click on a placed primitive edits it");

  if (widget_committed && edited_uid)
  {
    if (settings != applied_settings)
      regenerate(ctx);
    if (value_before_edit)
      commit_edit(ctx);
  }
}

} // namespace client
