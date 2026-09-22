#include "path_tool.hpp"

#include "../../../shared/entities/entity_reflection.hpp"
#include "../../../shared/log.hpp"
#include "../../../shared/map_connection.hpp"
#include "../../../shared/map_geometry.hpp"
#include "../../../shared/movers.hpp"
#include "../../../shared/subtick.hpp"
#include "../../geometry_renderer.hpp"
#include "../../hud/announcement.hpp"
#include "../../renderer.hpp"
#include "../editor_object_bounds.hpp"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace client
{

namespace
{

constexpr float MARKER_PICK_RADIUS_IN_PIXELS = 14.0f;
constexpr float NODE_MARKER_RADIUS          = 6.0f;
constexpr float ACTIVE_NODE_MARKER_RADIUS   = 10.0f;
constexpr color_t NODE_GHOST_COLOR{90, 200, 220};

const entities::Path_Node_Entity* node_in_map(const shared::map_t& map, shared::entity_uid_t uid)
{
  const shared::map_entity_t* entry = map.find_by_uid(uid);
  return entry != nullptr ? entities::entity_as<entities::Path_Node_Entity>(entry->entity.get())
                          : nullptr;
}

const entities::Mover_Entity* mover_in_map(const shared::map_t& map, shared::entity_uid_t uid)
{
  const shared::map_entity_t* entry = map.find_by_uid(uid);
  return entry != nullptr ? entities::entity_as<entities::Mover_Entity>(entry->entity.get())
                          : nullptr;
}

std::optional<shared::aabb_bounds_t> rest_bounds_of_owned(const shared::map_t& map,
                                                          const std::vector<shared::entity_uid_t>& owned)
{
  std::optional<shared::aabb_bounds_t> bounds;
  for (shared::entity_uid_t uid : owned)
  {
    const shared::aabb_bounds_t piece = shared::compute_object_bounds(map, uid);
    bounds = bounds ? shared::union_aabb(*bounds, piece) : piece;
  }
  return bounds;
}

void draw_posed_box(pass_builder_t& draws, const shared::path_pose_t& rest_frame,
                    const shared::path_pose_t& pose, const shared::aabb_bounds_t& rest, color_t color)
{
  linalg::vec3 corners[8];
  for (uint32_t corner = 0; corner < 8; ++corner)
    corners[corner] = shared::apply_mover_pose(rest_frame, pose,
                                               {(corner & 1) ? rest.max.x : rest.min.x,
                                                (corner & 2) ? rest.max.y : rest.min.y,
                                                (corner & 4) ? rest.max.z : rest.min.z});
  for (uint32_t corner = 0; corner < 8; ++corner)
    for (uint32_t axis_bit : {1u, 2u, 4u})
      if ((corner & axis_bit) == 0)
        draws.debug.line(corners[corner], corners[corner | axis_bit], color);
}

std::string segment_label(const entities::Path_Node_Entity& node)
{
  std::string label = std::format("{:.2f}s", node.traversal_seconds);
  if (node.wait_seconds > 0.0f)
    label += std::format(" +{:.2f}s wait", node.wait_seconds);
  if (node.easing != entities::Easing::Linear)
    label += std::format(" {}", entities::to_string(node.easing));
  return label;
}

} // namespace

void Path_Tool::on_enable(editor_context_t& ctx)
{
  playing = false;
  if (!ctx.map)
    return;
  for (shared::entity_uid_t uid : ctx.selection_handed_over)
  {
    if (node_in_map(*ctx.map, uid) != nullptr || mover_in_map(*ctx.map, uid) != nullptr)
    {
      adopt(ctx, uid);
      break;
    }
  }
}

void Path_Tool::on_disable(editor_context_t& ctx)
{
  playing = false;
  if (gizmo.is_dragging())
  {
    gizmo.end_drag();
    commit_drag(ctx);
  }
  gizmo.clear_target();
  end_field_edit(ctx);
}

void Path_Tool::refresh(editor_context_t& ctx)
{
  rebuild_path_scratch(*ctx.map, scratch);

  if (node_in_map(*ctx.map, active_node) == nullptr)
    active_node = shared::null_entity_uid;
  const entities::Mover_Entity* mover = mover_in_map(*ctx.map, subject_mover);
  if (mover == nullptr)
    subject_mover = shared::null_entity_uid;

  const shared::entity_uid_t chain_node =
      active_node != shared::null_entity_uid ? active_node
      : mover != nullptr                     ? mover->follow.from
                                             : shared::null_entity_uid;
  chain = chain_through(scratch, chain_node);

  active_selection.clear();
  if (active_node != shared::null_entity_uid)
    active_selection.push_back(active_node);
  if (subject_mover != shared::null_entity_uid)
    active_selection.push_back(subject_mover);

  cycle_seconds = 0.0f;
  for (const entities::Mover_Entity& each : scratch.system.entities_of<entities::Mover_Entity>())
    cycle_seconds = std::max(cycle_seconds,
                             chain_cycle_seconds(chain_through(scratch, each.follow.from), ctx.tickrate));
}

float Path_Tool::chain_cycle_seconds(const std::vector<shared::entity_uid_t>& walk, float tickrate) const
{
  if (walk.size() < 2 || tickrate <= 0.0f)
    return 0.0f;

  const bool closed = chain_is_closed(scratch, walk);
  const size_t segment_count = closed ? walk.size() : walk.size() - 1;

  uint32_t ticks = 0;
  for (size_t index = 0; index < segment_count; ++index)
  {
    const std::optional<shared::path_segment_t> segment =
        shared::try_cut_path_segment(scratch.system, scratch.links, walk[index], 1, tickrate);
    if (segment)
      ticks += std::max(1u, segment->traversal_ticks + segment->wait_ticks);
  }
  return (float)(closed ? ticks : ticks * 2) / tickrate;
}

gizmo_view_t Path_Tool::make_gizmo_view() const
{
  const camera_t& camera = cached_viewport.camera;
  return {camera.position, get_orientation_vectors(camera).forward, camera.orthographic,
          camera.ortho_height, camera.fov_degrees};
}

shared::entity_uid_t Path_Tool::pick_marker(const editor_context_t& ctx) const
{
  const linalg::vec2i pixel = input::mouse_position();
  shared::entity_uid_t nearest = shared::null_entity_uid;
  float nearest_distance = MARKER_PICK_RADIUS_IN_PIXELS;

  for (const shared::map_entity_t& entry : ctx.map->entities)
  {
    if (!entry.entity || !ctx.object_is_visible(entry.uid))
      continue;
    if (entry.entity->type != entities::entity_type::Path_Node_Entity &&
        entry.entity->type != entities::entity_type::Mover_Entity)
      continue;

    const std::optional<linalg::vec2> screen = try_project_to_screen(cached_viewport, entry.entity->position);
    if (!screen)
      continue;
    const float dx = screen->x - (float)pixel.x;
    const float dy = screen->y - (float)pixel.y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance >= nearest_distance)
      continue;
    nearest_distance = distance;
    nearest = entry.uid;
  }
  return nearest;
}

shared::entity_uid_t Path_Tool::mover_under_ray(const editor_context_t& ctx) const
{
  if (!ctx.bvh || ctx.bvh->nodes.empty())
    return shared::null_entity_uid;

  ray_hit_result_t hit{};
  if (!bvh_intersect_ray(*ctx.bvh, cached_viewport.mouse_ray.origin, cached_viewport.mouse_ray.direction, hit))
    return shared::null_entity_uid;

  const shared::entity_uid_t uid = hit.id.index;
  if (mover_in_map(*ctx.map, uid) != nullptr)
    return uid;
  if (const shared::map_geometry_t* geometry = ctx.map->find_geometry_by_uid(uid))
  {
    const shared::entity_uid_t owner = shared::get_owner_uid(geometry->value);
    if (mover_in_map(*ctx.map, owner) != nullptr)
      return owner;
  }
  return shared::null_entity_uid;
}

void Path_Tool::adopt(editor_context_t& ctx, shared::entity_uid_t uid)
{
  if (node_in_map(*ctx.map, uid) != nullptr)
  {
    active_node = uid;
    return;
  }
  if (const entities::Mover_Entity* mover = mover_in_map(*ctx.map, uid))
  {
    subject_mover = uid;
    active_node = node_in_map(*ctx.map, mover->follow.from) != nullptr ? mover->follow.from
                                                                       : shared::null_entity_uid;
  }
}

void Path_Tool::on_update(editor_context_t& ctx, const viewport_state_t& view, float dt)
{
  cached_viewport = view;
  if (!ctx.map)
    return;

  refresh(ctx);

  if (playing)
  {
    preview_seconds += dt * playback_speed;
    if (cycle_seconds > 0.0f)
      preview_seconds = std::fmod(preview_seconds, cycle_seconds);
  }

  const float grid_step = ctx.grid ? ctx.grid->step() : editor::MAJOR_GRID_STEP;
  gizmo.snap_step = input::current_modifiers().alt ? 0.0f : grid_step;

  if (gizmo.is_dragging())
  {
    if (const std::optional<gizmo_drag_t> drag = gizmo.try_update_drag(view.mouse_ray, make_gizmo_view()))
      apply_drag(ctx, *drag);
    return;
  }

  if (active_node != shared::null_entity_uid)
  {
    gizmo.set_target(editor_object_bounds(*ctx.map, active_node), {.rotate = true, .reshape = false},
                     make_gizmo_view(), shared::try_get_object_position(*ctx.map, active_node));
    gizmo.update_hover(view.mouse_ray);
  }
  else
  {
    gizmo.clear_target();
  }

  hovered_uid = shared::null_entity_uid;
  placement_point.reset();
  if (gizmo.is_hovered() || input::imgui_wants_mouse())
    return;

  hovered_uid = pick_marker(ctx);
  if (hovered_uid == shared::null_entity_uid)
    hovered_uid = mover_under_ray(ctx);
  if (hovered_uid != shared::null_entity_uid)
    return;

  const entities::Path_Node_Entity* from = node_in_map(*ctx.map, active_node);
  if (from == nullptr || input::current_modifiers().shift)
  {
    placement_point = try_pick_placement_point(ctx, view);
    return;
  }

  float distance = 0.0f;
  if (!linalg::intersect_ray_plane(view.mouse_ray.origin, view.mouse_ray.direction, from->position,
                                   {0.0f, 1.0f, 0.0f}, distance) ||
      distance <= 0.0f)
    return;

  linalg::vec3 point = view.mouse_ray.origin + view.mouse_ray.direction * distance;
  if (!input::current_modifiers().alt)
  {
    point.x = editor::snap(point.x, grid_step);
    point.z = editor::snap(point.z, grid_step);
  }
  point.y = from->position.y;
  placement_point = point;
}

void Path_Tool::on_mouse_down(editor_context_t& ctx, const input::mouse_event_t& e)
{
  if (e.button != input::mouse_button_t::Left || !ctx.map)
    return;

  if (gizmo.try_begin_drag(cached_viewport.mouse_ray, make_gizmo_view()))
  {
    begin_drag(ctx);
    return;
  }

  if (hovered_uid != shared::null_entity_uid)
  {
    adopt(ctx, hovered_uid);
    return;
  }

  if (placement_point)
    place_node(ctx, *placement_point);
}

void Path_Tool::on_mouse_drag(editor_context_t& ctx, const input::mouse_event_t& e) {}

void Path_Tool::on_mouse_up(editor_context_t& ctx, const input::mouse_event_t& e)
{
  if (e.button != input::mouse_button_t::Left || !gizmo.is_dragging())
    return;
  gizmo.end_drag();
  commit_drag(ctx);
}

void Path_Tool::on_key_down(editor_context_t& ctx, const key_event_t& e)
{
  if (!ctx.map)
    return;

  if (e.key == input::key_t::Delete || e.key == input::key_t::Backspace)
  {
    delete_active_node(ctx);
    return;
  }
  if (e.key == input::key_t::Space && !e.mods.shift)
  {
    playing = !playing;
    return;
  }
  if (e.key == input::key_t::N)
  {
    active_node = shared::null_entity_uid;
    hud::set_announcement("The next click starts a new chain.");
  }
}

void Path_Tool::begin_drag(editor_context_t& ctx)
{
  drag_origins.clear();

  std::vector<shared::entity_uid_t> carried = {active_node};
  for (const auto [uid, mover] : ctx.map->entities_of_type<entities::Mover_Entity>())
  {
    if (mover->follow.from != active_node)
      continue;
    carried.push_back(uid);
    for (shared::entity_uid_t owned : geometry_owned_by(*ctx.map, uid))
      carried.push_back(owned);
  }

  for (shared::entity_uid_t uid : carried)
  {
    const std::optional<linalg::vec3> position = shared::try_get_object_position(*ctx.map, uid);
    if (!position)
      continue;

    drag_origin_t origin{.uid = uid,
                         .position = *position,
                         .orientation = shared::try_get_object_orientation(*ctx.map, uid)};
    if (const shared::map_geometry_t* geometry = ctx.map->find_geometry_by_uid(uid))
      origin.geometry_before = geometry->value;
    else if (const shared::map_entity_t* entry = ctx.map->find_by_uid(uid))
      origin.entity_before = snapshot_entity(entry->entity.get());
    drag_origins.push_back(std::move(origin));
  }
}

void Path_Tool::apply_drag(editor_context_t& ctx, const gizmo_drag_t& drag)
{
  for (const drag_origin_t& origin : drag_origins)
  {
    if (!shared::try_set_object_position(*ctx.map, origin.uid, origin.position + drag.translation))
      log_error("path tool: object {} vanished mid-drag", origin.uid);

    if (origin.orientation && !linalg::is_identity_rotation(drag.rotation))
      if (!shared::try_set_object_orientation(*ctx.map, origin.uid,
                                              linalg::rotate_model_in_world(*origin.orientation, drag.rotation)))
        log_error("path tool: object {} took a rotation it cannot store", origin.uid);
  }
}

void Path_Tool::commit_drag(editor_context_t& ctx)
{
  transaction_t transaction;
  for (const drag_origin_t& origin : drag_origins)
  {
    if (origin.geometry_before)
    {
      if (const shared::map_geometry_t* entry = ctx.map->find_geometry_by_uid(origin.uid))
        transaction.add_geometry_modified(origin.uid, *origin.geometry_before, entry->value);
      continue;
    }
    if (const shared::map_entity_t* entry = ctx.map->find_by_uid(origin.uid))
      transaction.add_modified_from_diff(origin.uid, origin.entity_before, entry->entity.get());
  }
  ctx.transaction_system.push(std::move(transaction));
  drag_origins.clear();

  if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
}

void Path_Tool::place_node(editor_context_t& ctx, const linalg::vec3& position)
{
  std::shared_ptr<entities::Entity> created = shared::make_entity(entities::entity_type::Path_Node_Entity);
  if (!created)
  {
    log_error("path tool: could not make a path node");
    return;
  }

  entities::Path_Node_Entity& node = static_cast<entities::Path_Node_Entity&>(*created);
  node.position = position;

  shared::map_entity_t* previous = ctx.map->find_by_uid(active_node);
  entities::Path_Node_Entity* previous_node =
      previous != nullptr ? entities::entity_as<entities::Path_Node_Entity>(previous->entity.get()) : nullptr;
  const entities::Mover_Entity* mover = mover_in_map(*ctx.map, subject_mover);

  if (previous_node != nullptr)
  {
    node.orientation = previous_node->orientation;
    node.next        = previous_node->next;
  }
  else if (mover != nullptr)
  {
    node.orientation = mover->orientation;
  }

  const shared::entity_uid_t uid = ctx.map->add_entity(created);
  transaction_t transaction;
  transaction.add_created(uid, snapshot_entity(created.get()));

  if (previous_node != nullptr)
  {
    const entity_snapshot_t before = snapshot_entity(previous_node);
    previous_node->next = uid;
    transaction.add_modified_from_diff(active_node, before, previous_node);
  }
  else if (mover != nullptr && node_in_map(*ctx.map, mover->follow.from) == nullptr)
  {
    shared::map_entity_t* mover_entry = ctx.map->find_by_uid(subject_mover);
    const entity_snapshot_t before = snapshot_entity(mover_entry->entity.get());
    static_cast<entities::Mover_Entity&>(*mover_entry->entity).follow.from = uid;
    transaction.add_modified_from_diff(subject_mover, before, mover_entry->entity.get());
  }

  ctx.transaction_system.push(std::move(transaction));
  active_node = uid;

  if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
}

void Path_Tool::delete_active_node(editor_context_t& ctx)
{
  shared::map_entity_t* entry = ctx.map->find_by_uid(active_node);
  if (entry == nullptr || !entry->entity)
    return;

  const shared::entity_uid_t removed = active_node;
  const shared::entity_uid_t previous = shared::previous_node_of(scratch.links, removed);
  const shared::entity_uid_t next = static_cast<const entities::Path_Node_Entity&>(*entry->entity).next;

  transaction_t transaction;
  relink_paths_around_removed_nodes(*ctx.map, Span<const shared::entity_uid_t>(&removed, 1), transaction);

  entry = ctx.map->find_by_uid(removed);
  transaction.add_removed(removed, snapshot_entity(entry->entity.get()));
  ctx.map->remove_entity(removed);

  std::vector<shared::connection_t> connections_before = ctx.map->connections;
  (void)shared::remove_connections_naming(ctx.map->connections, Span<const shared::entity_uid_t>(&removed, 1));
  transaction.add_map_connections_modified(std::move(connections_before), ctx.map->connections);
  ctx.transaction_system.push(std::move(transaction));

  active_node = previous != shared::null_entity_uid ? previous : next == removed ? shared::null_entity_uid : next;

  if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
}

void Path_Tool::start_mover_at_active_node(editor_context_t& ctx, shared::entity_uid_t mover)
{
  shared::map_entity_t* entry = ctx.map->find_by_uid(mover);
  if (entry == nullptr || node_in_map(*ctx.map, active_node) == nullptr)
    return;
  const entity_snapshot_t before = snapshot_entity(entry->entity.get());
  static_cast<entities::Mover_Entity&>(*entry->entity).follow.from = active_node;
  transaction_t transaction;
  transaction.add_modified_from_diff(mover, before, entry->entity.get());
  ctx.transaction_system.push(std::move(transaction));
}

void Path_Tool::begin_field_edit(const editor_context_t& ctx, shared::entity_uid_t uid)
{
  if (const shared::map_entity_t* entry = ctx.map->find_by_uid(uid))
    field_edit = field_edit_t{uid, snapshot_entity(entry->entity.get())};
}

void Path_Tool::end_field_edit(editor_context_t& ctx)
{
  if (!field_edit || !ctx.map)
  {
    field_edit.reset();
    return;
  }
  if (const shared::map_entity_t* entry = ctx.map->find_by_uid(field_edit->uid))
  {
    transaction_t transaction;
    transaction.add_modified_from_diff(field_edit->uid, field_edit->before, entry->entity.get());
    ctx.transaction_system.push(std::move(transaction));
  }
  field_edit.reset();
}

void Path_Tool::draw_mover_preview(editor_context_t& ctx, pass_builder_t& draws)
{
  const float tickrate = ctx.tickrate;
  const float preview_ticks = std::max(0.0f, preview_seconds * tickrate);
  const uint32_t tick = (uint32_t)preview_ticks;
  const float fraction = preview_ticks - (float)tick;

  for (const entities::Mover_Entity& mover : scratch.system.entities_of<entities::Mover_Entity>())
  {
    entities::Mover_Entity previewed = mover;
    previewed.follow.segment_start_tick = 0;
    previewed.follow.frozen_at_tick = 0;

    const shared::path_pose_t rest_frame = shared::mover_rest_frame(scratch.system, mover);
    const shared::path_pose_t pose = shared::blend_path_poses(
        shared::mover_pose_at(scratch.system, scratch.links, previewed, rest_frame, tick, tickrate),
        shared::mover_pose_at(scratch.system, scratch.links, previewed, rest_frame, tick + 1, tickrate),
        fraction);

    const std::vector<shared::entity_uid_t> owned = geometry_owned_by(*ctx.map, mover.entity_id);
    const std::optional<shared::aabb_bounds_t> rest = rest_bounds_of_owned(*ctx.map, owned);

    const bool follows_shown_chain =
        std::find(chain.begin(), chain.end(), mover.follow.from) != chain.end();
    if (rest && follows_shown_chain)
    {
      for (shared::entity_uid_t node_uid : chain)
      {
        const entities::Path_Node_Entity* node = scratch.system.get<entities::Path_Node_Entity>(node_uid);
        draw_posed_box(draws, rest_frame, {.position = node->position, .orientation = node->orientation},
                       *rest, NODE_GHOST_COLOR);
      }
    }

    const bool at_rest = linalg::length(pose.position - rest_frame.position) < 0.01f &&
                         std::abs(linalg::dot(pose.orientation, rest_frame.orientation)) > 1.0f - 1e-6f;
    if (!show_preview || at_rest)
      continue;

    draws.debug.wire_sphere(pose.position, NODE_MARKER_RADIUS, colors::magenta);
    const linalg::mat4f moved_by = shared::mover_model_matrix(rest_frame, pose);
    for (shared::entity_uid_t uid : owned)
      if (const shared::map_geometry_t* geometry = ctx.map->find_geometry_by_uid(uid))
        draw_geometry(draws, geometry->value, uid, ctx.map->materials, ctx.map->lightmap, &moved_by);
    if (rest)
      draw_posed_box(draws, rest_frame, pose, *rest, colors::magenta);
  }
}

void Path_Tool::on_draw_overlay(editor_context_t& ctx, pass_builder_t& draws)
{
  if (!ctx.map)
    return;

  for (const shared::entity_uid_t node_uid : chain)
  {
    const entities::Path_Node_Entity* node = scratch.system.get<entities::Path_Node_Entity>(node_uid);
    const bool active = node_uid == active_node;
    draws.debug.wire_sphere(node->position, active ? ACTIVE_NODE_MARKER_RADIUS : NODE_MARKER_RADIUS,
                            active ? colors::yellow : colors::green);

    if (const entities::Path_Node_Entity* next = scratch.system.get<entities::Path_Node_Entity>(node->next))
      draws.debug.backed_text((node->position + next->position) * 0.5f, segment_label(*node).c_str(), colors::white);
  }

  if (hovered_uid != shared::null_entity_uid)
    if (const std::optional<linalg::vec3> position = shared::try_get_object_position(*ctx.map, hovered_uid))
      draws.debug.wire_sphere(*position, ACTIVE_NODE_MARKER_RADIUS * 1.5f, colors::white);

  if (placement_point)
  {
    draws.debug.wire_sphere(*placement_point, NODE_MARKER_RADIUS, colors::yellow);
    if (const entities::Path_Node_Entity* from = node_in_map(*ctx.map, active_node))
      draws.debug.line(from->position, *placement_point, colors::yellow);
  }

  draw_mover_preview(ctx, draws);
  gizmo.draw(draws);
}

void Path_Tool::on_draw_ui(editor_context_t& ctx)
{
  if (!ctx.map)
    return;

  ImGui::SetNextWindowSize({340, 0}, ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Path"))
  {
    ImGui::End();
    return;
  }

  ImGui::TextDisabled("Click empty space: add a node after the active one,");
  ImGui::TextDisabled("  at its height. Shift: on the surface. Alt: no snap.");
  ImGui::TextDisabled("Click a node or mover: make it active.");
  ImGui::TextDisabled("N: start a new chain   Del: remove node   Space: play");

  ImGui::SeparatorText("Preview");
  ImGui::Checkbox("Show moving brushes", &show_preview);
  if (ImGui::Button(playing ? "Pause" : "Play"))
    playing = !playing;
  ImGui::SameLine();
  if (ImGui::Button("Rewind"))
    preview_seconds = 0.0f;
  ImGui::SliderFloat("Speed", &playback_speed, 0.1f, 4.0f, "%.2fx");
  if (cycle_seconds > 0.0f)
  {
    if (ImGui::SliderFloat("Time", &preview_seconds, 0.0f, cycle_seconds, "%.2f s"))
      playing = false;
    ImGui::TextDisabled("longest cycle: %.2f s", cycle_seconds);
  }
  else
  {
    ImGui::TextDisabled("No mover has a chain to follow.");
  }

  ImGui::SeparatorText("Chain");
  if (chain.empty())
  {
    ImGui::TextDisabled("No chain. Click in the world to place its first node.");
  }
  else
  {
    const bool closed = chain_is_closed(scratch, chain);
    ImGui::Text("%zu nodes, %s", chain.size(), closed ? "closed (loops)" : "open (oscillates)");

    for (size_t index = 0; index < chain.size(); ++index)
    {
      const shared::entity_uid_t uid = chain[index];
      shared::map_entity_t* entry = ctx.map->find_by_uid(uid);
      if (entry == nullptr)
        continue;
      entities::Path_Node_Entity& node = static_cast<entities::Path_Node_Entity&>(*entry->entity);

      ImGui::PushID((int)uid);
      if (ImGui::Selectable(shared::describe_map_entity(*ctx.map, uid).c_str(), uid == active_node))
        active_node = uid;

      const bool leaves_a_segment = closed || index + 1 < chain.size();
      if (leaves_a_segment)
      {
        ImGui::Indent();
        const auto edit = [&](bool changed)
        {
          if (ImGui::IsItemActivated())
            begin_field_edit(ctx, uid);
          if (ImGui::IsItemDeactivated())
            end_field_edit(ctx);
          return changed;
        };
        edit(ImGui::DragFloat("traversal", &node.traversal_seconds, 0.05f, 0.0f, 600.0f, "%.2f s"));
        edit(ImGui::DragFloat("wait", &node.wait_seconds, 0.05f, 0.0f, 600.0f, "%.2f s"));

        int easing = (int)node.easing;
        const char* easing_names[entities::Easing_COUNT];
        for (uint32_t i = 0; i < entities::Easing_COUNT; ++i)
          easing_names[i] = entities::to_string((entities::Easing)i);
        if (ImGui::Combo("easing", &easing, easing_names, IM_ARRAYSIZE(easing_names)))
        {
          begin_field_edit(ctx, uid);
          node.easing = (entities::Easing)easing;
          end_field_edit(ctx);
        }
        ImGui::Unindent();
      }
      else
      {
        ImGui::TextDisabled("  end of the chain: the mover turns back here");
      }
      ImGui::PopID();
    }
  }

  ImGui::SeparatorText("Movers");
  bool any_mover = false;
  for (const auto [uid, mover] : ctx.map->entities_of_type<entities::Mover_Entity>())
  {
    const bool on_chain = std::find(chain.begin(), chain.end(), mover->follow.from) != chain.end();
    if (!on_chain && uid != subject_mover)
      continue;
    any_mover = true;

    ImGui::PushID((int)uid);
    if (ImGui::Selectable(shared::describe_map_entity(*ctx.map, uid).c_str(), uid == subject_mover))
      adopt(ctx, uid);
    ImGui::Indent();
    ImGui::Text("starts at %s", shared::describe_map_entity(*ctx.map, mover->follow.from).c_str());
    ImGui::Text("%zu brushes tied", geometry_owned_by(*ctx.map, uid).size());

    if (active_node != shared::null_entity_uid && active_node != mover->follow.from &&
        ImGui::Button("Start from the active node"))
      start_mover_at_active_node(ctx, uid);
    ImGui::Unindent();
    ImGui::PopID();
  }
  if (!any_mover)
    ImGui::TextDisabled("No mover follows this chain.");

  const std::vector<shared::path_refusal_t> refusals = shared::validate_map_paths(*ctx.map);
  if (!refusals.empty())
  {
    ImGui::SeparatorText("Refused at load");
    for (const shared::path_refusal_t& refusal : refusals)
      ImGui::TextColored({1.0f, 0.3f, 0.3f, 1.0f}, "%s", refusal.reason.c_str());
  }

  ImGui::End();
}

std::optional<view_focus_t> Path_Tool::view_focus() const
{
  if (chain.empty())
    return std::nullopt;

  shared::aabb_bounds_t bounds{};
  bool first = true;
  for (shared::entity_uid_t uid : chain)
  {
    const entities::Path_Node_Entity* node = scratch.system.get<entities::Path_Node_Entity>(uid);
    if (node == nullptr)
      continue;
    if (first)
      bounds = {node->position, node->position};
    else
      shared::expand_aabb_to_include_point(bounds, node->position);
    first = false;
  }
  return view_focus_t{.center = (bounds.min + bounds.max) * 0.5f,
                      .radius = std::max(64.0f, linalg::length(bounds.max - bounds.min) * 0.5f)};
}

Span<const shared::entity_uid_t> Path_Tool::selected_objects() const
{
  return active_selection;
}

} // namespace client
