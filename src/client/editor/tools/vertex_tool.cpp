#include "vertex_tool.hpp"

#include "../../../shared/collision/collision_detection.hpp"
#include "../../../shared/log.hpp"
#include "../../../shared/map/mesh/mesh_primitives.hpp"
#include "../../camera.hpp"
#include "../../geometry_renderer.hpp"
#include "../../hud.hpp"
#include "../geometry_editor.hpp"
#include "imgui.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <variant>

namespace client
{

namespace
{

constexpr float OVERLAY_DEPTH_BIAS = -200.0f;
constexpr float HANDLE_RADIUS = 5.0f;          // pixels
constexpr float SELECTED_HANDLE_RADIUS = 7.0f; // pixels
constexpr float BAND_DRAG_THRESHOLD = 4.0f;    // pixels
constexpr color_t HOVERED_FACE_FILL = with_alpha(colors::white, 40);
constexpr color_t SELECTED_FACE_FILL = with_alpha(colors::gold, 60);

// The point of the axis line nearest the ray, as a distance along the axis
// from `anchor`. Nothing when the ray looks straight down the axis.
[[nodiscard]] std::optional<float> try_distance_along_axis(const linalg::vec3 &anchor,
                                                           const linalg::vec3 &axis,
                                                           const linalg::ray_t &ray)
{
  const linalg::vec3 between = anchor - ray.origin;
  const float axis_dot_ray = linalg::dot(axis, ray.direction);
  const float denominator = 1.0f - axis_dot_ray * axis_dot_ray;
  if (std::abs(denominator) < 1e-5f)
    return std::nullopt;
  return (linalg::dot(between, ray.direction) * axis_dot_ray - linalg::dot(between, axis)) /
         denominator;
}

[[nodiscard]] std::optional<linalg::vec3> try_intersect_ray_with_plane(const linalg::ray_t &ray,
                                                                       const Plane &plane)
{
  const float denominator = linalg::dot(plane.normal, ray.direction);
  if (std::abs(denominator) < 1e-5f)
    return std::nullopt;
  const float t = linalg::dot(plane.normal, plane.point - ray.origin) / denominator;
  if (t < 0.f)
    return std::nullopt;
  return ray.origin + ray.direction * t;
}

// The world axis the camera looks along most: a drag in the plane across it
// is the one the cursor can express.
linalg::vec3 dominant_axis(const linalg::vec3 &direction)
{
  const float x = std::abs(direction.x);
  const float y = std::abs(direction.y);
  const float z = std::abs(direction.z);
  if (x >= y && x >= z)
    return {1.f, 0.f, 0.f};
  if (y >= z)
    return {0.f, 1.f, 0.f};
  return {0.f, 0.f, 1.f};
}

linalg::vec3 snap_point(const linalg::vec3 &point, float step)
{
  return {editor::snap(point.x, step), editor::snap(point.y, step), editor::snap(point.z, step)};
}

// Every undirected edge once: a ring edge whose reverse another face walks is
// listed by the face that walks it a < b.
std::vector<std::pair<uint32_t, uint32_t>> collect_undirected_edges(
    const shared::mesh_geometry_t &mesh)
{
  const std::vector<shared::mesh_directed_edge_t> edges = shared::collect_mesh_directed_edges(mesh);
  std::vector<std::pair<uint32_t, uint32_t>> undirected;
  undirected.reserve(edges.size() / 2);
  for (const shared::mesh_directed_edge_t &edge : edges)
  {
    if (edge.a > edge.b && shared::try_find_mesh_face_walking_edge(edges, edge.b, edge.a))
      continue;
    undirected.emplace_back(std::min(edge.a, edge.b), std::max(edge.a, edge.b));
  }
  return undirected;
}

float distance_to_segment(const linalg::vec2 &point, const linalg::vec2 &a, const linalg::vec2 &b)
{
  const linalg::vec2 along{b.x - a.x, b.y - a.y};
  const float length_squared = along.x * along.x + along.y * along.y;
  float t = 0.f;
  if (length_squared > 1e-6f)
    t = std::clamp(((point.x - a.x) * along.x + (point.y - a.y) * along.y) / length_squared, 0.f,
                   1.f);
  const float dx = a.x + along.x * t - point.x;
  const float dy = a.y + along.y * t - point.y;
  return std::sqrt(dx * dx + dy * dy);
}

bool every_vertex_selected(Span<const uint32_t> ring, Span<const uint32_t> selected_sorted)
{
  for (uint32_t vertex : ring)
    if (!std::binary_search(selected_sorted.begin(), selected_sorted.end(), vertex))
      return false;
  return true;
}

} // namespace

void Vertex_Tool::on_enable(editor_context_t &ctx)
{
  assert(ctx.map);
  mode = Mode::Vertex;
  hover = {};
  drag = {};
  live_defect.reset();
  select_mesh(shared::null_entity_uid);

  if (ctx.selection_handed_over.size() == 1 && try_get_mesh(ctx, ctx.selection_handed_over[0]))
    select_mesh(ctx.selection_handed_over[0]);
}

void Vertex_Tool::on_disable(editor_context_t &ctx)
{
  // A drag in progress is an edit already visible in the map; it is judged as
  // a release would judge it rather than left without an undo entry.
  if (drag.kind == Drag::Move)
    end_move(ctx);
  drag = {};
}

Span<const shared::entity_uid_t> Vertex_Tool::selected_objects() const
{
  return Span<const shared::entity_uid_t>(selected_uids.data(), selected_uids.size());
}

shared::mesh_geometry_t *Vertex_Tool::try_get_mesh(editor_context_t &ctx, shared::entity_uid_t uid)
{
  if (uid == shared::null_entity_uid || !ctx.map)
    return nullptr;
  shared::map_geometry_t *entry = ctx.map->find_geometry_by_uid(uid);
  return entry ? &entry->value : nullptr;
}

shared::mesh_geometry_t *Vertex_Tool::try_get_selected_mesh(editor_context_t &ctx)
{
  return try_get_mesh(ctx, selected_uid);
}

void Vertex_Tool::select_mesh(shared::entity_uid_t uid)
{
  if (uid != selected_uid)
    selected_vertices.clear();
  selected_uid = uid;
  selected_uids.clear();
  if (selected_uid != shared::null_entity_uid)
    selected_uids.push_back(selected_uid);
  live_defect.reset();
}

bool Vertex_Tool::vertex_is_selected(uint32_t vertex) const
{
  return std::binary_search(selected_vertices.begin(), selected_vertices.end(), vertex);
}

void Vertex_Tool::add_vertex_to_selection(uint32_t vertex)
{
  const std::vector<uint32_t>::iterator slot =
      std::lower_bound(selected_vertices.begin(), selected_vertices.end(), vertex);
  if (slot == selected_vertices.end() || *slot != vertex)
    selected_vertices.insert(slot, vertex);
}

// Ctrl semantics: an element wholly selected drops out, anything else joins.
void Vertex_Tool::toggle_vertices(Span<const uint32_t> vertices)
{
  if (every_vertex_selected(vertices, selected_vertices))
  {
    for (uint32_t vertex : vertices)
      selected_vertices.erase(
          std::lower_bound(selected_vertices.begin(), selected_vertices.end(), vertex));
    return;
  }
  for (uint32_t vertex : vertices)
    add_vertex_to_selection(vertex);
}

std::vector<uint32_t> Vertex_Tool::hovered_element_vertices(
    const shared::mesh_geometry_t &mesh) const
{
  switch (mode)
  {
  case Mode::Vertex:
    if (hover.vertex)
      return {*hover.vertex};
    return {};
  case Mode::Edge:
    if (hover.edge)
      return {hover.edge->a, hover.edge->b};
    return {};
  case Mode::Face:
    if (hover.face && *hover.face < mesh.faces.size())
      return mesh.faces[*hover.face].vertex_indices;
    return {};
  }
  return {};
}

void Vertex_Tool::update_hover(editor_context_t &ctx)
{
  hover = {};

  if (ctx.bvh)
  {
    ray_hit_result_t hit{};
    if (bvh_intersect_ray(*ctx.bvh, cached_view.mouse_ray.origin, cached_view.mouse_ray.direction,
                          hit) &&
        hit.id.type == Collision_Id::Type::Static_Geometry &&
        try_get_mesh(ctx, hit.id.index))
      hover.uid = hit.id.index;
  }

  // Elements are picked on the SELECTED mesh alone, in screen space, so a
  // vertex behind a neighbour is still reachable and a click means one thing.
  const shared::mesh_geometry_t *mesh = try_get_selected_mesh(ctx);
  if (!mesh || mesh->vertices.empty())
    return;
  const std::optional<linalg::vec2> cursor = try_cursor_screen_position(cached_view);
  if (!cursor)
    return;

  switch (mode)
  {
  case Mode::Vertex:
  {
    std::vector<linalg::vec3> handles;
    handles.reserve(mesh->vertices.size());
    for (uint32_t vertex = 0; vertex < (uint32_t)mesh->vertices.size(); ++vertex)
      handles.push_back(shared::compute_mesh_world_vertex(*mesh, vertex));
    if (const std::optional<size_t> picked =
            try_pick_handle_under_cursor(handles, cached_view, *cursor))
      hover.vertex = (uint32_t)*picked;
    return;
  }
  case Mode::Edge:
  {
    float best = HANDLE_PICK_RADIUS_PIXELS;
    for (const std::pair<uint32_t, uint32_t> &edge : collect_undirected_edges(*mesh))
    {
      const std::optional<linalg::vec2> a =
          try_project_to_screen(cached_view, shared::compute_mesh_world_vertex(*mesh, edge.first));
      const std::optional<linalg::vec2> b = try_project_to_screen(
          cached_view, shared::compute_mesh_world_vertex(*mesh, edge.second));
      if (!a || !b)
        continue;
      const float distance = distance_to_segment(*cursor, *a, *b);
      if (distance < best)
      {
        best = distance;
        hover.edge = edge_ref_t{edge.first, edge.second};
      }
    }
    return;
  }
  case Mode::Face:
  {
    if (hover.uid != selected_uid)
      return;
    if (const std::optional<shared::mesh_face_hit_t> face_hit = shared::try_pick_mesh_face(
            *mesh, cached_view.mouse_ray.origin, cached_view.mouse_ray.direction))
    {
      hover.face = face_hit->face_index;
      hover.face_point = face_hit->position;
    }
    return;
  }
  }
}

void Vertex_Tool::on_update(editor_context_t &ctx, const viewport_state_t &view, float)
{
  cached_view = view;

  if (selected_uid != shared::null_entity_uid && !try_get_selected_mesh(ctx))
  {
    drag = {};
    select_mesh(shared::null_entity_uid);
  }

  if (drag.kind == Drag::Move)
  {
    const float step = input::current_modifiers().alt
                           ? 0.f
                           : (ctx.grid ? ctx.grid->step() : editor::MAJOR_GRID_STEP);
    update_move(ctx, step);
    return;
  }

  update_hover(ctx);
}

void Vertex_Tool::on_mouse_down(editor_context_t &ctx, const input::mouse_event_t &e)
{
  if (e.button != input::mouse_button_t::Left || drag.kind != Drag::None)
    return;

  const linalg::vec2 screen_position{(float)e.position.x, (float)e.position.y};
  shared::mesh_geometry_t *mesh = try_get_selected_mesh(ctx);

  if (mesh)
  {
    const std::vector<uint32_t> element = hovered_element_vertices(*mesh);
    if (!element.empty())
    {
      if (e.mods.ctrl)
      {
        toggle_vertices(element);
        return;
      }
      if (!every_vertex_selected(element, selected_vertices))
      {
        selected_vertices.clear();
        for (uint32_t vertex : element)
          add_vertex_to_selection(vertex);
      }

      linalg::vec3 anchor{0, 0, 0};
      if (mode == Mode::Face)
        anchor = hover.face_point;
      else
      {
        for (uint32_t vertex : element)
          anchor = anchor + shared::compute_mesh_world_vertex(*mesh, vertex);
        anchor = anchor * (1.0f / (float)element.size());
      }
      begin_move(*mesh, anchor, e.mods);
      return;
    }

    // A press beside the handles of the selected mesh is a band, or a click
    // the release resolves (empty space clears, another mesh selects).
    if (mode == Mode::Vertex)
    {
      drag.kind = Drag::Band_Armed;
      drag.band = {};
      drag.band.start = screen_position;
      drag.band.end = screen_position;
      drag.band.adds = e.mods.ctrl;
      drag.band.press_uid = hover.uid;
      return;
    }
  }

  select_mesh(hover.uid);
}

void Vertex_Tool::begin_move(shared::mesh_geometry_t &mesh, const linalg::vec3 &anchor,
                             const input::modifiers_t &mods)
{
  move_t &move = drag.move;
  move = {};
  move.anchor = anchor;
  move.geometry_at_start = mesh;
  move.vertices_at_start = mesh.vertices;

  if (mode == Mode::Face && hover.face)
  {
    move.along_axis = true;
    move.axis = shared::compute_mesh_world_face_plane(mesh, *hover.face).normal;
  }
  else if (mods.shift)
  {
    move.along_axis = true;
    move.axis = {0.f, 1.f, 0.f};
  }
  else
  {
    move.along_axis = false;
    move.plane_normal = dominant_axis(get_orientation_vectors(cached_view.camera).forward);
  }

  if (move.along_axis)
  {
    const std::optional<float> distance =
        try_distance_along_axis(move.anchor, move.axis, cached_view.mouse_ray);
    if (!distance)
      return;
    move.start_distance = *distance;
  }
  else
  {
    const std::optional<linalg::vec3> point =
        try_intersect_ray_with_plane(cached_view.mouse_ray, Plane{move.anchor, move.plane_normal});
    if (!point)
      return;
    move.start_plane_point = *point;
  }

  // A hand edit freezes the recipe; a drag that moves nothing restores the
  // start value, recipe and all.
  (void)shared::freeze_mesh_recipe(mesh);
  drag.kind = Drag::Move;
}

void Vertex_Tool::update_move(editor_context_t &ctx, float grid_step)
{
  shared::mesh_geometry_t *mesh = try_get_selected_mesh(ctx);
  if (!mesh || !drag.move.geometry_at_start)
  {
    drag = {};
    return;
  }

  move_t &move = drag.move;
  linalg::vec3 world_delta{0, 0, 0};
  if (move.along_axis)
  {
    const std::optional<float> distance =
        try_distance_along_axis(move.anchor, move.axis, cached_view.mouse_ray);
    if (!distance)
      return;
    float travel = *distance - move.start_distance;
    if (grid_step > 0.f)
      travel = std::round(travel / grid_step) * grid_step;
    world_delta = move.axis * travel;
  }
  else
  {
    const std::optional<linalg::vec3> point =
        try_intersect_ray_with_plane(cached_view.mouse_ray, Plane{move.anchor, move.plane_normal});
    if (!point)
      return;
    const linalg::vec3 target = move.anchor + (*point - move.start_plane_point);
    world_delta = (grid_step > 0.f ? snap_point(target, grid_step) : target) - move.anchor;
  }

  if (world_delta.x == move.world_delta.x && world_delta.y == move.world_delta.y &&
      world_delta.z == move.world_delta.z)
    return;
  move.world_delta = world_delta;

  const linalg::vec3 local_delta =
      linalg::rotate(linalg::conjugate(mesh->orientation), world_delta);
  mesh->vertices = move.vertices_at_start;
  for (uint32_t vertex : selected_vertices)
    if (vertex < mesh->vertices.size())
      mesh->vertices[vertex] = mesh->vertices[vertex] + local_delta;

  live_defect = shared::find_mesh_defect(*mesh);
  refresh_generated_geometry_mesh(*mesh, selected_uid, ctx.map->materials, ctx.map->lightmap);
}

void Vertex_Tool::end_move(editor_context_t &ctx)
{
  shared::mesh_geometry_t *mesh = try_get_selected_mesh(ctx);
  std::optional<shared::mesh_geometry_t> start = std::move(drag.move.geometry_at_start);
  const linalg::vec3 world_delta = drag.move.world_delta;
  drag = {};
  if (!mesh || !start)
    return;

  const bool unchanged = shared::geometry_values_equal(*start, *mesh);
  const bool moved_nothing = world_delta.x == 0.f && world_delta.y == 0.f && world_delta.z == 0.f;
  if (unchanged || moved_nothing || live_defect)
  {
    if (live_defect)
    {
      const std::string reason = shared::format_mesh_defect(*live_defect);
      log_error("vertex drag on mesh {} refused: {}", selected_uid, reason);
      hud::set_announcement("vertex drag refused: " + reason);
    }
    *mesh = *start;
    live_defect.reset();
    refresh_generated_geometry_mesh(*mesh, selected_uid, ctx.map->materials, ctx.map->lightmap);
    return;
  }

  transaction_t transaction;
  transaction.add_geometry_modified(selected_uid, std::move(*start), *mesh);
  ctx.transaction_system.push("Move vertices", std::move(transaction));
  if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
}

void Vertex_Tool::cancel_move(editor_context_t &ctx)
{
  shared::mesh_geometry_t *mesh = try_get_selected_mesh(ctx);
  std::optional<shared::mesh_geometry_t> start = std::move(drag.move.geometry_at_start);
  drag = {};
  live_defect.reset();
  if (!mesh || !start)
    return;
  *mesh = *start;
  refresh_generated_geometry_mesh(*mesh, selected_uid, ctx.map->materials, ctx.map->lightmap);
}

void Vertex_Tool::resolve_band_press_as_click()
{
  const shared::entity_uid_t pressed = drag.band.press_uid;
  drag = {};
  if (pressed == selected_uid)
  {
    selected_vertices.clear();
    return;
  }
  select_mesh(pressed);
}

void Vertex_Tool::on_mouse_drag(editor_context_t &ctx, const input::mouse_event_t &e)
{
  const linalg::vec2 screen_position{(float)e.position.x, (float)e.position.y};

  if (drag.kind == Drag::Band_Armed)
  {
    const float dx = screen_position.x - drag.band.start.x;
    const float dy = screen_position.y - drag.band.start.y;
    if (std::sqrt(dx * dx + dy * dy) < BAND_DRAG_THRESHOLD)
      return;
    drag.kind = Drag::Band_Sizing;
  }

  if (drag.kind != Drag::Band_Sizing)
    return;

  drag.band.end = screen_position;
  const shared::mesh_geometry_t *mesh = try_get_selected_mesh(ctx);
  if (!mesh)
    return;

  // Rebuilt from scratch every frame, so shrinking the band takes a vertex
  // back; a ctrl band skips the clear and only ever adds.
  const float min_x = std::min(drag.band.start.x, drag.band.end.x);
  const float max_x = std::max(drag.band.start.x, drag.band.end.x);
  const float min_y = std::min(drag.band.start.y, drag.band.end.y);
  const float max_y = std::max(drag.band.start.y, drag.band.end.y);
  if (!drag.band.adds)
    selected_vertices.clear();
  for (uint32_t vertex = 0; vertex < (uint32_t)mesh->vertices.size(); ++vertex)
  {
    const std::optional<linalg::vec2> projected =
        try_project_to_screen(cached_view, shared::compute_mesh_world_vertex(*mesh, vertex));
    if (!projected)
      continue;
    if (projected->x >= min_x && projected->x <= max_x && projected->y >= min_y &&
        projected->y <= max_y)
      add_vertex_to_selection(vertex);
  }
}

void Vertex_Tool::on_mouse_up(editor_context_t &ctx, const input::mouse_event_t &e)
{
  if (e.button != input::mouse_button_t::Left)
    return;

  switch (drag.kind)
  {
  case Drag::None:
    return;
  case Drag::Band_Armed:
    resolve_band_press_as_click();
    return;
  case Drag::Band_Sizing:
    drag = {};
    return;
  case Drag::Move:
    end_move(ctx);
    return;
  }
}

void Vertex_Tool::on_key_down(editor_context_t &ctx, const key_event_t &e)
{
  switch (e.key)
  {
  case input::key_t::Tab:
    if (drag.kind == Drag::Move)
      end_move(ctx);
    drag = {};
    mode = (mode == Mode::Vertex) ? Mode::Edge : (mode == Mode::Edge) ? Mode::Face : Mode::Vertex;
    return;

  case input::key_t::Escape:
    if (drag.kind == Drag::Move)
    {
      cancel_move(ctx);
      return;
    }
    drag = {};
    if (!selected_vertices.empty())
      selected_vertices.clear();
    else
      select_mesh(shared::null_entity_uid);
    return;

  case input::key_t::A:
  {
    if (!e.mods.ctrl)
      return;
    const shared::mesh_geometry_t *mesh = try_get_selected_mesh(ctx);
    if (!mesh)
      return;
    selected_vertices.clear();
    for (uint32_t vertex = 0; vertex < (uint32_t)mesh->vertices.size(); ++vertex)
      selected_vertices.push_back(vertex);
    return;
  }

  default:
    return;
  }
}

void Vertex_Tool::edit_selected_vertices(editor_context_t &ctx, const char *name,
                                         Span<const linalg::vec3> local_positions)
{
  shared::mesh_geometry_t *mesh = try_get_selected_mesh(ctx);
  if (!mesh || local_positions.count != selected_vertices.size())
    return;

  const shared::mesh_geometry_t before = *mesh;
  for (size_t i = 0; i < selected_vertices.size(); ++i)
    if (selected_vertices[i] < mesh->vertices.size())
      mesh->vertices[selected_vertices[i]] = local_positions[i];
  (void)shared::freeze_mesh_recipe(*mesh);

  if (shared::geometry_values_equal(before, *mesh))
  {
    *mesh = before;
    return;
  }

  if (const std::optional<shared::mesh_defect_t> defect = shared::find_mesh_defect(*mesh))
  {
    const std::string reason = shared::format_mesh_defect(*defect);
    log_error("vertex edit on mesh {} refused: {}", selected_uid, reason);
    hud::set_announcement("vertex edit refused: " + reason);
    *mesh = before;
    return;
  }

  refresh_generated_geometry_mesh(*mesh, selected_uid, ctx.map->materials, ctx.map->lightmap);
  transaction_t transaction;
  transaction.add_geometry_modified(selected_uid, before, *mesh);
  ctx.transaction_system.push(name, std::move(transaction));
  if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
}

// ============================================================================
// Overlay: the world half. The handles are screen-space discs and live in the
// UI pass (draw_handles_ui), since the debug list draws in world space.
// ============================================================================

void Vertex_Tool::on_draw_overlay(editor_context_t &ctx, pass_builder_t &draws)
{
  if (hover.uid != shared::null_entity_uid && hover.uid != selected_uid)
    if (const shared::mesh_geometry_t *hovered = try_get_mesh(ctx, hover.uid))
      draw_mesh_wireframe(draws, *hovered, colors::white, OVERLAY_DEPTH_BIAS);

  const shared::mesh_geometry_t *mesh = try_get_selected_mesh(ctx);
  if (!mesh)
    return;

  draw_mesh_wireframe(draws, *mesh, live_defect ? colors::red : colors::yellow,
                      OVERLAY_DEPTH_BIAS);

  // An edge with both ends selected and a face with its whole ring selected
  // read as selected, whatever mode picked them.
  for (const std::pair<uint32_t, uint32_t> &edge : collect_undirected_edges(*mesh))
  {
    const bool hovered = hover.edge && hover.edge->a == edge.first && hover.edge->b == edge.second;
    const bool selected = vertex_is_selected(edge.first) && vertex_is_selected(edge.second);
    if (!hovered && !selected)
      continue;
    draws.debug.line(shared::compute_mesh_world_vertex(*mesh, edge.first),
                     shared::compute_mesh_world_vertex(*mesh, edge.second),
                     selected ? colors::gold : colors::cyan, OVERLAY_DEPTH_BIAS * 2.f);
  }

  for (uint32_t face_index = 0; face_index < (uint32_t)mesh->faces.size(); ++face_index)
  {
    const shared::mesh_face_t &face = mesh->faces[face_index];
    const bool hovered = hover.face && *hover.face == face_index;
    const bool selected = every_vertex_selected(face.vertex_indices, selected_vertices);
    if (!hovered && !selected)
      continue;
    std::vector<linalg::vec3f> polygon;
    polygon.reserve(face.vertex_indices.size());
    for (uint32_t vertex : face.vertex_indices)
      polygon.push_back(shared::compute_mesh_world_vertex(*mesh, vertex));
    draws.debug.filled_polygon(polygon, selected ? SELECTED_FACE_FILL : HOVERED_FACE_FILL, 0.0f,
                               {.depth_bias = OVERLAY_DEPTH_BIAS});
  }

  if (drag.kind == Drag::Move)
  {
    const move_t &move = drag.move;
    const linalg::vec3 end = move.anchor + move.world_delta;
    draws.debug.arrow(move.anchor, end, live_defect ? colors::red : colors::green);
    char label[96];
    std::snprintf(label, sizeof(label), "%.0f  %.0f  %.0f", move.world_delta.x,
                  move.world_delta.y, move.world_delta.z);
    draws.debug.text(end, label, live_defect ? colors::red : colors::green);
  }
}

void Vertex_Tool::draw_handles_ui()
{
  ImDrawList *list = ImGui::GetBackgroundDrawList();
  if (drag.kind == Drag::Band_Sizing)
  {
    list->AddRect({drag.band.start.x, drag.band.start.y}, {drag.band.end.x, drag.band.end.y},
                  IM_COL32(255, 220, 60, 255), 0.0f, 0, 1.5f);
    list->AddRectFilled({drag.band.start.x, drag.band.start.y},
                        {drag.band.end.x, drag.band.end.y}, IM_COL32(255, 220, 60, 30));
  }
}

void Vertex_Tool::on_draw_ui(editor_context_t &ctx)
{
  draw_handles_ui();

  const shared::mesh_geometry_t *mesh = try_get_selected_mesh(ctx);
  if (mesh)
  {
    // Every vertex has a disc in Vertex mode; the other modes draw only what
    // is selected, since the element under the cursor is drawn in the world.
    ImDrawList *list = ImGui::GetBackgroundDrawList();
    const ImU32 selected_fill = IM_COL32(255, 190, 60, 255);
    const ImU32 hovered_fill = IM_COL32(120, 220, 255, 255);
    const ImU32 plain_fill = IM_COL32(40, 40, 48, 220);
    const ImU32 outline = IM_COL32(255, 255, 255, 230);
    for (uint32_t vertex = 0; vertex < (uint32_t)mesh->vertices.size(); ++vertex)
    {
      const bool selected = vertex_is_selected(vertex);
      const bool hovered = hover.vertex && *hover.vertex == vertex;
      if (mode != Mode::Vertex && !selected)
        continue;
      const std::optional<linalg::vec2> projected =
          try_project_to_screen(cached_view, shared::compute_mesh_world_vertex(*mesh, vertex));
      if (!projected)
        continue;
      const float radius = selected ? SELECTED_HANDLE_RADIUS : HANDLE_RADIUS;
      list->AddCircleFilled({projected->x, projected->y}, radius,
                            selected ? selected_fill : hovered ? hovered_fill : plain_fill);
      list->AddCircle({projected->x, projected->y}, radius, outline, 0, 1.5f);
    }
  }
}

void Vertex_Tool::on_draw_inspector(editor_context_t &ctx)
{
  const shared::mesh_geometry_t *mesh = try_get_selected_mesh(ctx);

  constexpr Mode MODE_BUTTONS[] = {Mode::Vertex, Mode::Edge, Mode::Face};
  constexpr const char *MODE_LABELS[] = {"Vertex", "Edge", "Face"};
  int mode_index = 0;
  for (int index = 0; index < (int)std::size(MODE_BUTTONS); ++index)
    if (MODE_BUTTONS[index] == mode)
      mode_index = index;
  bool chose_a_mode = false;
  for (int index = 0; index < (int)std::size(MODE_BUTTONS); ++index)
    chose_a_mode = ImGui::RadioButton(MODE_LABELS[index], &mode_index, index) || chose_a_mode;
  if (chose_a_mode && MODE_BUTTONS[mode_index] != mode)
  {
    if (drag.kind == Drag::Move)
      end_move(ctx);
    drag = {};
    mode = MODE_BUTTONS[mode_index];
  }

  ImGui::Separator();

  if (!mesh)
    ImGui::TextDisabled("click a mesh");
  else if (std::holds_alternative<shared::asset_mesh_source_t>(mesh->source))
    ImGui::TextDisabled("mesh %u is asset-sourced: its vertices are the asset's, not editable here",
                        selected_uid);
  else
  {
    ImGui::Text("mesh %u  %zu vertices, %zu faces  (%zu selected)", selected_uid,
                mesh->vertices.size(), mesh->faces.size(), selected_vertices.size());
    if (std::holds_alternative<shared::recipe_mesh_source_t>(mesh->source))
      ImGui::TextDisabled("a recipe mesh: moving a vertex freezes it to inline");

    if (live_defect)
      ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "not a solid: %s",
                         shared::format_mesh_defect(*live_defect).c_str());

    if (selected_vertices.size() == 1 && selected_vertices[0] < mesh->vertices.size() &&
        drag.kind == Drag::None)
    {
      linalg::vec3 world = shared::compute_mesh_world_vertex(*mesh, selected_vertices[0]);
      float position[3] = {world.x, world.y, world.z};
      if (ImGui::InputFloat3("position", position, "%.3f", ImGuiInputTextFlags_EnterReturnsTrue))
      {
        const linalg::vec3 local = linalg::rotate(
            linalg::conjugate(mesh->orientation),
            linalg::vec3{position[0], position[1], position[2]} - mesh->position);
        edit_selected_vertices(ctx, "Set vertex position", Span<const linalg::vec3>(&local, 1));
      }
    }
  }

  ImGui::Separator();
  ImGui::TextDisabled("Tab         vertex / edge / face mode");
  ImGui::TextDisabled("click       select the element under the cursor, ctrl toggles");
  ImGui::TextDisabled("drag        box-select vertices (Vertex mode)");
  ImGui::TextDisabled("drag elem   move the selection in the plane facing the camera");
  ImGui::TextDisabled("shift+drag  move it up and down");
  ImGui::TextDisabled("drag face   move it along the face normal (Face mode)");
  ImGui::TextDisabled("alt         no grid snap");
  ImGui::TextDisabled("ctrl+A      select every vertex");
  ImGui::TextDisabled("esc         cancel the drag, drop the vertices, then the mesh");
  ImGui::TextDisabled("a move that bends a face is refused on release");
}

} // namespace client
