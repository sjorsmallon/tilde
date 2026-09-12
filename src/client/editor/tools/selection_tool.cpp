#include "../../../shared/entities/entity_reflection.hpp"
#include "selection_tool.hpp"
#include "../../hud/announcement.hpp"
#include "../../renderer.hpp"
#include "../connection_panel.hpp"
#include "../editor_bvh.hpp"
#include "../entity_editor_traits.hpp"
#include "../entity_inspector.hpp"
#include "../entity_outliner.hpp"
#include "../geometry_editor.hpp"
#include "../transaction_system.hpp"
#include "../../../shared/map_connection.hpp"
#include "../../../shared/map_fragment.hpp"
#include "../../../shared/map_group.hpp"
#include "../../../shared/lighting.hpp"
#include "../../../shared/lightmap.hpp"
#include "../../../shared/lightmap_lights.hpp"
#include "../../../shared/lightmap_reflections.hpp"
#include "../../../shared/shapes.hpp"
#include "../../../shared/log.hpp"
#include "../../../shared/shader_math.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <limits>
#include <string>

namespace client
{

namespace
{

// TWO radii, and the difference between them is the whole rule. An entity icon
// is a few pixels wide and usually stands in front of a wall, so a ray that hit
// the wall is not evidence the author meant the wall -- an entity CLOSE to the
// cursor therefore wins over a geometry hit. A merely NEARBY one wins only when
// the ray hit nothing at all: applying the generous radius over geometry would
// make a blockout floor unclickable wherever a lamp is parked on it.
constexpr float ENTITY_PICK_RADIUS_OVER_GEOMETRY   = 14.0f;
constexpr float ENTITY_PICK_RADIUS_IN_EMPTY_SPACE  = 40.0f;

// A press that never moved this far in pixels is a CLICK, whatever gesture it
// started. Squared, so no square root is taken to answer a yes/no.
constexpr int CLICK_MOVEMENT_THRESHOLD_SQUARED = 25;

// What the click would take, drawn before it is taken: a ring on the target, a
// leader line to it, and its label at the cursor. Built for the connection pick
// and shared with the ordinary hover, because a pick radius that is generous
// enough to be useful hits SOMETHING every time and the alternative is finding
// out which one afterwards.
void draw_cursor_target_marker(ImDrawList *overlay, ImVec2 mouse,
                               const std::optional<linalg::vec2> &target, const char *label,
                               ImU32 color)
{
  if (target)
  {
    const ImVec2 at{target->x, target->y};
    overlay->AddCircle(at, 10.0f, color, 0, 2.0f);
    overlay->AddLine(mouse, at, (color & 0x00FFFFFFu) | 0xA0000000u, 1.5f);
  }

  // A filled plate under the label: the viewport behind the cursor is whatever
  // the map is, and a coloured line over a lit wall is not readable.
  const ImVec2 text_at{mouse.x + 16.0f, mouse.y + 4.0f};
  const ImVec2 text_size = ImGui::CalcTextSize(label);
  const float  pad       = 4.0f;
  overlay->AddRectFilled(ImVec2(text_at.x - pad, text_at.y - pad),
                         ImVec2(text_at.x + text_size.x + pad, text_at.y + text_size.y + pad),
                         IM_COL32(0, 0, 0, 200), 3.0f);
  overlay->AddText(text_at, color, label);
}

// What the hover marker calls the thing under the cursor. Entities go through
// shared::describe_map_entity, which is the ONE spelling of an endpoint and is
// shared with the loader's refusals; geometry is never an endpoint, so naming a
// brush is this tool's own business and belongs here rather than in there.
std::string describe_hovered_object(const shared::map_t &map, shared::entity_uid_t uid)
{
  if (const shared::map_geometry_t *geometry = map.find_geometry_by_uid(uid))
    return std::format("{} uid {}", shared::get_kind_name(shared::get_kind(geometry->value)), uid);

  return shared::describe_map_entity(map, uid);
}

} // namespace


// Capture the pre-drag state of everything selected. Both regimes go into the
// same map keyed by uid, so the drag itself never asks which is which.
void Selection_Tool::capture_drag_snapshots(editor_context_t& ctx)
{
  drag_start_snapshots.clear();
  drag_origins.clear();
  if (!ctx.map)
    return;

  for (shared::entity_uid_t uid : selected_uids)
  {
    const std::optional<linalg::vec3> position = shared::try_get_object_position(*ctx.map, uid);
    if (!position)
      continue;

    drag_origins.push_back(
        {uid, *position,
         shared::try_get_object_orientation(*ctx.map, uid)
             .value_or(linalg::quatf::identity())});

    if (const shared::map_geometry_t *geometry = ctx.map->find_geometry_by_uid(uid))
    {
      drag_start_snapshots[uid].geometry = geometry->value;
      continue;
    }

    if (auto *entry = ctx.map->find_by_uid(uid); entry && entry->entity)
      drag_start_snapshots[uid].entity = snapshot_entity(entry->entity.get());
  }
}

// Push one transaction covering the whole drag, so Ctrl+Z undoes the move of all
// selected objects at once rather than one at a time.
void Selection_Tool::commit_drag_snapshots(editor_context_t& ctx)
{
  if (drag_start_snapshots.empty() || !ctx.map)
  {
    drag_start_snapshots.clear();
    return;
  }

  transaction_t transaction;
  for (const auto &[uid, snapshot] : drag_start_snapshots)
  {
    if (snapshot.geometry)
    {
      if (const shared::map_geometry_t *entry = ctx.map->find_geometry_by_uid(uid))
        transaction.add_geometry_modified(uid, *snapshot.geometry, entry->value);
      continue;
    }

    if (auto *entry = ctx.map->find_by_uid(uid); entry && entry->entity)
      transaction.add_modified_from_diff(uid, snapshot.entity, entry->entity.get());
  }
  ctx.transaction_system.push(std::move(transaction));
  drag_start_snapshots.clear();
  drag_origins.clear();
}

std::optional<shared::aabb_bounds_t>
Selection_Tool::try_compute_selection_bounds(editor_context_t& ctx) const
{
  if (selected_uids.empty() || !ctx.map)
    return std::nullopt;

  shared::aabb_bounds_t bounds = shared::compute_object_bounds(*ctx.map, selected_uids[0]);
  for (size_t index = 1; index < selected_uids.size(); ++index)
    bounds = shared::union_aabb(bounds,
                                shared::compute_object_bounds(*ctx.map, selected_uids[index]));
  return bounds;
}

gizmo_view_t Selection_Tool::make_gizmo_view() const
{
  const client::camera_t &camera = cached_viewport.camera;
  return {camera.position, client::get_orientation_vectors(camera).forward, camera.orthographic,
          camera.ortho_height, camera.fov_degrees};
}

// Apply what the gizmo reported to everything the drag started on. The gizmo
// itself writes nothing -- it does not know a map exists -- so this is the one
// place a gizmo drag reaches the world, and it goes through the same per-uid
// seam every other tool uses.
//
// It flags NO bvh rebuild: a live drag calls this every frame, and a rebuild
// decomposes every brush in the map -- 148 ms for one level-32 sculpted face,
// which is the whole frame. Nothing picks against the bvh while a drag holds
// the mouse, so both callers that end an edit flag it instead (on_mouse_up for
// the drag, apply_transform_as_one_edit for the discrete ones) -- the rule
// sculpting_tool already follows.
void Selection_Tool::apply_gizmo_drag(editor_context_t& ctx, const gizmo_drag_t &drag)
{
  if (!ctx.map)
    return;

  // A reshape names one whole box, and the handles are only offered when the
  // selection is a single object that has one, so it is written through rather
  // than distributed as a delta.
  if (drag.box && drag_origins.size() == 1)
  {
    if (!shared::try_set_object_box(*ctx.map, drag_origins[0].uid, *drag.box))
      log_error("selection tool: object {} took a reshape it cannot store",
                drag_origins[0].uid);
  }
  else
  {
    for (const drag_origin_t &origin : drag_origins)
    {
      if (!shared::try_set_object_position(*ctx.map, origin.uid,
                                           origin.position + drag.translation))
        log_error("selection tool: object {} vanished mid-drag", origin.uid);
    }
  }

  if (!linalg::is_identity_rotation(drag.rotation))
  {
    // Rotating ONE object spins it where it stands; rotating a GROUP turns the
    // arrangement. That is not an implementation accident -- they are different
    // operations, and a group of one is the first, not a degenerate second.
    // Orbiting a lone object about its own bounds centre would also shift any
    // entity whose box volume sits off its origin, which nobody asked for.
    const bool orbit = drag_origins.size() > 1;

    for (const drag_origin_t &origin : drag_origins)
    {
      if (orbit)
      {
        const linalg::vec3 orbited =
            drag.pivot + linalg::rotate(drag.rotation, origin.position - drag.pivot);
        if (!shared::try_set_object_position(*ctx.map, origin.uid, orbited))
          log_error("selection tool: object {} vanished mid-drag", origin.uid);
      }

      // An object with no orientation to store still travelled -- an
      // axis-aligned box orbits the pivot and stays axis-aligned, which is what
      // it IS. Writing a rotation onto one and drawing it unrotated is the lie
      // map_geometry.hpp deleted the field for, so this is not a failure.
      const std::optional<linalg::quatf> current =
          shared::try_get_object_orientation(*ctx.map, origin.uid);
      if (!current)
        continue;

      if (!shared::try_set_object_orientation(*ctx.map, origin.uid,
                                              linalg::rotate_model_in_world(origin.orientation, drag.rotation)))
        log_error("selection tool: object {} took a rotation it cannot store", origin.uid);
    }
  }
}

void Selection_Tool::apply_transform_as_one_edit(editor_context_t   &ctx,
                                                 const gizmo_drag_t &transform)
{
  capture_drag_snapshots(ctx);
  apply_gizmo_drag(ctx, transform);
  commit_drag_snapshots(ctx);

  if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
}

void Selection_Tool::snap_selection_to_surface_below(editor_context_t& ctx)
{
  if (selected_uids.empty() || !ctx.map || !ctx.bvh)
    return;

  const std::optional<shared::aabb_bounds_t> bounds = try_compute_selection_bounds(ctx);
  if (!bounds)
    return;

  // Far enough to cross any level, short enough that an object over a hole
  // stays where it is instead of leaving the map.
  constexpr float MAX_SNAP_DROP = 100000.0f;

  const std::optional<float> drop = try_drop_distance_to_surface_below(
      *ctx.bvh, *bounds, selected_uids, MAX_SNAP_DROP);

  if (!drop)
  {
    log_warning("selection tool: nothing under the selection to snap to");
    return;
  }

  if (*drop <= 0.001f)
    return;

  apply_transform_as_one_edit(ctx, {.translation = {0.0f, -*drop, 0.0f},
                                    .pivot = (bounds->min + bounds->max) * 0.5f});
}

void Selection_Tool::draw_multi_selection_panel(editor_context_t& ctx)
{
  const shared::aabb_bounds_t bounds = *try_compute_selection_bounds(ctx);
  const linalg::vec3          center = (bounds.min + bounds.max) * 0.5f;
  const linalg::vec3          size   = bounds.max - bounds.min;

  ImGui::Text("%zu objects selected", selected_uids.size());
  ImGui::Separator();
  ImGui::Text("Center  %.0f  %.0f  %.0f", center.x, center.y, center.z);
  ImGui::Text("Size    %.0f  %.0f  %.0f", size.x, size.y, size.z);
  ImGui::Separator();

  // Typed offset. The gizmo covers dragging; what it cannot do is an exact
  // number, which is the whole reason this half exists.
  ImGui::DragFloat3("Offset", &panel_offset.x, 1.0f);
  ImGui::BeginDisabled(panel_offset.x == 0.f && panel_offset.y == 0.f &&
                       panel_offset.z == 0.f);
  if (ImGui::Button("Apply offset"))
  {
    apply_transform_as_one_edit(ctx, {.translation = panel_offset, .pivot = center});
    panel_offset = {0, 0, 0};
  }
  ImGui::EndDisabled();

  ImGui::Separator();
  ImGui::TextUnformatted("Rotate 90 degrees about");

  // A quarter turn is the one angle that is exact for EVERY kind here: it maps
  // an axis-aligned box's arrangement onto the grid it came from, so nothing
  // lands off-grid and no object has to store an orientation it does not have.
  constexpr const char *AXIS_LABELS[3] = {"X", "Y", "Z"};
  for (int axis = 0; axis < 3; ++axis)
  {
    if (axis > 0)
      ImGui::SameLine();

    ImGui::PushID(axis);
    if (ImGui::Button(AXIS_LABELS[axis]))
    {
      gizmo_drag_t turn;
      turn.rotation = linalg::rotation_delta_from_axis_angle(axis, 90.f);
      turn.pivot          = center;
      apply_transform_as_one_edit(ctx, turn);
    }
    ImGui::PopID();
  }

  ImGui::Separator();
  if (ImGui::Button("Snap all to grid"))
  {
    // Per-object, so this is not a single delta and does not go through
    // apply_gizmo_drag -- every object rounds to its own nearest cell.
    const float step = ctx.grid ? ctx.grid->step() : editor::MAJOR_GRID_STEP;

    capture_drag_snapshots(ctx);
    for (const drag_origin_t &origin : drag_origins)
    {
      const linalg::vec3 snapped = {editor::snap(origin.position.x, step),
                                    editor::snap(origin.position.y, step),
                                    editor::snap(origin.position.z, step)};
      if (!shared::try_set_object_position(*ctx.map, origin.uid, snapped))
        log_error("selection tool: object {} vanished before it could be snapped", origin.uid);
    }
    commit_drag_snapshots(ctx);

    if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
      *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
  }
}

// The one thing this popup is FOR: a row that crosses the boundary of the
// selection is a row the prefab silently loses, in either direction. The same
// walk decides what the file keeps, so the warning and the file cannot
// disagree -- and Ctrl+C shows the same count, because the clipboard drops them
// for the same reason.
void Selection_Tool::draw_prefab_save_popup(editor_context_t& ctx)
{
  if (!ImGui::BeginPopupModal("Save as prefab", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    return;

  if (!ctx.map || selected_uids.empty())
  {
    ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
    return;
  }

  ImGui::Text("%zu object%s", selected_uids.size(), selected_uids.size() == 1 ? "" : "s");
  ImGui::InputText("Name", prefab_name.data, prefab_name.size());

  const std::string name = prefab_name.data;
  const std::string path =
      std::string(shared::PREFAB_DIRECTORY) + "/" + name + shared::PREFAB_EXTENSION;

  const bool name_is_usable = !name.empty() &&
                              name.find('/') == std::string::npos &&
                              name.find('\\') == std::string::npos &&
                              name.find(':') == std::string::npos;

  if (!name.empty() && !name_is_usable)
    ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f),
                       "A prefab name is a filename, not a path.");

  const bool already_exists =
      name_is_usable && std::filesystem::exists(std::filesystem::path(path));
  if (already_exists)
  {
    ImGui::TextColored(ImVec4(1.f, 0.8f, 0.3f, 1.f), "%s already exists.", path.c_str());
    ImGui::Checkbox("Overwrite it", &prefab_overwrite);
  }

  const std::vector<shared::crossing_connection_t> crossings =
      shared::find_crossing_connections(*ctx.map, selected_uids);

  if (!crossings.empty())
  {
    size_t kept = 0;
    for (const shared::crossing_connection_t &crossing : crossings)
      kept += crossing.kept_as_unbound ? 1 : 0;
    const size_t lost = crossings.size() - kept;

    ImGui::Separator();
    ImGui::TextWrapped("The selected entities have %zu connection(s) to entities you did not "
                       "select:",
                       crossings.size());

    for (const shared::crossing_connection_t &crossing : crossings)
    {
      if (crossing.index >= ctx.map->connections.size())
        continue;
      const shared::connection_t &row = ctx.map->connections[crossing.index];

      const std::string sender = shared::describe_map_entity(*ctx.map, row.sender);
      const std::string target = row.target_kind == shared::connection_target_t::Uid
                                     ? shared::describe_map_entity(*ctx.map, row.target)
                                     : std::string(shared::to_string(row.target_kind));

      const ImVec4 colour = crossing.kept_as_unbound ? ImVec4(1.f, 0.85f, 0.5f, 1.f)
                                                     : ImVec4(1.f, 0.6f, 0.6f, 1.f);
      ImGui::TextColored(colour, "  %s --%s--> %s", sender.c_str(),
                         entities::to_string(row.signal), target.c_str());
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("You have two options:");
    if (kept > 0)
      ImGui::TextWrapped("1) Continue as is. The %zu connection(s) FROM a selected entity are "
                         "stored with an \"Unbound\" target, which you are expected to resolve "
                         "when placing the prefab: after each placement the editor asks you to "
                         "click the target.",
                         kept);
    if (lost > 0)
      ImGui::TextWrapped("%s Continue as is. The %zu connection(s) INTO a selected entity are "
                         "NOT saved: the prefab has no sender to keep them on.",
                         kept > 0 ? "  " : "1)", lost);
    ImGui::TextWrapped("2) Include the entities you did not select into your selection, so the "
                       "prefab carries them and their connections stay complete.");

    // Repeat until the list is empty and the group is wiring-complete. One
    // press adds one ring of neighbours, which is what makes it predictable.
    if (ImGui::Button("Include those entities in the selection"))
    {
      for (const shared::crossing_connection_t &crossing : crossings)
      {
        if (crossing.outside_uid == shared::null_entity_uid ||
            !ctx.map->has_object(crossing.outside_uid))
          continue;
        if (std::find(selected_uids.begin(), selected_uids.end(), crossing.outside_uid) ==
            selected_uids.end())
          selected_uids.push_back(crossing.outside_uid);
      }
    }
  }

  ImGui::Separator();

  const bool can_write = name_is_usable && (!already_exists || prefab_overwrite);
  ImGui::BeginDisabled(!can_write);
  if (ImGui::Button(crossings.empty() ? "Save" : "Continue as is and save"))
  {
    shared::map_t fragment = shared::extract_map_subset(*ctx.map, selected_uids);
    fragment.name          = name + shared::PREFAB_EXTENSION;

    std::error_code directory_error;
    std::filesystem::create_directories(shared::PREFAB_DIRECTORY, directory_error);

    if (shared::save_map(path, fragment))
    {
      prefab_status = std::format("saved {} ({} objects, {} connections)", path,
                                  fragment.object_count(), fragment.connections.size());
      hud::set_announcement(prefab_status);
      ImGui::CloseCurrentPopup();
    }
    else
    {
      prefab_status = std::format("FAILED to write {}", path);
      log_error("selection_tool: could not write prefab \"{}\"", path);
    }
  }
  ImGui::EndDisabled();

  ImGui::SameLine();
  if (ImGui::Button("Cancel"))
    ImGui::CloseCurrentPopup();

  ImGui::EndPopup();
}

// --- Clipboard and paste -----------------------------------------------------

void Selection_Tool::copy_selection_to_clipboard(editor_context_t& ctx)
{
  if (!ctx.map || selected_uids.empty())
    return;

  // The whole copy, wiring included. extract_map_subset rebases the members so
  // the group's anchor -- bottom-centre of its bounds -- is the fragment's
  // origin, which is what the cursor then drives at paste time.
  shared::map_t fragment = shared::extract_map_subset(*ctx.map, selected_uids);
  if (fragment.object_count() == 0)
  {
    log_warning("selection_tool: nothing in the selection could be copied");
    return;
  }

  const size_t copied      = fragment.object_count();
  const size_t connections = fragment.connections.size();

  size_t lost = 0;
  for (const shared::crossing_connection_t &crossing :
       shared::find_crossing_connections(*ctx.map, selected_uids))
    lost += crossing.kept_as_unbound ? 0 : 1;

  adopt_clipboard(std::move(fragment), lost);
  clipboard_group_name.clear();

  if (clipboard_crossing_count > 0)
    hud::set_announcement(std::format("copied {} object(s), {} connection(s) -- {} more cross "
                                      "the selection and were NOT copied",
                                      copied, connections, clipboard_crossing_count));
  else
    hud::set_announcement(
        std::format("copied {} object(s), {} connection(s)", copied, connections));
}

void Selection_Tool::adopt_clipboard(shared::map_t fragment, size_t crossing_count)
{
  clipboard_crossing_count = crossing_count;

  clipboard_brush_hulls.clear();
  clipboard_brush_hulls.reserve(fragment.geometry.size());
  for (const shared::map_geometry_t &entry : fragment.geometry)
  {
    const shared::brush_geometry_t *brush = std::get_if<shared::brush_geometry_t>(&entry.value);
    clipboard_brush_hulls.push_back(brush ? shared::try_build_brush_polyhedron(brush->hull_points)
                                          : std::nullopt);
  }

  // The fragment's own low corner, which is already relative to its anchor.
  // Paste puts THAT corner on a grid line, the rule
  // compute_geometry_placement_center already follows for a single object.
  bool                  any = false;
  shared::aabb_bounds_t bounds{{0, 0, 0}, {0, 0, 0}};
  for (const shared::map_entity_t &entry : fragment.entities)
  {
    const shared::aabb_bounds_t object = shared::compute_object_bounds(fragment, entry.uid);
    bounds                             = any ? shared::union_aabb(bounds, object) : object;
    any                                = true;
  }
  for (const shared::map_geometry_t &entry : fragment.geometry)
  {
    const shared::aabb_bounds_t object = shared::compute_object_bounds(fragment, entry.uid);
    bounds                             = any ? shared::union_aabb(bounds, object) : object;
    any                                = true;
  }
  clipboard_low_corner_offset = bounds.min;

  clipboard = std::move(fragment);
}

void Selection_Tool::begin_paste()
{
  if (!clipboard || clipboard->object_count() == 0)
  {
    log_warning("selection_tool: nothing on the clipboard to paste");
    return;
  }

  paste_is_pending = true;
  // Nothing is placeable until on_update has aimed the cursor at a surface.
  paste_anchor_valid = false;
}

void Selection_Tool::cancel_paste()
{
  // The clipboard deliberately survives: cancelling says "not there", not
  // "forget what I copied".
  paste_is_pending   = false;
  paste_anchor_valid = false;
}

void Selection_Tool::commit_paste(editor_context_t& ctx)
{
  if (!paste_is_pending || !paste_anchor_valid || !clipboard || !ctx.map)
    return;

  // Captured BEFORE the stamp: the wiring is a whole-list diff, so the baseline
  // has to be what the map held a moment ago rather than what it holds now.
  std::vector<shared::connection_t> connections_before = ctx.map->connections;
  std::vector<shared::map_group_t>  groups_before      = ctx.map->groups;

  const shared::stamp_result_t stamped = shared::stamp_map(*ctx.map, *clipboard, paste_anchor);
  if (stamped.uids.empty())
  {
    log_error("selection_tool: the clipboard would not paste");
    cancel_paste();
    return;
  }

  // A placed prefab is ONE group, so a click grabs the whole stamp. Any group
  // the fragment carried inside it is pulled into this one -- a member belongs
  // to one group, and the stamp is the one the author placed.
  if (!clipboard_group_name.empty())
    (void)shared::group_objects(*ctx.map, stamped.uids, clipboard_group_name);

  transaction_t transaction;
  for (shared::entity_uid_t uid : stamped.uids)
  {
    if (const shared::map_geometry_t *geometry = ctx.map->find_geometry_by_uid(uid))
    {
      transaction.add_geometry_created(uid, geometry->value);
      continue;
    }

    const shared::map_entity_t *entry = ctx.map->find_by_uid(uid);
    if (entry && entry->entity)
      transaction.add_created(uid, snapshot_entity(entry->entity.get()));
  }
  transaction.add_map_connections_modified(std::move(connections_before), ctx.map->connections);
  transaction.add_map_groups_modified(std::move(groups_before), ctx.map->groups);

  // One transaction for the whole paste, so Ctrl+Z takes all of it back at once
  // -- the objects AND the wiring between them -- the same rule the
  // multi-object delete follows.
  ctx.transaction_system.push(std::move(transaction));

  // The copies become the selection: what you just placed is what the gizmo and
  // the arrow keys should be aimed at.
  selected_uids = stamped.uids;
  hovered_uid   = 0;
  cancel_paste();

  if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;

  // A prefab's slots. Every unbound row this stamp placed is queued, in map
  // order, and the first group is armed right away: placing a prefab is stamp,
  // click the target.
  connection_pick.disarm();
  for (size_t index = 0; index < ctx.map->connections.size(); ++index)
  {
    const shared::connection_t &row = ctx.map->connections[index];
    if (row.target_kind != shared::connection_target_t::Unbound)
      continue;
    if (std::find(stamped.uids.begin(), stamped.uids.end(), row.sender) == stamped.uids.end())
      continue;
    connection_pick.queued_rows.push_back(index);
  }
  arm_next_unbound_pick(ctx);
}

void Selection_Tool::group_selection(editor_context_t& ctx)
{
  if (!ctx.map)
    return;
  if (selected_uids.size() < 2)
  {
    hud::set_announcement("Select at least two objects to group");
    return;
  }

  std::vector<shared::map_group_t> before = ctx.map->groups;
  const shared::entity_uid_t       group_uid = shared::group_objects(
      *ctx.map, selected_uids, std::format("group {}", ctx.map->next_uid));
  if (group_uid == shared::null_entity_uid)
    return;

  transaction_t transaction;
  transaction.add_map_groups_modified(std::move(before), ctx.map->groups);
  ctx.transaction_system.push(std::move(transaction));

  hud::set_announcement(std::format("Grouped {} objects", selected_uids.size()));
}

void Selection_Tool::ungroup_selection(editor_context_t& ctx)
{
  if (!ctx.map)
    return;

  std::vector<shared::entity_uid_t> group_uids;
  for (shared::entity_uid_t selected : selected_uids)
    if (const shared::map_group_t *group = shared::find_group_of(*ctx.map, selected))
      if (std::find(group_uids.begin(), group_uids.end(), group->uid) == group_uids.end())
        group_uids.push_back(group->uid);

  if (group_uids.empty())
  {
    hud::set_announcement("Nothing selected is grouped");
    return;
  }

  std::vector<shared::map_group_t> before = ctx.map->groups;
  for (shared::entity_uid_t group_uid : group_uids)
    (void)shared::ungroup(*ctx.map, group_uid);

  transaction_t transaction;
  transaction.add_map_groups_modified(std::move(before), ctx.map->groups);
  ctx.transaction_system.push(std::move(transaction));

  hud::set_announcement(group_uids.size() == 1
                            ? std::string("Ungrouped")
                            : std::format("Ungrouped {} groups", group_uids.size()));
}

void Selection_Tool::ungroup_by_uid(editor_context_t& ctx, shared::entity_uid_t group_uid)
{
  if (!ctx.map)
    return;

  std::vector<shared::map_group_t> before = ctx.map->groups;
  if (!shared::ungroup(*ctx.map, group_uid))
  {
    log_warning("selection_tool: no group has uid {}", group_uid);
    return;
  }

  transaction_t transaction;
  transaction.add_map_groups_modified(std::move(before), ctx.map->groups);
  ctx.transaction_system.push(std::move(transaction));
}

void Selection_Tool::select_group(editor_context_t& ctx, shared::entity_uid_t group_uid)
{
  selected_uids.clear();
  if (!ctx.map)
    return;
  const shared::map_group_t *group = shared::find_group_by_uid(*ctx.map, group_uid);
  if (group == nullptr)
  {
    log_warning("selection_tool: no group has uid {}", group_uid);
    return;
  }
  for (shared::entity_uid_t member : group->members)
    if (ctx.map->has_object(member))
      selected_uids.push_back(member);
}

void Selection_Tool::arm_next_unbound_pick(editor_context_t& ctx)
{
  if (!ctx.map)
  {
    connection_pick.disarm();
    return;
  }

  // The first queued row that is STILL unbound leads the group; a row the
  // author already filled through the panel, or undid away, is skipped.
  std::optional<size_t> lead;
  while (!connection_pick.queued_rows.empty())
  {
    const size_t index = connection_pick.queued_rows.front();
    connection_pick.queued_rows.erase(connection_pick.queued_rows.begin());
    if (index < ctx.map->connections.size() &&
        ctx.map->connections[index].target_kind == shared::connection_target_t::Unbound)
    {
      lead = index;
      break;
    }
  }
  if (!lead)
  {
    connection_pick.disarm();
    return;
  }

  const shared::connection_t &row = ctx.map->connections[*lead];

  // The key is the uid the prefab's rows named in the map they were saved from,
  // so rows sharing it aimed at ONE entity and take one click.
  connection_pick.also_rows.clear();
  std::vector<size_t> still_queued;
  for (size_t index : connection_pick.queued_rows)
  {
    const shared::connection_t &other = ctx.map->connections[index];
    if (other.target_kind == shared::connection_target_t::Unbound && other.target == row.target)
      connection_pick.also_rows.push_back(index);
    else
      still_queued.push_back(index);
  }
  connection_pick.queued_rows = std::move(still_queued);

  connection_pick.armed = true;
  connection_pick.row   = *lead;

  // The panel is drawn for ONE selected entity and disarms the pick otherwise,
  // so the group's sender has to be the selection.
  selected_uids = {row.sender};
  editor_gizmo.clear_target();

  const size_t fills = 1 + connection_pick.also_rows.size();
  hud::set_announcement(std::format(
      "Waiting to set a connection:\n{} {} ? : {}{}\n{} more to "
      "pick after this one. Esc leaves them unbound.",
      shared::describe_map_entity(*ctx.map, row.sender), entities::to_string(row.signal),
      entities::to_string(row.data.tag),
      fills > 1 ? std::format(" (and {} more row(s) aimed at the same thing)", fills - 1)
                : std::string(),
      connection_pick.queued_rows.size()));
}

std::optional<shared::entity_uid_t>
Selection_Tool::try_pick_entity_near_cursor(const editor_context_t &ctx,
                                            linalg::vec2 cursor) const
{
  if (!ctx.map)
    return std::nullopt;

  // A ray that actually hit an ENTITY is not a guess, so it beats proximity
  // outright. hovered_uid can name geometry, which find_by_uid answers null for
  // -- and a connection endpoint is always an entity, so geometry under the
  // cursor is not a competing answer here the way it is for a selection.
  if (hovered_uid != 0 && ctx.map->find_by_uid(hovered_uid) != nullptr)
    return hovered_uid;

  return try_pick_entity_within(ctx, cursor, ENTITY_PICK_RADIUS_IN_EMPTY_SPACE);
}

std::optional<shared::entity_uid_t>
Selection_Tool::try_pick_entity_within(const editor_context_t &ctx, linalg::vec2 cursor,
                                       float radius) const
{
  if (!ctx.map)
    return std::nullopt;

  std::optional<shared::entity_uid_t> nearest;
  float                               nearest_distance = radius;

  for (const shared::map_entity_t &candidate : ctx.map->entities)
  {
    if (!candidate.entity || !ctx.object_is_visible(candidate.uid))
      continue;

    const std::optional<linalg::vec2> screen =
        try_project_to_screen(cached_viewport, candidate.entity->position);
    if (!screen)
      continue;

    const float dx       = screen->x - cursor.x;
    const float dy       = screen->y - cursor.y;
    const float distance = std::sqrt(dx * dx + dy * dy);
    if (distance >= nearest_distance)
      continue;

    nearest_distance = distance;
    nearest          = candidate.uid;
  }

  return nearest;
}

void Selection_Tool::on_enable(editor_context_t& ctx)
{
  hovered_uid = 0;
  selected_uids.clear();
  editor_gizmo.clear_target();
  connection_pick.disarm();
  click_consumed_by_gesture = false;
}

void Selection_Tool::on_disable(editor_context_t& ctx)
{
  hovered_uid = 0;
  editor_gizmo.clear_target();
  cancel_paste();
  connection_pick.disarm();
  click_consumed_by_gesture = false;
}

void Selection_Tool::on_draw_ui(editor_context_t& ctx)
{
  if (is_dragging_box)
  {
    ImDrawList *draw_list = ImGui::GetForegroundDrawList();
    ImVec2 mouse_pos = ImGui::GetMousePos();
    drag_current_position.x = (int)mouse_pos.x;
    drag_current_position.y = (int)mouse_pos.y;

    int dx = drag_current_position.x - drag_start_position.x;
    int dy = drag_current_position.y - drag_start_position.y;

    if (dx * dx + dy * dy > 25)
    { // 5px threshold
      ImVec2 p1 = ImVec2((float)drag_start_position.x, (float)drag_start_position.y);
      ImVec2 p2 = mouse_pos;
      draw_list->AddRect(p1, p2, IM_COL32(0, 255, 0, 255));
      draw_list->AddRectFilled(p1, p2, IM_COL32(0, 255, 0, 50));
    }
  }

  // Inspector — geometry gets its handwritten panel, entities the schema-driven
  // one, and a multi-selection gets the transform panel in the same window. One
  // object's fields are not a thing a group HAS, so the window shows what a
  // group does have instead of showing nothing.
  if (!selected_uids.empty() && ctx.map)
  {
    if (ImGui::Begin("Entity Inspector", nullptr, ImGuiWindowFlags_NoFocusOnAppearing))
    {
      const shared::entity_uid_t uid = selected_uids[0];

      // The uid is what every log line, every bake table and every .source
      // block names an object by, so it is the first thing the panel says.
      if (selected_uids.size() > 1)
        ImGui::Text("%zu objects selected, first uid %u", selected_uids.size(), uid);
      else
        ImGui::Text("uid %u", uid);

      if (ImGui::Button("Snap to surface below (End)"))
        snap_selection_to_surface_below(ctx);

      // Groups. One line naming the group a single object is in, or how many
      // groups a selection touches, and the two edits beside it.
      {
        std::vector<shared::entity_uid_t> touched;
        for (shared::entity_uid_t selected : selected_uids)
          if (const shared::map_group_t *group = shared::find_group_of(*ctx.map, selected))
            if (std::find(touched.begin(), touched.end(), group->uid) == touched.end())
              touched.push_back(group->uid);

        if (touched.size() == 1)
        {
          const shared::map_group_t *group = shared::find_group_by_uid(*ctx.map, touched[0]);
          ImGui::Text("Group \"%s\" (uid %u, %zu members)", group->name.c_str(), group->uid,
                      group->members.size());
        }
        else if (touched.size() > 1)
        {
          ImGui::Text("%zu groups in this selection", touched.size());
        }

        if (selected_uids.size() > 1)
        {
          if (ImGui::Button("Group (Ctrl+G)"))
            group_selection(ctx);
          if (!touched.empty())
            ImGui::SameLine();
        }
        if (!touched.empty() && ImGui::Button("Ungroup (Ctrl+Shift+G)"))
          ungroup_selection(ctx);

        // The members, one row each, a click narrowing the selection to that
        // one: the outliner's pick-inside-a-group, in the panel already on
        // screen. Lists the whole group when the selection sits in exactly one,
        // so a single grouped object shows its siblings; the selection itself
        // otherwise. Goes through requested_selection like the outliner does,
        // since selected_uids is being read by this very frame's panel.
        std::vector<shared::entity_uid_t> members;
        if (touched.size() == 1)
          shared::expand_to_group(*ctx.map, selected_uids[0], members);
        else
          members = selected_uids;

        if (members.size() > 1)
        {
          ImGui::Separator();
          ImGui::Text("Members (%zu)", members.size());
          const float row_height = ImGui::GetTextLineHeightWithSpacing();
          const float list_height =
              row_height * static_cast<float>(std::min<size_t>(members.size(), 8)) +
              ImGui::GetStyle().FramePadding.y * 2.f;
          if (ImGui::BeginListBox("##members", ImVec2(-FLT_MIN, list_height)))
          {
            for (shared::entity_uid_t member : members)
            {
              const bool is_selected =
                  std::find(selected_uids.begin(), selected_uids.end(), member) !=
                  selected_uids.end();
              ImGui::PushID(static_cast<int>(member));
              if (ImGui::Selectable(object_label(*ctx.map, member).c_str(), is_selected))
                ctx.requested_selection = member;
              ImGui::PopID();
            }
            ImGui::EndListBox();
          }
        }
      }
      ImGui::Separator();

      if (selected_uids.size() > 1)
      {
        draw_multi_selection_panel(ctx);
      }
      else if (shared::map_geometry_t *geometry = ctx.map->find_geometry_by_uid(uid))
      {
        // Editing through the inspector is a series of single-frame edits, and
        // ImGui reports "changed" per frame of a drag, so pushing a transaction
        // here would flood the undo stack with one entry per frame. The BVH does
        // need rebuilding though — bounds just moved.
        //
        // TODO(inspector-undo): bracket a slider drag with
        // ImGui::IsItemActivated / IsItemDeactivatedAfterEdit and run it
        // through capture_drag_snapshots / commit_drag_snapshots, the way the
        // gizmo and the panel buttons already do, so it commits as one
        // transaction. Pre-existing gap: the entity inspector never pushed
        // transactions either.
        if (draw_geometry_inspector(geometry->value,
                                    ctx.object_collides(geometry->uid)))
        {
          if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
            *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
        }
      }
      else if (auto *entry = ctx.map->find_by_uid(uid); entry && entry->entity)
      {
        draw_light_bake_status(ctx, uid, *entry->entity);
        draw_reflection_volume_status(ctx, *entry->entity);
        render_entity_fields_in_an_imgui_window(entry->entity.get());
      }

      ImGui::Separator();
      if (ImGui::Button("Save as prefab..."))
      {
        prefab_overwrite = false;
        prefab_status.clear();
        ImGui::OpenPopup("Save as prefab");
      }
      if (!prefab_status.empty())
        ImGui::TextUnformatted(prefab_status.c_str());

      draw_prefab_save_popup(ctx);
    }
    ImGui::End();
  }

  // The wiring gets a window of its own rather than a header under the fields:
  // a trigger's field list is long, and a panel you have to scroll past the
  // fields to reach is a panel that is easy to forget exists. It follows the
  // selection, and only a SINGLE entity has wiring -- a group is not a sender.
  // What the click would actually take, drawn before it is taken. A generous
  // radius without this is a different kind of finicky -- it hits SOMETHING every
  // time and you find out which afterwards. The label is the same spelling the
  // panel and the loader's refusals use.
  if (connection_pick.armed && ctx.map)
  {
    const ImVec2 mouse = ImGui::GetMousePos();
    const std::optional<shared::entity_uid_t> candidate =
        try_pick_entity_near_cursor(ctx, {mouse.x, mouse.y});

    ImDrawList *overlay = ImGui::GetForegroundDrawList();
    if (candidate)
    {
      const shared::map_entity_t *entry = ctx.map->find_by_uid(*candidate);
      const std::optional<linalg::vec2> screen =
          entry && entry->entity
              ? try_project_to_screen(cached_viewport, entry->entity->position)
              : std::nullopt;

      const std::string label = shared::describe_map_entity(*ctx.map, *candidate);
      draw_cursor_target_marker(overlay, mouse, screen, label.c_str(),
                                IM_COL32(140, 220, 255, 255));
    }
    else
    {
      draw_cursor_target_marker(overlay, mouse, std::nullopt, "no entity near the cursor",
                                IM_COL32(255, 140, 140, 255));
    }
  }

  // The same marker for an ordinary hover. It is what makes the two pick radii
  // above legible: the ring says WHICH object the click resolved to, so a
  // proximity hit on a light in front of a wall is visibly a choice rather than
  // a surprise. Not drawn while another gesture owns the cursor -- a paste
  // preview, a drag or an armed pick each already say what the click will do.
  if (ctx.map && hovered_uid != 0 && !connection_pick.armed && !paste_is_pending &&
      !is_dragging_box && !is_dragging_object && !editor_gizmo.is_dragging())
  {
    const std::optional<linalg::vec3> anchor =
        shared::try_get_object_position(*ctx.map, hovered_uid);
    const std::optional<linalg::vec2> screen =
        anchor ? try_project_to_screen(cached_viewport, *anchor) : std::nullopt;

    const std::string label = describe_hovered_object(*ctx.map, hovered_uid);
    draw_cursor_target_marker(ImGui::GetForegroundDrawList(), ImGui::GetMousePos(), screen,
                              label.c_str(), IM_COL32(255, 226, 120, 255));
  }

  if (ctx.map && selected_uids.size() == 1)
  {
    draw_connection_panel(*ctx.map, selected_uids[0], ctx.transaction_system,
                          connection_pick);
  }
  else
  {
    connection_pick.disarm();
  }

}

// Whether the map's bake has anything for this light, said where the author is
// looking. A Baked light draws only through the chart slots that kept it, so
// "no slot" and "kept by no chart" are both a light that lights nothing, and
// neither is visible from the viewport or the field list.
void Selection_Tool::draw_light_bake_status(const editor_context_t& ctx,
                                            shared::entity_uid_t uid,
                                            const entities::Entity& entity)
{
  const std::optional<shared::scene_light_t> light = shared::try_light_of(entity);
  if (!light)
    return;

  const shared::lightmap_t& lightmap = ctx.map->lightmap;
  const ImVec4 warning_color{1.f, 0.55f, 0.2f, 1.f};

  // Said out loud rather than by drawing nothing: a Disable through a connection
  // takes this light out of every frame and out of the bake, and a panel that
  // simply went blank would read as a broken light rather than a switched one.
  if (!shared::light_is_switched_on(entity))
    ImGui::TextColored(warning_color,
                       "Switched OFF: contributes to no frame and to no bake until Enable.");

  // What the light DELIVERS, in the numbers every shader sums (radiance times
  // the arrival's attenuation), so an intensity is judged against what it does
  // and not against another light's knob: a point light's intensity is its
  // irradiance at one metre and falls off with the square of the distance, a
  // directional light's is its irradiance everywhere. A white Lambert face adds
  // that over pi to its colour before exposure -- a blockout face's fixed fake
  // sun (mesh_grid.frag) adds 0.85, which is the scale to read it against.
  {
    const float peak = std::max({light->radiance.x, light->radiance.y, light->radiance.z});
    if (light->kind == shared::light_kind_t::Directional)
    {
      ImGui::Text("Delivers irradiance %.3f to every face facing it (a white Lambert face adds %.3f).",
                  peak, peak / linalg::PI);
      ImGui::TextDisabled("No falloff: a point light of this intensity delivers the same at exactly "
                          "1 m. To match a spot's centre at d metres, use its intensity / d^2. At "
                          "the default exposure a white face saturates near 3.");
    }
    else
    {
      const auto irradiance_at = [&](float distance) {
        return peak * shared::shader_math::distance_attenuation(distance * distance, light->range,
                                                                light->source_radius);
      };
      ImGui::Text("Delivers irradiance %.3f at 1 m (%.0f units), %.3f at 100 units, %.3f at 200, "
                  "%.4f at 400 (a white Lambert face adds that over pi).",
                  irradiance_at(shared::LIGHT_REFERENCE_DISTANCE), shared::LIGHT_REFERENCE_DISTANCE,
                  irradiance_at(100.f), irradiance_at(200.f), irradiance_at(400.f));
    }
  }

  // The four gates a face has to clear, counted per face, as the map is NOW.
  // Kept beside the bake status because "kept by no chart" says what happened
  // and this says why.
  if (ImGui::Button("Explain what this light reaches"))
  {
    light_reach_uid = uid;
    light_reach_lines.clear();

    std::vector<shared::light_reach_on_face_t> reach = shared::probe_light_reach(
        *ctx.map, {uid, *light}, lightmap.settings, 0.25f, 100000.f, 24);

    // Faces it gets to first, then by how close it came.
    std::sort(reach.begin(), reach.end(),
              [](const shared::light_reach_on_face_t& a, const shared::light_reach_on_face_t& b) {
                if ((a.visible > 0) != (b.visible > 0)) return a.visible > 0;
                if ((a.arrives > 0) != (b.arrives > 0)) return a.arrives > 0;
                return a.nearest_distance < b.nearest_distance;
              });

    int total_visible = 0;
    for (const shared::light_reach_on_face_t& face : reach) total_visible += face.visible;
    light_reach_lines.push_back(
        std::format("{} faces sampled; {} samples lit in total. range {:.0f}{}",
                    reach.size(), total_visible, light->range,
                    light->kind == shared::light_kind_t::Spot
                        ? std::format(", cone outer cos {:.3f}", light->cos_outer)
                        : std::string()));

    constexpr size_t MAX_LINES = 16;
    for (size_t index = 0; index < reach.size() && index < MAX_LINES; ++index)
    {
      const shared::light_reach_on_face_t& face = reach[index];
      std::string why;
      if (face.visible > 0)
        why = "LIT";
      else if (face.reaches > 0)
        why = "occluded on every sample that arrives";
      else if (face.arrives > 0)
        why = "faces away from the light";
      else if (face.nearest_distance > light->range)
        why = "out of range";
      else if (light->kind == shared::light_kind_t::Spot && face.best_cone_cos < light->cos_outer)
        why = "outside the cone";
      else
        why = "attenuation reached zero";

      light_reach_lines.push_back(std::format(
          "obj {} n({:+.0f},{:+.0f},{:+.0f}): {}/{} arrive, {} face it, {} lit; nearest {:.0f}{} -- {}",
          face.object_uid, face.normal.x, face.normal.y, face.normal.z, face.arrives,
          face.sampled, face.reaches, face.visible, face.nearest_distance,
          light->kind == shared::light_kind_t::Spot
              ? std::format(", best cone cos {:.3f}", face.best_cone_cos)
              : std::string(),
          why));
    }
    if (reach.size() > MAX_LINES)
      light_reach_lines.push_back(std::format("...and {} more faces", reach.size() - MAX_LINES));
  }

  if (light_reach_uid == uid)
    for (const std::string& line : light_reach_lines) ImGui::TextWrapped("%s", line.c_str());

  if (!shared::light_is_baked(light->mode))
  {
    ImGui::TextDisabled("Dynamic: not in the bake, shaded analytically everywhere.");
    ImGui::Separator();
    return;
  }

  if (lightmap.charts.empty())
  {
    ImGui::TextColored(warning_color,
                       "This map has no bake; a Baked light lights NOTHING, whatever its intensity.");
    ImGui::Separator();
    return;
  }

  const int16_t slot = shared::find_baked_light_slot(lightmap, uid);
  if (slot == shared::LIGHTMAP_NO_LIGHT_SLOT)
  {
    ImGui::TextColored(warning_color,
                       "Not in the bake (placed or switched since it ran): it lights NOTHING until "
                       "you rebake, whatever its intensity.");
    ImGui::Separator();
    return;
  }

  const size_t kept_by = shared::count_charts_keeping_light(lightmap, slot);
  if (kept_by == 0)
    ImGui::TextColored(warning_color,
                       "Bake slot %d, kept by NO chart: it reached no face when baked. "
                       "Check range, cone and occluders, then rebake.",
                       (int)slot);
  else
    ImGui::Text("Bake slot %d, kept by %zu of %zu charts.", (int)slot, kept_by,
                lightmap.charts.size());
  ImGui::Separator();
}

// Gate 6 step 6. A volume replaces the measured parallax box of every capture
// whose lattice point lies inside it, and nothing in the viewport says whether
// that is one capture, four, or none -- a volume between two lattice points
// overrides nothing and is a placement mistake nothing else reports.
void Selection_Tool::draw_reflection_volume_status(const editor_context_t& ctx,
                                                   const entities::Entity& entity)
{
  const entities::Reflection_Volume_Entity* volume =
      entities::entity_as<entities::Reflection_Volume_Entity>(&entity);
  if (!volume)
    return;

  const shared::reflection_capture_set_t& set = ctx.map->lightmap.reflections;
  const ImVec4 warning_color{1.f, 0.55f, 0.2f, 1.f};

  if (set.empty())
  {
    ImGui::TextColored(warning_color,
                       "This map's bake carries no reflection captures: the volume overrides "
                       "nothing until one is baked with \"Bake reflection captures\" on.");
    ImGui::Separator();
    return;
  }

  const shared::reflection_volume_coverage_t coverage = shared::reflection_volume_coverage_of(
      set, shared::get_bounds(volume->volume, volume->position));
  if (coverage.covered == 0)
    ImGui::TextColored(warning_color,
                       "Covers NO capture of %zu at %.0f unit spacing: no lattice point lies "
                       "inside it, so it overrides nothing. Enlarge it past a lattice point or "
                       "lower the capture spacing, then rebake.",
                       set.captures.size(), set.spacing);
  else if (coverage.overridden_as_placed == coverage.covered)
    ImGui::Text("Overrides the parallax box of %zu of %zu captures.", coverage.covered,
                set.captures.size());
  else
    ImGui::TextColored(warning_color,
                       "Covers %zu of %zu captures, but the bake holds this box for only %zu: "
                       "moved or resized since it ran. Rebake.",
                       coverage.covered, set.captures.size(), coverage.overridden_as_placed);
  ImGui::Separator();
}

void Selection_Tool::on_update(editor_context_t& ctx,
                               const viewport_state_t &view, float /*dt*/)
{
  cached_viewport = view;

  // Somebody outside the tools asked for a selection -- the Map Info connection
  // list clicking a row. Consumed here because this tool owns the selection.
  if (ctx.requested_selection)
  {
    selected_uids.clear();
    if (ctx.map && ctx.map->has_object(*ctx.requested_selection))
      selected_uids.push_back(*ctx.requested_selection);
    ctx.requested_selection.reset();
  }

  // A prefab picked in the Placement tool. It arrives as a fragment already
  // anchored at its origin -- which is what the clipboard holds -- so placing
  // one is the paste gesture and nothing else.
  if (ctx.requested_paste)
  {
    adopt_clipboard(std::move(*ctx.requested_paste), 0);
    ctx.requested_paste.reset();
    clipboard_group_name = std::move(ctx.requested_paste_group_name);
    ctx.requested_paste_group_name.clear();
    begin_paste();
  }

  // The outliner's group rows.
  if (ctx.requested_group_selection)
  {
    select_group(ctx, *ctx.requested_group_selection);
    ctx.requested_group_selection.reset();
  }
  if (ctx.requested_group_of_selection)
  {
    ctx.requested_group_of_selection = false;
    group_selection(ctx);
  }
  if (ctx.requested_ungroup)
  {
    ungroup_by_uid(ctx, *ctx.requested_ungroup);
    ctx.requested_ungroup.reset();
  }

  // Hiding the selected thing drops it: a gizmo on something invisible is a
  // handle to nothing, and its inspector describes a thing you cannot see.
  std::erase_if(selected_uids, [&](shared::entity_uid_t uid)
                { return !ctx.object_is_visible(uid); });

  // A pending paste owns the cursor: no hover, no gizmo, no box drag, because
  // every one of those wants the same LMB that commits the placement.
  if (paste_is_pending)
  {
    editor_gizmo.clear_target();
    hovered_uid      = 0;
    grid_hover_valid = false;

    const std::optional<linalg::vec3> point = try_pick_placement_point(ctx, view);
    paste_anchor_valid                      = point.has_value();
    if (!point)
      return;

    paste_anchor = *point;

    // Put the GROUP's low corner on a grid line and carry the anchor with it, so
    // a pasted arrangement lands where the brush tool would snap a vertex and
    // every member keeps the offset it was copied with.
    const float step = ctx.grid ? ctx.grid->step() : 0.0f;
    if (step > 0.0f)
    {
      const linalg::vec3 low = paste_anchor + clipboard_low_corner_offset;
      paste_anchor = paste_anchor + linalg::vec3{editor::snap(low.x, step) - low.x,
                                                 editor::snap(low.y, step) - low.y,
                                                 editor::snap(low.z, step) - low.z};
    }
    return;
  }

  const float grid_step  = ctx.grid ? ctx.grid->step() : editor::MAJOR_GRID_STEP;
  editor_gizmo.snap_step = input::current_modifiers().alt ? 0.0f : grid_step;

  const gizmo_view_t gizmo_view = make_gizmo_view();

  if (editor_gizmo.is_dragging())
  {
    if (const std::optional<gizmo_drag_t> drag =
            editor_gizmo.try_update_drag(view.mouse_ray, gizmo_view))
      apply_gizmo_drag(ctx, *drag);
    return;
  }

  if (!ctx.map)
  {
    editor_gizmo.clear_target();
    return;
  }

  // Point the gizmo at the selection, however many objects that is.
  if (selected_uids.empty())
  {
    editor_gizmo.clear_target();
  }
  else if (selected_uids.size() == 1)
  {
    // BOTH capabilities come from the map seam rather than from a type test
    // here: an object with no editable box cannot be reshaped, one with no
    // orientation cannot be rotated, and those are exactly the two questions
    // try_get_object_box / try_get_object_orientation already answer. So this
    // tool has no entity-vs-geometry branch in it at all.
    const shared::entity_uid_t          uid = selected_uids[0];
    const std::optional<shared::aabb_t> editable_box =
        shared::try_get_object_box(*ctx.map, uid);

    gizmo_capabilities_t capabilities;
    capabilities.reshape = editable_box.has_value();
    capabilities.rotate  = shared::try_get_object_orientation(*ctx.map, uid).has_value();

    // The reshape handles have to sit on the box the drag will write back, not
    // on derived render bounds, or the first frame of a drag would jump.
    // Snapping measures against what the map STORES, not against the bounds the
    // handles sit on -- those are the same point for a box and 36 units apart
    // for a feet-origin spawn.
    editor_gizmo.set_target(editable_box ? shared::get_bounds(*editable_box)
                                         : shared::compute_object_bounds(*ctx.map, uid),
                            capabilities, gizmo_view,
                            shared::try_get_object_position(*ctx.map, uid));
    editor_gizmo.update_hover(view.mouse_ray);
  }
  else
  {
    // A group sits at the centre of everything in it, and the two capabilities
    // answer differently than they do for one object:
    //
    // RESHAPE IS OFF. Scaling a group means scaling each member about the
    // pivot, and the three regimes disagree about what that even means -- a
    // box has half-extents, a static mesh takes its size from its asset, a
    // brush would have to move every vertex. One handle cannot promise that.
    //
    // ROTATE IS ON, and it means something different: the ARRANGEMENT turns.
    // Positions orbit the pivot exactly, whatever the object is, and an
    // object's own orientation only changes if it has one to change.
    // No snap origin: a group snaps its MOVEMENT, so every member that was on
    // the grid stays on it. One absolute origin could only align one of them.
    editor_gizmo.set_target(*try_compute_selection_bounds(ctx), {.rotate = true, .reshape = false},
                            gizmo_view, std::nullopt);
    editor_gizmo.update_hover(view.mouse_ray);
  }

  if (!is_dragging_box)
  {
    hovered_uid = 0;

    if (editor_gizmo.is_hovered())
    {
      grid_hover_valid = false;
      return;
    }

    bool hit_bvh = false;

    if (ctx.bvh)
    {
      ray_hit_result_t hit;
      if (bvh_intersect_ray(*ctx.bvh, view.mouse_ray.origin, view.mouse_ray.direction,
                            hit))
      {
        if (hit.id.type == Collision_Id::Type::Static_Geometry)
        {
          shared::entity_uid_t uid = hit.id.index;
          if (ctx.map->has_object(uid) && ctx.object_is_visible(uid))
          {
            hovered_uid = uid;
            hit_bvh = true;
          }
        }
      }
    }

    // An entity near the cursor outranks the ray's answer -- see the two radii
    // above. Asked here, once, so the yellow highlight, the marker and the
    // click all resolve to the same object rather than to three guesses.
    if (!input::imgui_wants_mouse() && ctx.map &&
        (hovered_uid == 0 || ctx.map->find_by_uid(hovered_uid) == nullptr))
    {
      const linalg::vec2i pixel = input::mouse_position();
      const float         radius =
          hovered_uid == 0 ? ENTITY_PICK_RADIUS_IN_EMPTY_SPACE : ENTITY_PICK_RADIUS_OVER_GEOMETRY;

      if (const std::optional<shared::entity_uid_t> nearby =
              try_pick_entity_within(ctx, {(float)pixel.x, (float)pixel.y}, radius))
      {
        hovered_uid = *nearby;
        hit_bvh     = true;
      }
    }

    if (!hit_bvh)
    {
      linalg::vec3 plane_point = {0, -2.0f, 0};
      linalg::vec3 plane_normal = {0, 1.0f, 0};
      float t = 0.0f;
      if (linalg::intersect_ray_plane(view.mouse_ray.origin, view.mouse_ray.direction,
                                      plane_point, plane_normal, t))
      {
        grid_hover_position = view.mouse_ray.origin + view.mouse_ray.direction * t;
        float step = ctx.grid ? ctx.grid->step() : editor::MAJOR_GRID_STEP;
        grid_hover_position.x = editor::snap(grid_hover_position.x, step);
        grid_hover_position.z = editor::snap(grid_hover_position.z, step);
        grid_hover_valid = true;
      }
      else
      {
        grid_hover_valid = false;
      }
    }
    else
    {
      grid_hover_valid = false;
    }
  }
}

void Selection_Tool::on_mouse_down(editor_context_t& ctx,
                                   const input::mouse_event_t &e)
{
  if (e.button == input::mouse_button_t::Left)
  {
    // Before anything else, because every branch below either selects or drags,
    // and a pick must do neither.
    if (connection_pick.armed && ctx.map)
    {
      click_consumed_by_gesture = true;
      connection_pick.armed    = false;

      const std::optional<shared::entity_uid_t> target = try_pick_entity_near_cursor(
          ctx, {(float)e.position.x, (float)e.position.y});
      if (target)
      {
        std::vector<size_t> rows;
        rows.push_back(connection_pick.row);
        rows.insert(rows.end(), connection_pick.also_rows.begin(),
                    connection_pick.also_rows.end());
        commit_picked_connection_target(*ctx.map, ctx.transaction_system, rows, *target);
        connection_pick.also_rows.clear();
        arm_next_unbound_pick(ctx);
      }
      else
      {
        hud::set_announcement("No entity near the cursor — the pick is cancelled.");
        connection_pick.disarm();
      }
      return;
    }

    if (paste_is_pending)
    {
      // Swallowed like the connection pick: commit_paste selects what it
      // stamped, and this click's release would land on nothing and clear it.
      click_consumed_by_gesture = true;
      commit_paste(ctx);
      return;
    }

    if (editor_gizmo.try_begin_drag(cached_viewport.mouse_ray, make_gizmo_view()))
    {
      capture_drag_snapshots(ctx);
      return;
    }

    // Ctrl+LMB: move selected objects in the camera's view plane. Recorded
    // here as well because on_mouse_up measures against it to tell this gesture
    // from a ctrl+CLICK, which adds to the selection instead.
    if (e.mods.ctrl && !selected_uids.empty() && ctx.map)
    {
      drag_start_position   = e.position;
      drag_current_position = e.position;
      capture_drag_snapshots(ctx);

      if (!drag_origins.empty())
      {
        linalg::vec3 center = {0, 0, 0};
        for (const drag_origin_t &origin : drag_origins)
          center = center + origin.position;
        center = center * (1.0f / (float)drag_origins.size());

        auto basis = client::get_orientation_vectors(cached_viewport.camera);
        drag_plane_normal = basis.forward;

        float t = 0.0f;
        if (linalg::intersect_ray_plane(cached_viewport.mouse_ray.origin,
                                        cached_viewport.mouse_ray.direction,
                                        center, drag_plane_normal, t) && t > 0)
        {
          drag_plane_hit_start = cached_viewport.mouse_ray.origin +
                                 cached_viewport.mouse_ray.direction * t;
          is_dragging_object = true;
          return;
        }

        drag_start_snapshots.clear();
        drag_origins.clear();
      }
    }

    is_dragging_box = false;
    drag_start_position = e.position;
    drag_current_position = e.position;

    ImVec2 m = ImGui::GetMousePos();
    if (std::abs(m.x - e.position.x) < 20 && std::abs(m.y - e.position.y) < 20)
    {
    }
    else
    {
      drag_start_position = {(int)m.x, (int)m.y};
      drag_current_position = drag_start_position;
    }

    is_dragging_box = true;

  }
}

void Selection_Tool::on_mouse_drag(editor_context_t& ctx,
                                   const input::mouse_event_t &e)
{
  if (is_dragging_object && !drag_origins.empty() && ctx.map)
  {
    // Use the first object's start position as the plane reference point
    linalg::vec3 plane_point = drag_origins[0].position;
    float t = 0.0f;
    if (linalg::intersect_ray_plane(cached_viewport.mouse_ray.origin,
                                    cached_viewport.mouse_ray.direction,
                                    plane_point, drag_plane_normal, t) && t > 0)
    {
      linalg::vec3 current_hit = cached_viewport.mouse_ray.origin +
                                 cached_viewport.mouse_ray.direction * t;
      linalg::vec3 delta = current_hit - drag_plane_hit_start;

      const float grid_step = ctx.grid ? ctx.grid->step() : editor::MAJOR_GRID_STEP;
      const float snap_step = e.mods.alt ? 0.0f : grid_step;

      // The SAME rule the gizmo snaps by (Editor_Gizmo::set_target): one object
      // lands ITSELF on the grid, a group snaps its movement so every member
      // that was aligned stays aligned. Two drag styles for the same objects
      // reading the grid differently is indistinguishable from the grid itself
      // misbehaving -- which is exactly how it was reported.
      const bool single = drag_origins.size() == 1;
      for (int axis = 0; axis < 3; ++axis)
      {
        if (!single)
        {
          delta[axis] = editor::snap(delta[axis], snap_step);
          continue;
        }

        const float start = drag_origins[0].position[axis];
        delta[axis] = editor::snap(start + delta[axis], snap_step) - start;
      }

      for (const drag_origin_t &origin : drag_origins)
      {
        if (!shared::try_set_object_position(*ctx.map, origin.uid, origin.position + delta))
          log_error("selection tool: object {} vanished mid-drag", origin.uid);
      }
    }
    return;
  }

  if (is_dragging_box)
  {
    drag_current_position = e.position;
  }
}

void Selection_Tool::on_mouse_up(editor_context_t& ctx, const input::mouse_event_t &e)
{
  if (e.button == input::mouse_button_t::Left)
  {
    // The press was a gesture -- a target pick or a paste commit -- so the
    // release is the other half of it and must not fall through to the
    // selection branch below.
    if (click_consumed_by_gesture)
    {
      click_consumed_by_gesture = false;
      is_dragging_box          = false;
      return;
    }

    // Both drag styles end the same way, because both went through the same
    // snapshot: one transaction covering every object the drag touched.
    if (editor_gizmo.is_dragging())
    {
      editor_gizmo.end_drag();
      commit_drag_snapshots(ctx);
      if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
        *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
      return;
    }

    if (is_dragging_object)
    {
      is_dragging_object = false;
      commit_drag_snapshots(ctx);
      if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
        *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;

      // Ctrl+DRAG moves the selection in the view plane; ctrl+CLICK adds to it.
      // The two are one gesture until the mouse moves, so a press that never
      // did falls through to the selection branch below -- the commit above is
      // a no-op when nothing moved, so nothing lands on the undo stack.
      const int drag_dx = e.position.x - drag_start_position.x;
      const int drag_dy = e.position.y - drag_start_position.y;
      if (drag_dx * drag_dx + drag_dy * drag_dy > CLICK_MOVEMENT_THRESHOLD_SQUARED)
        return;
    }

    is_dragging_box = false;

    int dx = e.position.x - drag_start_position.x;
    int dy = e.position.y - drag_start_position.y;
    bool moved_significantly = (dx * dx + dy * dy) > CLICK_MOVEMENT_THRESHOLD_SQUARED;

    if (moved_significantly)
    {
      if (!ctx.map)
        return;

      int x_min = std::min(drag_start_position.x, drag_current_position.x);
      int x_max = std::max(drag_start_position.x, drag_current_position.x);
      int y_min = std::min(drag_start_position.y, drag_current_position.y);
      int y_max = std::max(drag_start_position.y, drag_current_position.y);

      if (!e.mods.shift && !e.mods.ctrl)
      {
        selected_uids.clear();
      }

      const auto &view = cached_viewport;

      for (const auto &[uid, bounds] : shared::collect_object_bounds(*ctx.map))
      {
        if (!ctx.object_is_visible(uid))
          continue;

        // The object's own anchor, not the bound's middle: a spectate spot's
        // bound is its frustum, whose centre is 36 units out in front of it.
        linalg::vec3 p = shared::try_get_object_position(*ctx.map, uid)
                             .value_or((bounds.min + bounds.max) * 0.5f);

        const std::optional<linalg::vec2> screen_pos = try_project_to_screen(view, p);
        if (!screen_pos)
          continue;

        // A member inside the box brings its group, whole: a box and a click
        // agree on what a grouped object is. Appends without duplicates.
        if (screen_pos->x >= x_min && screen_pos->x <= x_max &&
            screen_pos->y >= y_min && screen_pos->y <= y_max)
          shared::expand_to_group(*ctx.map, uid, selected_uids);
      }
    }
    else
    {
      if (hovered_uid != 0)
      {
        // What the click takes: the group's live members when the object has
        // one, the object alone when it does not. The BVH answered a uid and
        // the group is applied to that answer, nowhere earlier.
        std::vector<shared::entity_uid_t> picked;
        shared::expand_to_group(*ctx.map, hovered_uid, picked);

        const auto is_selected = [&](shared::entity_uid_t uid)
        { return std::find(selected_uids.begin(), selected_uids.end(), uid) != selected_uids.end(); };

        // Ctrl and shift both mean ADD, and mean it identically: two spellings
        // of one gesture, because every other editor binds one or the other and
        // nobody should have to find out which this one chose. On a group it
        // toggles the group: all in means out, otherwise the rest come in.
        if (e.mods.shift || e.mods.ctrl)
        {
          const bool all_selected = std::all_of(picked.begin(), picked.end(), is_selected);
          if (all_selected)
          {
            std::erase_if(selected_uids, [&](shared::entity_uid_t uid)
                          { return std::find(picked.begin(), picked.end(), uid) != picked.end(); });
          }
          else
          {
            for (shared::entity_uid_t uid : picked)
              if (!is_selected(uid))
                selected_uids.push_back(uid);
          }
        }
        // CLICK THROUGH: a plain click on a member of the group that IS the
        // selection narrows to that member. First click the group, second
        // click the thing -- the gesture for picking inside a group, with no
        // modifier to find, since both of them already mean add.
        else if (picked.size() > 1 && shared::uid_sets_equal(selected_uids, picked))
        {
          selected_uids = {hovered_uid};
        }
        else
        {
          selected_uids = std::move(picked);
        }
      }
      else
      {
        if (!e.mods.shift && !e.mods.ctrl)
        {
          selected_uids.clear();
        }
      }
    }
  }
}

void Selection_Tool::on_key_down(editor_context_t& ctx, const key_event_t &e)
{
  if (e.key == input::key_t::C && e.mods.ctrl)
  {
    copy_selection_to_clipboard(ctx);
    return;
  }

  if (e.key == input::key_t::V && e.mods.ctrl)
  {
    begin_paste();
    return;
  }

  if (e.key == input::key_t::G && e.mods.ctrl)
  {
    if (e.mods.shift)
      ungroup_selection(ctx);
    else
      group_selection(ctx);
    return;
  }

  // Escape is the cancel, not a right click: only LMB reaches a tool at all, and
  // RMB is the camera's -- an RMB click is an orbit drag that happened not to
  // move, so cancelling on it would fire every time you stopped looking around.
  if (e.key == input::key_t::End)
  {
    snap_selection_to_surface_below(ctx);
    return;
  }

  if (e.key == input::key_t::Escape)
  {
    if (!connection_pick.queued_rows.empty())
      hud::set_announcement(std::format("{} connection(s) left unbound.\nThey are red in the "
                                        "Connections panel, where the Pick button still "
                                        "reaches them.",
                                        connection_pick.queued_rows.size() +
                                            (connection_pick.armed ? 1u : 0u)));
    connection_pick.disarm();
    cancel_paste();
    return;
  }

  if (e.key == input::key_t::Delete || e.key == input::key_t::Backspace)
  {
    if (!selected_uids.empty() && ctx.map)
    {
      // One transaction for the whole selection, so Ctrl+Z brings back every
      // deleted object at once.
      transaction_t transaction;
      for (auto uid : selected_uids)
      {
        if (const shared::map_geometry_t *geometry = ctx.map->find_geometry_by_uid(uid))
        {
          transaction.add_geometry_removed(uid, geometry->value);
          ctx.map->remove_geometry(uid);
          continue;
        }

        if (auto *entry = ctx.map->find_by_uid(uid); entry && entry->entity)
        {
          transaction.add_removed(uid, snapshot_entity(entry->entity.get()));
          ctx.map->remove_entity(uid);
        }
      }
      ctx.transaction_system.push(std::move(transaction));

      if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
        *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
    }
    selected_uids.clear();
    hovered_uid = 0;
  }
}

void Selection_Tool::on_draw_overlay(editor_context_t& ctx,
                                     pass_builder_t &draws)
{
  if (!ctx.map)
    return;

  // The paste preview is the placement tool's ghost, deliberately: a wireframe
  // is what "not placed yet" already looks like in this editor, and the
  // pulsating outline below already means "selected".
  if (paste_is_pending && paste_anchor_valid)
  {
    // The fragment's members are already anchored at its origin, so the whole
    // preview is one translation by the cursor's anchor.
    for (size_t index = 0; clipboard && index < clipboard->geometry.size(); ++index)
    {
      const shared::map_geometry_t &entry = clipboard->geometry[index];

      if (index < clipboard_brush_hulls.size() && clipboard_brush_hulls[index])
      {
        // Traced from the hull captured at copy time. draw_geometry_ghost
        // would rebuild it every frame.
        draw_brush_hull_wireframe(draws, *clipboard_brush_hulls[index], paste_anchor,
                                  colors::yellow, 0.0f);
        continue;
      }

      draw_geometry_ghost(entry.value, draws, paste_anchor + shared::get_position(entry.value));
    }

    if (clipboard)
    {
      for (const shared::map_entity_t &entry : clipboard->entities)
      {
        if (!entry.entity)
          continue;
        const linalg::vec3 position = paste_anchor + entry.entity->position;
        if (!draw_entity_ghost(entry.entity.get(), draws, position))
          draw_default_ghost(entry.entity.get(), draws, position);
      }
    }
  }

  // Hover / box-select preview only ever needs the bound, so it works off the
  // uniform bounds accessor and doesn't care which regime an object is in.
  auto draw_bounds_highlight = [&](shared::entity_uid_t uid, color_t color)
  {
    const shared::aabb_bounds_t bounds = shared::compute_object_bounds(*ctx.map, uid);
    draws.debug.box((bounds.min + bounds.max) * 0.5f,
                           (bounds.max - bounds.min) * 0.5f, color);
  };

  // 1. Draw selected items with pulsating pink/white wireframe
  float grid_step = ctx.grid ? ctx.grid->step() : editor::MAJOR_GRID_STEP;
  for (auto uid : selected_uids)
  {
    if (const shared::map_geometry_t *geometry = ctx.map->find_geometry_by_uid(uid))
      draw_geometry_selection_highlight(geometry->value, draws, ctx.time, grid_step);
    else if (auto *entry = ctx.map->find_by_uid(uid); entry && entry->entity)
      draw_selection_highlight(entry->entity.get(), draws, ctx.time, grid_step);
  }

  // 2. Highlight Hovered Item - Yellow
  int dx = drag_current_position.x - drag_start_position.x;
  int dy = drag_current_position.y - drag_start_position.y;
  bool is_dragging_significantly = is_dragging_box && (dx * dx + dy * dy > 25);

  // Every member the click would take, so a group reads as one thing before
  // it is one selection. Members already selected are drawn as selected.
  if (!is_dragging_significantly && hovered_uid != 0 && ctx.map->has_object(hovered_uid))
  {
    std::vector<shared::entity_uid_t> would_pick;
    shared::expand_to_group(*ctx.map, hovered_uid, would_pick);
    for (shared::entity_uid_t uid : would_pick)
      if (std::find(selected_uids.begin(), selected_uids.end(), uid) == selected_uids.end())
        draw_bounds_highlight(uid, colors::yellow);
  }

  // 3. Highlight Box Selection candidates (Live Preview) - Yellow
  if (is_dragging_significantly)
  {
    int x_min = std::min(drag_start_position.x, drag_current_position.x);
    int x_max = std::max(drag_start_position.x, drag_current_position.x);
    int y_min = std::min(drag_start_position.y, drag_current_position.y);
    int y_max = std::max(drag_start_position.y, drag_current_position.y);

    const auto &view = cached_viewport;

    for (const auto &[uid, bounds] : shared::collect_object_bounds(*ctx.map))
    {
      linalg::vec3 p = shared::try_get_object_position(*ctx.map, uid)
                           .value_or((bounds.min + bounds.max) * 0.5f);

      const std::optional<linalg::vec2> screen_pos = try_project_to_screen(view, p);
      if (!screen_pos)
        continue;

      if (screen_pos->x >= x_min && screen_pos->x <= x_max &&
          screen_pos->y >= y_min && screen_pos->y <= y_max)
      {
        bool already_selected = false;
        for (auto selected : selected_uids)
          if (selected == uid)
            already_selected = true;

        if (!already_selected)
          draw_bounds_highlight(uid, colors::yellow);
      }
    }
  }

  // 4. Grid Indication
  if (grid_hover_valid && hovered_uid == 0 &&
      !is_dragging_significantly && !editor_gizmo.is_dragging())
  {
    linalg::vec3 center = grid_hover_position;
    linalg::vec3 half_extents = {editor::GRID_INDICATOR_HALF_W,
                                  editor::GRID_INDICATOR_HALF_H,
                                  editor::GRID_INDICATOR_HALF_W};
    draws.debug.box(center, half_extents, with_alpha(colors::white, 0x88));
  }

  // 5. The group's extent. Only for a real group: for one object the pulsing
  // highlight already says it, and a second box around it is noise. The gizmo
  // stays screen-constant rather than growing to this, which is what keeps it
  // usable for a selection spanning half the level.
  if (selected_uids.size() > 1)
  {
    if (const std::optional<shared::aabb_bounds_t> bounds = try_compute_selection_bounds(ctx))
      draws.debug.box((bounds->min + bounds->max) * 0.5f, (bounds->max - bounds->min) * 0.5f,
                      with_alpha(colors::white, 0x66));
  }

  // 6. Draw Gizmo. No selection-count gate: a gizmo with no target draws
  // nothing, so "is there something to manipulate" is asked in one place.
  editor_gizmo.draw(draws);
}

} // namespace client
