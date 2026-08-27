#include "../../../shared/entities/entity_reflection.hpp"
#include "selection_tool.hpp"
#include "../../renderer.hpp"
#include "../entity_editor_traits.hpp"
#include "../entity_inspector.hpp"
#include "../geometry_editor.hpp"
#include "../transaction_system.hpp"
#include "../../../shared/log.hpp"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace client
{

namespace
{

// Rotate `offset` about whichever single world axis `euler_degrees` names. The
// gizmo turns one ring at a time, so exactly one component is ever non-zero;
// the axis pair matches the ring's own basis ((axis+1)%3, (axis+2)%3), which is
// what makes a positive drag orbit the way the ring reads.
linalg::vec3 rotate_about_world_axis(const linalg::vec3 &offset,
                                     const linalg::vec3 &euler_degrees)
{
  linalg::vec3 result = offset;
  for (int axis = 0; axis < 3; ++axis)
  {
    if (euler_degrees[axis] == 0.f)
      continue;

    const int   u       = (axis + 1) % 3;
    const int   v       = (axis + 2) % 3;
    const float radians = linalg::to_radians(euler_degrees[axis]);
    const float cosine  = std::cos(radians);
    const float sine    = std::sin(radians);

    const linalg::vec3 source = result;
    result[u] = source[u] * cosine - source[v] * sine;
    result[v] = source[u] * sine + source[v] * cosine;
  }
  return result;
}

} // namespace


// Capture the pre-drag state of everything selected. Both regimes go into the
// same map keyed by uid, so the drag itself never asks which is which.
void Selection_Tool::capture_drag_snapshots(editor_context_t &ctx)
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
         shared::try_get_object_orientation(*ctx.map, uid).value_or(linalg::vec3{0, 0, 0})});

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
void Selection_Tool::commit_drag_snapshots(editor_context_t &ctx)
{
  if (drag_start_snapshots.empty() || !ctx.transaction_system || !ctx.map)
  {
    drag_start_snapshots.clear();
    return;
  }

  transaction_builder_t builder;
  for (const auto &[uid, snapshot] : drag_start_snapshots)
  {
    if (snapshot.geometry)
    {
      if (const shared::map_geometry_t *entry = ctx.map->find_geometry_by_uid(uid))
        builder.add_geometry_modified(uid, *snapshot.geometry, entry->value);
      continue;
    }

    if (auto *entry = ctx.map->find_by_uid(uid); entry && entry->entity)
      builder.add_modified_from_diff(uid, snapshot.entity, entry->entity.get());
  }
  ctx.transaction_system->push(builder.take());
  drag_start_snapshots.clear();
  drag_origins.clear();
}

std::optional<shared::aabb_bounds_t>
Selection_Tool::try_compute_selection_bounds(editor_context_t &ctx) const
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
  return {camera.position, camera.orthographic, camera.ortho_height, camera.fov_degrees};
}

// Apply what the gizmo reported to everything the drag started on. The gizmo
// itself writes nothing -- it does not know a map exists -- so this is the one
// place a gizmo drag reaches the world, and it goes through the same per-uid
// seam every other tool uses.
void Selection_Tool::apply_gizmo_drag(editor_context_t &ctx, const gizmo_drag_t &drag)
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

  if (drag.rotation.x != 0.f || drag.rotation.y != 0.f || drag.rotation.z != 0.f)
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
            drag.pivot + rotate_about_world_axis(origin.position - drag.pivot, drag.rotation);
        if (!shared::try_set_object_position(*ctx.map, origin.uid, orbited))
          log_error("selection tool: object {} vanished mid-drag", origin.uid);
      }

      // An object with no orientation to store still travelled -- an
      // axis-aligned box orbits the pivot and stays axis-aligned, which is what
      // it IS. Writing a rotation onto one and drawing it unrotated is the lie
      // map_geometry.hpp deleted the field for, so this is not a failure.
      const std::optional<linalg::vec3> current =
          shared::try_get_object_orientation(*ctx.map, origin.uid);
      if (!current)
        continue;

      if (!shared::try_set_object_orientation(*ctx.map, origin.uid,
                                              origin.orientation + drag.rotation))
        log_error("selection tool: object {} took a rotation it cannot store", origin.uid);
    }
  }

  if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
}

void Selection_Tool::apply_transform_as_one_edit(editor_context_t   &ctx,
                                                 const gizmo_drag_t &transform)
{
  capture_drag_snapshots(ctx);
  apply_gizmo_drag(ctx, transform);
  commit_drag_snapshots(ctx);
}

void Selection_Tool::draw_multi_selection_panel(editor_context_t &ctx)
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
      turn.rotation[axis] = 90.f;
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

void Selection_Tool::on_enable(editor_context_t &ctx)
{
  hovered_uid = 0;
  selected_uids.clear();
  editor_gizmo.clear_target();
}

void Selection_Tool::on_disable(editor_context_t &ctx)
{
  hovered_uid = 0;
  editor_gizmo.clear_target();
}

void Selection_Tool::on_draw_ui(editor_context_t &ctx)
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
        if (draw_geometry_inspector(geometry->value))
        {
          if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
            *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
        }
      }
      else if (auto *entry = ctx.map->find_by_uid(uid); entry && entry->entity)
      {
        render_entity_fields_in_an_imgui_window(entry->entity.get());
      }
    }
    ImGui::End();
  }
}

void Selection_Tool::on_update(editor_context_t &ctx,
                               const viewport_state_t &view, float /*dt*/)
{
  cached_viewport = view;

  if (ctx.grid)
    editor_gizmo.snap_step = ctx.grid->step();

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
          if (ctx.map->has_object(uid))
          {
            hovered_uid = uid;
            hit_bvh = true;
          }
        }
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

void Selection_Tool::on_mouse_down(editor_context_t &ctx,
                                   const input::mouse_event_t &e)
{
  if (e.button == input::mouse_button_t::Left)
  {
    if (editor_gizmo.try_begin_drag(cached_viewport.mouse_ray, make_gizmo_view()))
    {
      capture_drag_snapshots(ctx);
      return;
    }

    // Ctrl+LMB: move selected objects in the camera's view plane
    if (e.mods.ctrl && !selected_uids.empty() && ctx.map)
    {
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

void Selection_Tool::on_mouse_drag(editor_context_t &ctx,
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

      const float snap_step = ctx.grid ? ctx.grid->step() : editor::MAJOR_GRID_STEP;

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
      if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
        *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
    }
    return;
  }

  if (is_dragging_box)
  {
    drag_current_position = e.position;
  }
}

void Selection_Tool::on_mouse_up(editor_context_t &ctx, const input::mouse_event_t &e)
{
  if (e.button == input::mouse_button_t::Left)
  {
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
      return;
    }

    is_dragging_box = false;

    int dx = e.position.x - drag_start_position.x;
    int dy = e.position.y - drag_start_position.y;
    bool moved_significantly = (dx * dx + dy * dy) > 25;

    if (moved_significantly)
    {
      if (!ctx.map)
        return;

      int x_min = std::min(drag_start_position.x, drag_current_position.x);
      int x_max = std::max(drag_start_position.x, drag_current_position.x);
      int y_min = std::min(drag_start_position.y, drag_current_position.y);
      int y_max = std::max(drag_start_position.y, drag_current_position.y);

      if (!e.mods.shift)
      {
        selected_uids.clear();
      }

      const auto &view = cached_viewport;

      for (const auto &[uid, bounds] : shared::collect_object_bounds(*ctx.map))
      {
        linalg::vec3 p = (bounds.min + bounds.max) * 0.5f;

        linalg::vec3 view_pos = linalg::world_to_view(
            p, {view.camera.position.x, view.camera.position.y, view.camera.position.z}, view.camera.yaw,
            view.camera.pitch);

        if (view_pos.z >= -0.1f)
          continue;

        linalg::vec2 screen_pos = linalg::view_to_screen(
            view_pos, view.display_size, view.camera.orthographic,
            view.camera.ortho_height, view.camera.fov_degrees);

        if (screen_pos.x >= x_min && screen_pos.x <= x_max &&
            screen_pos.y >= y_min && screen_pos.y <= y_max)
        {
          bool already_selected = false;
          for (auto selected : selected_uids)
            if (selected == uid)
              already_selected = true;
          if (!already_selected)
            selected_uids.push_back(uid);
        }
      }
    }
    else
    {
      if (hovered_uid != 0)
      {
        bool already_selected = false;
        for (auto uid : selected_uids)
        {
          if (uid == hovered_uid)
          {
            already_selected = true;
            break;
          }
        }

        if (e.mods.shift)
        {
          if (already_selected)
          {
            auto it =
                std::remove(selected_uids.begin(),
                            selected_uids.end(), hovered_uid);
            selected_uids.erase(it, selected_uids.end());
          }
          else
          {
            selected_uids.push_back(hovered_uid);
          }
        }
        else
        {
          selected_uids.clear();
          selected_uids.push_back(hovered_uid);
        }
      }
      else
      {
        if (!e.mods.shift)
        {
          selected_uids.clear();
        }
      }
    }
  }
}

void Selection_Tool::on_key_down(editor_context_t &ctx, const key_event_t &e)
{
  if (e.key == input::key_t::Delete || e.key == input::key_t::Backspace)
  {
    if (!selected_uids.empty() && ctx.map && ctx.transaction_system)
    {
      // One transaction for the whole selection, so Ctrl+Z brings back every
      // deleted object at once.
      transaction_builder_t builder;
      for (auto uid : selected_uids)
      {
        if (const shared::map_geometry_t *geometry = ctx.map->find_geometry_by_uid(uid))
        {
          builder.add_geometry_removed(uid, geometry->value);
          ctx.map->remove_geometry(uid);
          continue;
        }

        if (auto *entry = ctx.map->find_by_uid(uid); entry && entry->entity)
        {
          builder.add_removed(uid, snapshot_entity(entry->entity.get()));
          ctx.map->remove_entity(uid);
        }
      }
      ctx.transaction_system->push(builder.take());

      if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
        *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
    }
    selected_uids.clear();
    hovered_uid = 0;
  }
}

void Selection_Tool::on_draw_overlay(editor_context_t &ctx,
                                     pass_builder_t &draws)
{
  if (!ctx.map)
    return;

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

  if (!is_dragging_significantly && hovered_uid != 0)
  {
    bool is_selected = false;
    for (auto uid : selected_uids)
      if (uid == hovered_uid)
        is_selected = true;

    if (!is_selected && ctx.map->has_object(hovered_uid))
      draw_bounds_highlight(hovered_uid, colors::yellow);
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
      linalg::vec3 p = (bounds.min + bounds.max) * 0.5f;

      linalg::vec3 view_pos = linalg::world_to_view(
          p, {view.camera.position.x, view.camera.position.y, view.camera.position.z}, view.camera.yaw,
          view.camera.pitch);

      if (view_pos.z >= -0.1f)
        continue;

      linalg::vec2 screen_pos = linalg::view_to_screen(
          view_pos, view.display_size, view.camera.orthographic,
          view.camera.ortho_height, view.camera.fov_degrees);

      if (screen_pos.x >= x_min && screen_pos.x <= x_max &&
          screen_pos.y >= y_min && screen_pos.y <= y_max)
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
