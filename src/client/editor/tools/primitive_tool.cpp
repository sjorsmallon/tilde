#include "primitive_tool.hpp"

#include "../../../shared/collision/collision_detection.hpp"
#include "../../../shared/curve.hpp"
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
constexpr color_t EDIT_EDGE{255, 200, 90, 255};
constexpr color_t HOVER_EDGE{255, 240, 180, 255};
constexpr color_t CURVE_POINT{255, 255, 255, 255};
constexpr color_t CURVE_LINE{255, 160, 60, 255};
constexpr float CURVE_POINT_RADIUS = 3.f;
constexpr const char *AXIS_LABELS[3] = {"X", "Y", "Z"};
constexpr linalg::vec3 ORIGIN{0.f, 0.f, 0.f};
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

std::vector<linalg::vec3> translated(const std::vector<linalg::vec3> &points,
                                     const linalg::vec3 &translation)
{
  std::vector<linalg::vec3> moved;
  moved.reserve(points.size());
  for (const linalg::vec3 &point : points)
    moved.push_back(point + translation);
  return moved;
}

shared::map_group_t *find_mutable_group_by_uid(shared::map_t &map, shared::entity_uid_t group_uid)
{
  for (shared::map_group_t &group : map.groups)
    if (group.uid == group_uid)
      return &group;
  return nullptr;
}

std::vector<shared::entity_uid_t> live_brush_members(const shared::map_t &map,
                                                     const shared::map_group_t &group)
{
  std::vector<shared::entity_uid_t> live;
  for (shared::entity_uid_t member : group.members)
  {
    const shared::map_geometry_t *entry = map.find_geometry_by_uid(member);
    if (entry != nullptr && std::holds_alternative<shared::brush_geometry_t>(entry->value))
      live.push_back(member);
  }
  return live;
}

std::optional<linalg::vec3> try_lowest_corner(const shared::map_t &map,
                                              Span<const shared::entity_uid_t> uids)
{
  std::optional<linalg::vec3> lowest;
  for (shared::entity_uid_t uid : uids)
  {
    const shared::aabb_bounds_t bounds = shared::get_bounds(map.find_geometry_by_uid(uid)->value);
    if (!lowest)
      lowest = bounds.min;
    else
      *lowest = {std::min(lowest->x, bounds.min.x), std::min(lowest->y, bounds.min.y),
                 std::min(lowest->z, bounds.min.z)};
  }
  return lowest;
}

// The curved faces of a hulled primitive, placed by `translation`, join
// `smoothing_group`; every other face keeps what sync_face_surfaces gave it.
void put_curved_faces_in_group(shared::brush_geometry_t &brush,
                               const shared::brush_polyhedron_t &hull,
                               const linalg::vec3 &translation, const linalg::vec3 &axis,
                               int32_t smoothing_group)
{
  for (const shared::brush_face_t &face : hull.faces)
  {
    if (!shared::primitive_face_is_curved(hull, face, ORIGIN, axis))
      continue;
    const Plane placed{hull.vertices[face.vertex_indices[0]] + translation, face.plane.normal};
    shared::face_surface_for(brush, placed).smoothing_group = smoothing_group;
  }
}

} // namespace

void Primitive_Tool::on_enable(editor_context_t &ctx)
{
  cursor_is_over_surface = false;
  hovered_recipe_group.reset();
  rebuild_if_changed();
  if (!ctx.map)
    return;
  for (shared::entity_uid_t uid : ctx.selection_handed_over)
  {
    const shared::map_group_t *group = shared::find_group_containing_member(*ctx.map, uid);
    if (group != nullptr && group->recipe)
    {
      begin_edit(ctx, group->uid);
      return;
    }
  }
}

void Primitive_Tool::on_disable(editor_context_t &ctx)
{
  end_edit(ctx);
  curve_points_in_progress.clear();
  cursor_is_over_surface = false;
  hovered_recipe_group.reset();
}

bool Primitive_Tool::is_drawing_curve() const
{
  return settings.kind == Kind::Curve && !edited_group;
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
  return Span<const shared::entity_uid_t>(edited_members.data(), edited_members.size());
}

std::optional<shared::entity_uid_t>
Primitive_Tool::try_pick_recipe_group(const editor_context_t &ctx,
                                      const viewport_state_t &view) const
{
  if (!ctx.map || !ctx.bvh || ctx.bvh->nodes.empty())
    return std::nullopt;
  ray_hit_result_t hit{};
  if (!bvh_intersect_ray(*ctx.bvh, view.mouse_ray.origin, view.mouse_ray.direction, hit) ||
      hit.id.type != Collision_Id::Type::Static_Geometry)
    return std::nullopt;
  const shared::entity_uid_t uid = hit.id.index;
  if (!ctx.object_is_visible(uid))
    return std::nullopt;
  const shared::map_group_t *group = shared::find_group_containing_member(*ctx.map, uid);
  if (group == nullptr || !group->recipe)
    return std::nullopt;
  return group->uid;
}

void Primitive_Tool::on_update(editor_context_t &ctx, const viewport_state_t &view, float)
{
  cached_view = view;
  hovered_recipe_group = try_pick_recipe_group(ctx, view);

  if (edited_group)
  {
    cursor_is_over_surface = false;
    const shared::map_group_t *group =
        ctx.map ? shared::find_group_by_uid(*ctx.map, *edited_group) : nullptr;
    if (group == nullptr || !group->recipe)
    {
      end_edit(ctx);
      return;
    }
    if (!edit_session && *group->recipe != applied_settings)
    {
      settings = *group->recipe;
      applied_settings = settings;
    }
    if (settings != applied_settings)
      regenerate(ctx);
    rebuild_if_changed();

    edited_members = live_brush_members(*ctx.map, *group);
    const std::optional<linalg::vec3> lowest = try_lowest_corner(
        *ctx.map, Span<const shared::entity_uid_t>(edited_members.data(), edited_members.size()));
    edit_translation = lowest ? *lowest - bounds.min : linalg::vec3{0.f, 0.f, 0.f};
    hovered_curve_point.reset();
    if (is_editing_curve())
    {
      if (dragged_curve_point)
        hovered_curve_point = dragged_curve_point;
      else if (const std::optional<linalg::vec2> cursor = try_cursor_screen_position(view))
        hovered_curve_point = try_pick_curve_handle(*cursor);
      if (hovered_curve_point)
        hovered_recipe_group.reset();
    }
    return;
  }

  const std::optional<linalg::vec3> point = try_pick_placement_point(ctx, view);
  if (point)
    ghost_position = *point;
  if (is_drawing_curve())
  {
    if (!curve_points_in_progress.empty())
      hovered_recipe_group.reset();
    cursor_is_over_surface = point.has_value() && !hovered_recipe_group;
    settings.quarter_turns = 0;
    settings.curve_points = curve_points_in_progress;
    if (cursor_is_over_surface &&
        (settings.curve_points.empty() || !(settings.curve_points.back() == *point)))
      settings.curve_points.push_back(*point);
  }
  else
    cursor_is_over_surface = point.has_value() && !hovered_recipe_group;
  rebuild_if_changed();
}

void Primitive_Tool::begin_edit(editor_context_t &ctx, shared::entity_uid_t group_uid)
{
  end_edit(ctx);
  const shared::map_group_t *group = shared::find_group_by_uid(*ctx.map, group_uid);
  if (group == nullptr || !group->recipe)
    return;
  edited_group = group_uid;
  settings = *group->recipe;
  applied_settings = settings;
  edited_members = live_brush_members(*ctx.map, *group);
  rebuild_if_changed();
}

void Primitive_Tool::end_edit(editor_context_t &ctx)
{
  end_curve_drag();
  if (edit_session)
    commit_edit(ctx);
  edited_group.reset();
  edited_members.clear();
  hovered_curve_point.reset();
}

bool Primitive_Tool::is_editing_curve() const
{
  return settings.kind == Kind::Curve && edited_group.has_value();
}

std::vector<linalg::vec3> Primitive_Tool::edited_curve_points_in_world() const
{
  std::vector<linalg::vec3> in_world;
  in_world.reserve(settings.curve_points.size());
  for (const linalg::vec3 &point : settings.curve_points)
    in_world.push_back(shared::turn_about_up(point, settings.quarter_turns) + edit_translation);
  return in_world;
}

std::optional<size_t>
Primitive_Tool::try_pick_curve_handle(const linalg::vec2 &screen_position) const
{
  const std::vector<linalg::vec3> handles = edited_curve_points_in_world();
  return try_pick_handle_under_cursor(Span<const linalg::vec3>(handles.data(), handles.size()),
                                      cached_view, screen_position);
}

void Primitive_Tool::drag_curve_point(const editor_context_t &ctx, const input::mouse_event_t &e)
{
  const size_t index = *dragged_curve_point;
  if (index >= settings.curve_points.size())
  {
    end_curve_drag();
    return;
  }
  const linalg::vec3 handle = edited_curve_points_in_world()[index];
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

  settings.curve_points[index] =
      shared::turn_about_up(moved - *pinned_translation, (4 - settings.quarter_turns) % 4);
}

void Primitive_Tool::end_curve_drag()
{
  if (!dragged_curve_point)
    return;
  dragged_curve_point.reset();
  pinned_translation.reset();
  shared::rebase_curve_points_to_wall_corner(settings);
}

void Primitive_Tool::regenerate(editor_context_t &ctx)
{
  shared::map_group_t *group = find_mutable_group_by_uid(*ctx.map, *edited_group);
  shared::clamp_primitive_recipe(settings);

  std::vector<shared::entity_uid_t> live = live_brush_members(*ctx.map, *group);
  if (!edit_session)
  {
    edit_session_t session;
    for (shared::entity_uid_t uid : live)
      session.members_before.emplace_back(uid, ctx.map->find_geometry_by_uid(uid)->value);
    session.groups_before = ctx.map->groups;
    edit_session = std::move(session);
  }

  const std::optional<linalg::vec3> lowest =
      try_lowest_corner(*ctx.map, Span<const shared::entity_uid_t>(live.data(), live.size()));
  int32_t smoothing_group = 0;
  for (shared::entity_uid_t uid : live)
    for (const shared::face_surface_t &surface :
         std::get<shared::brush_geometry_t>(ctx.map->find_geometry_by_uid(uid)->value).face_surfaces)
      if (surface.smoothing_group != 0 && smoothing_group == 0)
        smoothing_group = surface.smoothing_group;
  if (smoothing_group == 0)
    smoothing_group = shared::next_free_smoothing_group(
        Span<const shared::map_geometry_t>(ctx.map->geometry.data(), ctx.map->geometry.size()));

  rebuild_if_changed();
  const linalg::vec3 translation = pinned_translation ? *pinned_translation
                                   : lowest           ? *lowest - bounds.min
                                                      : compute_translation(ctx);
  const linalg::vec3 axis = shared::primitive_recipe_axis(settings);

  for (size_t piece = 0; piece < point_sets.size(); ++piece)
  {
    if (piece < live.size())
    {
      shared::brush_geometry_t &brush =
          std::get<shared::brush_geometry_t>(ctx.map->find_geometry_by_uid(live[piece])->value);
      brush.hull_points = translated(point_sets[piece], translation);
      shared::sync_face_surfaces(brush);
      put_curved_faces_in_group(brush, hulls[piece], translation, axis, smoothing_group);
      continue;
    }
    shared::brush_geometry_t brush;
    brush.hull_points = translated(point_sets[piece], translation);
    shared::sync_face_surfaces(brush);
    put_curved_faces_in_group(brush, hulls[piece], translation, axis, smoothing_group);
    group->members.push_back(ctx.map->add_geometry(brush));
  }
  for (size_t extra = point_sets.size(); extra < live.size(); ++extra)
  {
    ctx.map->remove_geometry(live[extra]);
    std::erase(group->members, live[extra]);
  }

  group->recipe = settings;
  applied_settings = settings;
  if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
}

void Primitive_Tool::commit_edit(editor_context_t &ctx)
{
  transaction_t transaction;
  const edit_session_t &session = *edit_session;
  for (const std::pair<shared::entity_uid_t, shared::geometry_value_t> &before : session.members_before)
  {
    if (const shared::map_geometry_t *now = ctx.map->find_geometry_by_uid(before.first))
      transaction.add_geometry_modified(before.first, before.second, now->value);
    else
      transaction.add_geometry_removed(before.first, before.second);
  }
  if (const shared::map_group_t *group = shared::find_group_by_uid(*ctx.map, *edited_group))
    for (shared::entity_uid_t uid : live_brush_members(*ctx.map, *group))
    {
      const bool existed = std::any_of(
          session.members_before.begin(), session.members_before.end(),
          [uid](const std::pair<shared::entity_uid_t, shared::geometry_value_t> &before) {
            return before.first == uid;
          });
      if (!existed)
        transaction.add_geometry_created(uid, ctx.map->find_geometry_by_uid(uid)->value);
    }
  transaction.add_map_groups_modified(session.groups_before, ctx.map->groups);
  ctx.transaction_system.push(std::format("Edit {}", primitive_kind_label(settings.kind)),
                              std::move(transaction));
  edit_session.reset();
}

void Primitive_Tool::rebuild_if_changed()
{
  if (built && settings == built_settings)
    return;

  shared::clamp_primitive_recipe(settings);
  point_sets = shared::make_primitive_point_sets(settings);

  hulls.clear();
  flat_curve_pieces = 0;
  std::vector<linalg::vec3> every_point;
  for (size_t piece = 0; piece < point_sets.size();)
  {
    const std::optional<shared::brush_polyhedron_t> hull =
        shared::try_build_brush_polyhedron(point_sets[piece]);
    if (!hull)
    {
      if (settings.kind != Kind::Curve)
        fatal_error("primitive: a primitive produced a point set that does not hull");
      ++flat_curve_pieces;
      point_sets.erase(point_sets.begin() + (ptrdiff_t)piece);
      continue;
    }
    hulls.push_back(*hull);
    every_point.insert(every_point.end(), point_sets[piece].begin(), point_sets[piece].end());
    ++piece;
  }
  bounds = shared::compute_brush_bounds(every_point);
  built_settings = settings;
  built = true;
}

linalg::vec3 Primitive_Tool::compute_translation(const editor_context_t &ctx) const
{
  if (settings.kind == Kind::Curve)
    return ORIGIN;
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
  if (e.button == input::mouse_button_t::Right && is_drawing_curve())
  {
    remove_last_curve_point();
    return;
  }
  if (e.button != input::mouse_button_t::Left)
    return;
  if (is_editing_curve())
  {
    const linalg::vec2 screen_position{(float)e.position.x, (float)e.position.y};
    if (const std::optional<size_t> handle = try_pick_curve_handle(screen_position))
    {
      dragged_curve_point = handle;
      pinned_translation = edit_translation;
      return;
    }
  }
  if (hovered_recipe_group)
  {
    if (edited_group != hovered_recipe_group)
      begin_edit(ctx, *hovered_recipe_group);
    return;
  }
  if (edited_group)
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

void Primitive_Tool::on_mouse_up(editor_context_t &, const input::mouse_event_t &e)
{
  if (e.button == input::mouse_button_t::Left)
    end_curve_drag();
}
void Primitive_Tool::on_key_down(editor_context_t &ctx, const key_event_t &e)
{
  if (e.key == input::key_t::Escape)
  {
    if (edited_group)
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

void Primitive_Tool::place(editor_context_t &ctx)
{
  const char *label = primitive_kind_label(settings.kind);
  if (point_sets.empty())
  {
    hud::set_announcement(std::format("{}: nothing to place, every piece is flat", label));
    return;
  }
  const linalg::vec3 translation = compute_translation(ctx);
  shared::primitive_recipe_t recipe = settings;
  if (settings.kind == Kind::Curve)
    shared::rebase_curve_points_to_wall_corner(recipe);

  const int32_t smoothing_group = shared::next_free_smoothing_group(
      Span<const shared::map_geometry_t>(ctx.map->geometry.data(), ctx.map->geometry.size()));
  const linalg::vec3 axis = shared::primitive_recipe_axis(settings);

  transaction_t transaction;
  std::vector<shared::entity_uid_t> members;
  for (size_t piece = 0; piece < point_sets.size(); ++piece)
  {
    shared::brush_geometry_t brush;
    brush.hull_points = translated(point_sets[piece], translation);
    shared::sync_face_surfaces(brush);
    put_curved_faces_in_group(brush, hulls[piece], translation, axis, smoothing_group);
    const shared::entity_uid_t uid = ctx.map->add_geometry(brush);
    transaction.add_geometry_created(uid, std::move(brush));
    members.push_back(uid);
  }

  {
    std::vector<shared::map_group_t> groups_before = ctx.map->groups;
    const shared::entity_uid_t group_uid =
        shared::group_objects(*ctx.map, members, std::format("{} {}", label, members[0]));
    if (group_uid == shared::null_entity_uid)
      log_error("primitive: the {} brushes of the {} did not group", members.size(), label);
    else
      shared::find_group_containing_member(*ctx.map, members[0])->recipe = recipe;
    transaction.add_map_groups_modified(std::move(groups_before), ctx.map->groups);
  }

  ctx.transaction_system.push(std::format("Place {}", label), std::move(transaction));
  if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
  if (flat_curve_pieces > 0)
    hud::set_announcement(std::format("Placed a {} ({} brushes, {} flat pieces dropped)", label,
                                      members.size(), flat_curve_pieces));
  else
    hud::set_announcement(std::format("Placed a {} ({} brushes)", label, members.size()));
  curve_points_in_progress.clear();
}

void Primitive_Tool::on_draw_overlay(editor_context_t &ctx, pass_builder_t &draws)
{
  if (hovered_recipe_group && ctx.map && hovered_recipe_group != edited_group)
  {
    const shared::map_group_t *group = shared::find_group_by_uid(*ctx.map, *hovered_recipe_group);
    for (shared::entity_uid_t uid : live_brush_members(*ctx.map, *group))
    {
      const shared::aabb_bounds_t box = shared::get_bounds(ctx.map->find_geometry_by_uid(uid)->value);
      draws.debug.aabb(box.min, box.max, HOVER_EDGE);
    }
  }

  if (edited_group)
  {
    for (const shared::brush_polyhedron_t &hull : hulls)
      draw_brush_hull_wireframe(draws, hull, edit_translation, EDIT_EDGE, 0.5f);
    if (is_editing_curve())
    {
      const std::vector<linalg::vec3> handles = edited_curve_points_in_world();
      for (size_t index = 0; index < handles.size(); ++index)
      {
        const bool lit = hovered_curve_point == index;
        draws.debug.wire_sphere(handles[index], lit ? CURVE_POINT_RADIUS * 2.f : CURVE_POINT_RADIUS,
                                lit ? HOVER_EDGE : CURVE_POINT);
      }
      if (handles.size() >= 2)
      {
        const std::vector<linalg::vec3> centre_line = shared::sample_catmull_rom(
            Span<const linalg::vec3>(handles.data(), handles.size()),
            settings.curve_samples_per_segment);
        for (size_t i = 0; i + 1 < centre_line.size(); ++i)
          draws.debug.line(centre_line[i], centre_line[i + 1], CURVE_LINE, 0.5f, 0.f, true);
      }
    }
    return;
  }

  if (is_drawing_curve())
  {
    for (const linalg::vec3 &point : curve_points_in_progress)
      draws.debug.wire_sphere(point, CURVE_POINT_RADIUS, CURVE_POINT);
    if (cursor_is_over_surface)
      draws.debug.wire_sphere(ghost_position, CURVE_POINT_RADIUS, GHOST_EDGE);
    if (settings.curve_points.size() >= 2)
    {
      const std::vector<linalg::vec3> centre_line = shared::sample_catmull_rom(
          Span<const linalg::vec3>(settings.curve_points.data(), settings.curve_points.size()),
          settings.curve_samples_per_segment);
      for (size_t i = 0; i + 1 < centre_line.size(); ++i)
        draws.debug.line(centre_line[i], centre_line[i + 1], CURVE_LINE, 0.5f, 0.f, true);
    }
  }
  else if (!cursor_is_over_surface)
    return;
  if (hulls.empty())
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

void Primitive_Tool::on_draw_inspector(editor_context_t &ctx)
{
  if (edited_group)
  {
    ImGui::Text("Editing %s (group %u)", primitive_kind_label(settings.kind), *edited_group);
    if (ImGui::Button("Done (Esc)"))
      end_edit(ctx);
    ImGui::Separator();
  }

  int kind = (int)settings.kind;
  if (ImGui::Combo("Kind", &kind, "Prism\0Arch\0Straight stairs\0Spiral stairs\0Curve wall\0"))
    settings.kind = (Kind)kind;

  if (settings.kind != Kind::Spiral_Stairs && settings.kind != Kind::Curve)
  {
    ImGui::TextUnformatted(settings.kind == Kind::Straight_Stairs ? "Climbs along" : "Axis");
    for (int axis = 0; axis < 3; ++axis)
    {
      if (settings.kind == Kind::Straight_Stairs && axis == 1)
        continue;
      ImGui::SameLine();
      ImGui::RadioButton(AXIS_LABELS[axis], &settings.axis_index, axis);
    }
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
    case Kind::Straight_Stairs:
      ImGui::SliderInt("Steps", &settings.stair_steps, shared::MIN_STAIR_STEPS,
                       shared::MAX_STAIR_STEPS);
      ImGui::DragFloat("Rise", &settings.stair_rise, 1.f, 1.f, 256.f);
      ImGui::DragFloat("Tread depth", &settings.stair_tread_depth, 1.f, 1.f, 1024.f);
      ImGui::DragFloat("Width", &settings.stair_width, 1.f, 1.f, 4096.f);
      break;
    case Kind::Spiral_Stairs:
      ImGui::SliderInt("Steps", &settings.stair_steps, shared::MIN_STAIR_STEPS,
                       shared::MAX_STAIR_STEPS);
      ImGui::DragFloat("Rise", &settings.stair_rise, 1.f, 1.f, 256.f);
      ImGui::DragFloat("Inner radius", &settings.spiral_inner_radius, 1.f, 1.f, 4096.f);
      ImGui::DragFloat("Tread width", &settings.spiral_tread_width, 1.f, 1.f, 4096.f);
      ImGui::DragFloat("Degrees per step", &settings.spiral_degrees_per_step, 1.f,
                       shared::MIN_SPIRAL_DEGREES_PER_STEP,
                       shared::MAX_SPIRAL_DEGREES_PER_STEP);
      ImGui::Checkbox("Clockwise", &settings.spiral_clockwise);
      break;
    case Kind::Curve:
      ImGui::DragFloat("Width", &settings.curve_width, 1.f, 1.f, 4096.f);
      ImGui::DragFloat("Height", &settings.curve_height, 1.f, 1.f, 4096.f);
      ImGui::SliderInt("Samples per segment", &settings.curve_samples_per_segment,
                       shared::MIN_CURVE_SAMPLES_PER_SEGMENT, shared::MAX_CURVE_SAMPLES_PER_SEGMENT);
      if (is_drawing_curve())
        ImGui::Text("%d control point%s clicked", (int)curve_points_in_progress.size(),
                    curve_points_in_progress.size() == 1 ? "" : "s");
      else
        ImGui::Text("%d control point%s", (int)settings.curve_points.size(),
                    settings.curve_points.size() == 1 ? "" : "s");
      if (flat_curve_pieces > 0)
        ImGui::Text("%d flat piece%s dropped (a leg climbing straight up)", flat_curve_pieces,
                    flat_curve_pieces == 1 ? "" : "s");
      break;
  }

  ImGui::Separator();
  ImGui::Text("%d brush%s", (int)point_sets.size(), point_sets.size() == 1 ? "" : "es");
  if (is_drawing_curve())
    ImGui::TextUnformatted("Turns apply after placing (R in edit)");
  else
    ImGui::Text("Turned %d x 90 degrees (R, shift-R back)", settings.quarter_turns);
  if (is_editing_curve())
    ImGui::TextUnformatted("Drag a point along the floor, Shift drags it up and down");
  else if (edited_group)
    ImGui::TextUnformatted("Changes regenerate the group where it stands");
  else if (is_drawing_curve())
    ImGui::TextUnformatted("Click adds a point, right click takes one back,\nEnter places, Esc cancels");
  else
    ImGui::TextUnformatted("Click places; a click on a placed primitive edits it");

  if (edit_session && !dragged_curve_point && !ImGui::IsAnyItemActive() &&
      settings == applied_settings)
    commit_edit(ctx);
}

} // namespace client
