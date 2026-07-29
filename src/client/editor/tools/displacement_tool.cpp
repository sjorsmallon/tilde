#include "displacement_tool.hpp"
#include "../../../shared/box_face.hpp"
#include "../../../shared/collision_detection.hpp"
#include "../../../shared/log.hpp"
#include "../../../shared/shapes.hpp"
#include "../../geometry_renderer.hpp"
#include "imgui.h"
#include "renderer.hpp"
#include <cmath>

namespace client
{

void Displacement_Tool::on_enable(editor_context_t &ctx)
{
  mode = Mode::Setup;
  currently_painting = false;
  cursor_valid = false;
  resize_dragging = false;
  resize_moved = false;
}

void Displacement_Tool::on_disable(editor_context_t &ctx)
{
  currently_painting = false;
  cursor_valid = false;
  box_selecting = false;
  commit_geometry_edit(ctx, select_start_geometry);
  commit_geometry_edit(ctx, resize_start_geometry);
  resize_dragging = false;
}

// ===================================================================
// Helpers
// ===================================================================

shared::displacement_geometry_t * Displacement_Tool::get_displacement_if_it_is_selected(editor_context_t &ctx)
{
  if (selected_uid == shared::invalid_entity_uid || !ctx.map)
    return nullptr;

  shared::map_geometry_t *entry = ctx.map->find_geometry_by_uid(selected_uid);
  if (!entry)
    return nullptr;

  return std::get_if<shared::displacement_geometry_t>(&entry->value);
}

// Push the accumulated change for a multi-frame edit as one value swap.
void Displacement_Tool::commit_geometry_edit(
    editor_context_t &ctx, std::optional<shared::geometry_value_t> &start_state)
{
  if (!start_state)
    return;

  // Take the snapshot regardless of what happens below, so a failed commit
  // can't leave a stale start state to be diffed against a later edit.
  const shared::geometry_value_t before = std::move(*start_state);
  start_state.reset();

  if (!ctx.transaction_system || !ctx.map)
    return;

  const shared::map_geometry_t *entry = ctx.map->find_geometry_by_uid(selected_uid);
  if (!entry)
  {
    log_error("displacement tool: geometry uid {} vanished mid-edit — the edit "
              "is not undoable",
              selected_uid);
    return;
  }

  transaction_builder_t builder;
  builder.add_geometry_modified(selected_uid, before, entry->value);
  ctx.transaction_system->push(builder.take());
}

bool Displacement_Tool::raycast_displacement_mesh(
    const shared::displacement_geometry_t &displacement, const linalg::vec3 &ray_origin,
    const linalg::vec3 &ray_dir, float &out_t, linalg::vec3 &out_normal)
{
  if (displacement.active_face == shared::box_face_t::Invalid)
    return false;

  int grid_size = displacement.grid_size();
  float best_t = 1e30f;
  linalg::vec3 best_normal = {0, 1, 0};
  bool hit = false;

  for (int j = 0; j < grid_size - 1; ++j)
  {
    for (int i = 0; i < grid_size - 1; ++i)
    {
      // construct the two triangles for this quad
      linalg::vec3 tl = displacement.get_vertex_world(i, j);
      linalg::vec3 tr = displacement.get_vertex_world(i + 1, j);
      linalg::vec3 bl = displacement.get_vertex_world(i, j + 1);
      linalg::vec3 br = displacement.get_vertex_world(i + 1, j + 1);

      float t{};
      // Triangle 1: tl, bl, tr
      if (ray_triangle(ray_origin, ray_dir, tl, bl, tr, t) && t < best_t)
      {
        best_t = t;
        linalg::vec3 n = linalg::cross(bl - tl, tr - tl);
        float len = linalg::length(n);
        best_normal = (len > 1e-6f) ? n * (1.0f / len) : displacement.get_face_normal();
        hit = true;
      }
      // Triangle 2: tr, bl, br
      if (ray_triangle(ray_origin, ray_dir, tr, bl, br, t) && t < best_t)
      {
        best_t = t;
        linalg::vec3 n = linalg::cross(bl - tr, br - tr);
        float len = linalg::length(n);
        best_normal = (len > 1e-6f) ? n * (1.0f / len) : displacement.get_face_normal();
        hit = true;
      }
    }
  }

  if (hit)
  {
    out_t = best_t;
    out_normal = best_normal;
  }
  return hit;
}

void Displacement_Tool::apply_brush(shared::displacement_geometry_t &displacement,
                                    float dt, bool invert)
{
  if (!cursor_valid || displacement.active_face == shared::box_face_t::Invalid)
    return;

  int grid_size = displacement.grid_size();
  linalg::vec3 face_normal = displacement.get_face_normal();
  float sign = invert ? -1.0f : 1.0f;
  float sigma = 0.33f;

  for (int j = 0; j < grid_size; ++j)
  {
    for (int i = 0; i < grid_size; ++i)
    {
      linalg::vec3 vpos = displacement.get_vertex_world(i, j);
      linalg::vec3 diff = vpos - cursor_position;
      float dist = linalg::length(diff);
      if (dist > brush_radius)
        continue;

      float t = dist / brush_radius;
      float weight = std::exp(-(t * t) / (2.0f * sigma * sigma));

      linalg::vec3 d = displacement.get_displacement(i, j);
      d = d + face_normal * (sign * brush_strength * weight * dt);
      displacement.set_displacement(i, j, d);
    }
  }
}

linalg::vec2 Displacement_Tool::project_to_screen(const linalg::vec3 &world_pos) const
{
  linalg::vec3 camera_position = {cached_view.camera.position.x, cached_view.camera.position.y,
                          cached_view.camera.position.z};
  linalg::vec3 view_position = linalg::world_to_view(world_pos, camera_position,
                                                cached_view.camera.yaw,
                                                cached_view.camera.pitch);
  return linalg::view_to_screen(view_position, cached_view.display_size,
                                cached_view.camera.orthographic,
                                cached_view.camera.ortho_height,
                                cached_view.fov);
}

void Displacement_Tool::clear_selection(int grid_size)
{
  selected_vertices_bitmask.assign((size_t)(grid_size * grid_size), false);
}

// ===================================================================
// Update
// ===================================================================

void Displacement_Tool::on_update(editor_context_t &ctx,
                                  const viewport_state_t &view, float dt)
{
  if (!ctx.map)
    return;

  resize_last_view = view;
  cached_view = view;

  if (mode == Mode::Setup)
  {
    // Don't update hover while dragging a face
    if (!resize_dragging)
    {
      // reset hover state
      hovered_uid = shared::invalid_entity_uid;
      hovered_face = shared::box_face_t::Invalid;

      if (ctx.bvh)
      {
        auto hit = ray_hit_result_t{};
        if (bvh_intersect_ray(*ctx.bvh, view.mouse_ray.origin,
                               view.mouse_ray.direction, hit))
        {
          if (hit.id.type == Collision_Id::Type::Static_Geometry)
          {
            shared::entity_uid_t uid = hit.id.index;
            shared::map_geometry_t *entry = ctx.map->find_geometry_by_uid(uid);
            if (entry)
            {
              if (const auto* displacement_ptr =
                      std::get_if<shared::displacement_geometry_t>(&entry->value))
              {
                hovered_uid = uid;

                // Face picking on the AABB bounds
                shared::aabb_t aabb;
                aabb.center = displacement_ptr->position;
                aabb.half_extents = displacement_ptr->half_extents;
                float t;
                shared::box_face_t face;
                if (shared::ray_aabb_face_intersection(view.mouse_ray.origin,
                                                      view.mouse_ray.direction, aabb,
                                                      t, face))
                {
                  hovered_face = face;
                }
              }
            }
          }
        }
      }
    }
  }
  else if (mode == Mode::Paint)
  {
    cursor_valid = false;
    auto* displacement_ptr = get_displacement_if_it_is_selected(ctx);
    if (!displacement_ptr)
      return;

    float t;
    linalg::vec3 normal;
    if (raycast_displacement_mesh(*displacement_ptr, view.mouse_ray.origin,
                                  view.mouse_ray.direction, t, normal))
    {
      cursor_position = view.mouse_ray.origin + view.mouse_ray.direction * t;
      cursor_normal = normal;
      cursor_valid = true;
    }

    // If currently currently_painting, apply brush every frame
    if (currently_painting && cursor_valid)
    {
      bool invert = input::current_modifiers().shift;
      apply_brush(*displacement_ptr, dt, invert);
      refresh_displacement_mesh(*displacement_ptr, selected_uid);
      *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
    }
  }
}

// ===================================================================
// Mouse events
// ===================================================================

void Displacement_Tool::on_mouse_down(editor_context_t &ctx,
                                      const input::mouse_event_t &e)
{
  if (e.button != input::mouse_button_t::Left)
    return;

  if (mode == Mode::Setup)
  {
    if (hovered_uid != 0)
    {
      selected_uid = hovered_uid;
      auto *displacement_ptr = get_displacement_if_it_is_selected(ctx);
      if (displacement_ptr)
      {
        pending_subdivision = displacement_ptr->subdivision_level;

        if (displacement_ptr->active_face == shared::box_face_t::Invalid &&
            hovered_face != shared::box_face_t::Invalid)
        {
          if (e.mods.shift)
          {
            // Shift+click: initialize displacement on this face. A single-frame
            // edit, so snapshot and commit right here.
            const shared::geometry_value_t before = *displacement_ptr;
            displacement_ptr->init_grid(hovered_face, pending_subdivision);
            refresh_displacement_mesh(*displacement_ptr, selected_uid);

            if (ctx.transaction_system)
            {
              transaction_builder_t builder;
              builder.add_geometry_modified(selected_uid, before, *displacement_ptr);
              ctx.transaction_system->push(builder.take());
            }

            if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
              *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
          }
          else
          {
            // Plain click: start face drag (resize)
            resize_dragging = true;
            resize_moved = false;
            resize_face = hovered_face;
            resize_start_geometry = *displacement_ptr;
          }
        }
      }
    }
  }
  else if (mode == Mode::Paint)
  {
    currently_painting = true;
  }
  else if (mode == Mode::Select)
  {
    // Begin box selection drag
    box_selecting = true;
    box_start_screen = {(float)e.position.x, (float)e.position.y};
    box_end_screen   = box_start_screen;
    // Commit any pending height edit before starting a new selection
    commit_geometry_edit(ctx, select_start_geometry);
  }
}

void Displacement_Tool::on_mouse_drag(editor_context_t &ctx,
                                      const input::mouse_event_t &e)
{
  if (resize_dragging && selected_uid != 0 && ctx.map)
  {
    auto *displacement_ptr = get_displacement_if_it_is_selected(ctx);
    if (!displacement_ptr)
      return;

    resize_moved = true;

    using namespace linalg;

    vec3 current_center = displacement_ptr->position;
    vec3 current_he = displacement_ptr->half_extents;

    vec3 normal = shared::get_box_face_normal(resize_face);
    vec3 center_offset = {
        normal.x * current_he.x,
        normal.y * current_he.y,
        normal.z * current_he.z,
    };

    vec3 face_center_world = current_center + center_offset;
    vec3 face_end_world = face_center_world + normal;

    vec3 cam_pos = {resize_last_view.camera.position.x, resize_last_view.camera.position.y,
                    resize_last_view.camera.position.z};
    vec3 v0 = world_to_view(face_center_world, cam_pos,
                             resize_last_view.camera.yaw,
                             resize_last_view.camera.pitch);
    vec3 v1 = world_to_view(face_end_world, cam_pos,
                             resize_last_view.camera.yaw,
                             resize_last_view.camera.pitch);

    bool valid = true;
    if (!resize_last_view.camera.orthographic && (v0.z > -0.1f || v1.z > -0.1f))
      valid = false;

    if (valid)
    {
      vec2 screen_start = view_to_screen(v0, resize_last_view.display_size,
                                         resize_last_view.camera.orthographic,
                                         resize_last_view.camera.ortho_height,
                                         resize_last_view.fov);
      vec2 screen_end = view_to_screen(v1, resize_last_view.display_size,
                                       resize_last_view.camera.orthographic,
                                       resize_last_view.camera.ortho_height,
                                       resize_last_view.fov);

      // v0..v1 spans exactly one world unit along the face normal, so projecting
      // the mouse movement onto the face's on-screen direction gives the drag
      // distance directly in world units.
      vec2 face_screen_direction = {screen_end.x - screen_start.x,
                              screen_end.y - screen_start.y};
      float screen_direction_length_squared = face_screen_direction.x * face_screen_direction.x +
                                face_screen_direction.y * face_screen_direction.y;

      if (screen_direction_length_squared > 1e-4f)
      {
        vec2 mouse_delta = {(float)e.delta.x, (float)e.delta.y};
        float world_delta = (mouse_delta.x * face_screen_direction.x +
                             mouse_delta.y * face_screen_direction.y) /
                            screen_direction_length_squared;

        int axis = shared::box_face_axis(resize_face);
        float face_sign = shared::box_face_is_positive(resize_face) ? 1.0f : -1.0f;

        // Grow the box by half the drag along the dragged axis and shift the
        // center by the other half, so the opposite face stays anchored.
        float half_delta = world_delta * 0.5f;
        displacement_ptr->half_extents[axis] += half_delta;
        displacement_ptr->position[axis] += half_delta * face_sign;

        // Enforce the minimum extent, again keeping the opposite face anchored.
        float &extent = displacement_ptr->half_extents[axis];
        if (extent < editor::MIN_EXTENT)
        {
          float correction = editor::MIN_EXTENT - extent;
          extent = editor::MIN_EXTENT;
          displacement_ptr->position[axis] -= correction * face_sign;
        }

        refresh_displacement_mesh(*displacement_ptr, selected_uid);
        *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
      }
    }
  }
  // currently_painting is handled in on_update while currently_painting == true

  if (mode == Mode::Select && box_selecting)
  {
    box_end_screen.x += (float)e.delta.x;
    box_end_screen.y += (float)e.delta.y;
  }
}

void Displacement_Tool::on_mouse_up(editor_context_t &ctx,
                                    const input::mouse_event_t &e)
{
  currently_painting = false;

  if (resize_dragging)
  {
    resize_dragging = false;
    commit_geometry_edit(ctx, resize_start_geometry);
    if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
      *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
  }

  if (mode == Mode::Select && box_selecting)
  {
    box_selecting = false;

    auto *displacement_ptr = get_displacement_if_it_is_selected(ctx);
    if (displacement_ptr && displacement_ptr->active_face != shared::box_face_t::Invalid)
    {
      int grid_size = displacement_ptr->grid_size();
      selected_vertices_bitmask.resize((size_t)(grid_size * grid_size), false);

      float x0 = std::min(box_start_screen.x, box_end_screen.x);
      float x1 = std::max(box_start_screen.x, box_end_screen.x);
      float y0 = std::min(box_start_screen.y, box_end_screen.y);
      float y1 = std::max(box_start_screen.y, box_end_screen.y);

      // Shift = additive selection; otherwise replace
      if (!input::current_modifiers().shift)
        clear_selection(grid_size);

      for (int j = 0; j < grid_size; ++j)
      {
        for (int i = 0; i < grid_size; ++i)
        {
          linalg::vec2 sp = project_to_screen(displacement_ptr->get_vertex_world(i, j));
          if (sp.x >= x0 && sp.x <= x1 && sp.y >= y0 && sp.y <= y1)
            selected_vertices_bitmask[(size_t)(j * grid_size + i)] = true;
        }
      }
    }
  }
}

void Displacement_Tool::on_key_down(editor_context_t &ctx,
                                    const key_event_t &e)
{
  if (e.key == input::key_t::P)
  {
    auto *displacement_ptr = get_displacement_if_it_is_selected(ctx);
    if (displacement_ptr && displacement_ptr->active_face != shared::box_face_t::Invalid)
    {
      commit_geometry_edit(ctx, select_start_geometry);
      mode = Mode::Paint;
    }
  }
  else if (e.key == input::key_t::S)
  {
    auto *displacement_ptr = get_displacement_if_it_is_selected(ctx);
    if (displacement_ptr && displacement_ptr->active_face != shared::box_face_t::Invalid)
    {
      if (mode != Mode::Select)
      {
        mode = Mode::Select;
        int grid_size = displacement_ptr->grid_size();
        if ((int)selected_vertices_bitmask.size() != grid_size * grid_size)
          clear_selection(grid_size);
      }
    }
  }
  else if (e.key == input::key_t::Escape)
  {
    if (mode == Mode::Paint)
      mode = Mode::Setup;
    else if (mode == Mode::Select)
    {
      commit_geometry_edit(ctx, select_start_geometry);
      auto *displacement_ptr = get_displacement_if_it_is_selected(ctx);
      if (displacement_ptr)
        clear_selection(displacement_ptr->grid_size());
      mode = Mode::Setup;
    }
  }
  else if (mode == Mode::Select &&
           (e.key == input::key_t::Q || e.key == input::key_t::E))
  {
    auto *displacement_ptr = get_displacement_if_it_is_selected(ctx);
    if (!displacement_ptr || displacement_ptr->active_face == shared::box_face_t::Invalid)
      return;

    int grid_size = displacement_ptr->grid_size();
    if ((int)selected_vertices_bitmask.size() != grid_size * grid_size)
      return;

    // Count selected verts
    int sel_count = 0;
    for (bool b : selected_vertices_bitmask)
      if (b) ++sel_count;
    if (sel_count == 0)
      return;

    // Snapshot on the first height step of a run, so a burst of Q/E presses
    // undoes as one edit. Committed when the run ends (new box-select, mode
    // change, or the tool going away) — which is also the bug fix: the old
    // commit_select_edit() only DROPPED the snapshot, so height steps were
    // silently never undoable.
    if (!select_start_geometry)
      select_start_geometry = *displacement_ptr;

    float sign = (e.key == input::key_t::Q) ? 1.0f : -1.0f;
    linalg::vec3 face_normal = displacement_ptr->get_face_normal();
    linalg::vec3 delta = face_normal * (sign * height_snap);

    for (int j = 0; j < grid_size; ++j)
    {
      for (int i = 0; i < grid_size; ++i)
      {
        if (selected_vertices_bitmask[(size_t)(j * grid_size + i)])
        {
          linalg::vec3 d = displacement_ptr->get_displacement(i, j);
          displacement_ptr->set_displacement(i, j, d + delta);
        }
      }
    }

    refresh_displacement_mesh(*displacement_ptr, selected_uid);
    if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
      *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
  }
}

// ===================================================================
// Overlay
// ===================================================================

void Displacement_Tool::on_draw_overlay(editor_context_t &ctx,
                                        overlay_renderer_t &renderer)
{
  // In setup mode: highlight hovered face
  if (mode == Mode::Setup && hovered_uid != 0 &&
      hovered_face != shared::box_face_t::Invalid && ctx.map)
  {
    const shared::map_geometry_t *entry = ctx.map->find_geometry_by_uid(hovered_uid);
    if (entry)
    {
      if (const auto* displacement_ptr =
              std::get_if<shared::displacement_geometry_t>(&entry->value))
      {
        linalg::vec3 half_extents = displacement_ptr->half_extents;
        linalg::vec3 normal = shared::get_box_face_normal(hovered_face);
        // Move plane center to the face by going one half-extent along the
        // face normal, then flatten the box along that same axis (size = 0).
        linalg::vec3 p = displacement_ptr->position +
                         linalg::vec3{normal.x * half_extents.x, normal.y * half_extents.y, normal.z * half_extents.z};
        linalg::vec3 size = half_extents;
        int axis = shared::box_face_axis(hovered_face);
        if (axis == 0) size.x = 0;
        else if (axis == 1) size.y = 0;
        else size.z = 0;

        renderer.draw_wire_box(p, size, colors::green); // Green highlight
      }
    }
  }

  // Draw the grid wireframe for the selected displacement entity
  if (selected_uid != 0 && ctx.map)
  {
    auto *displacement_ptr = get_displacement_if_it_is_selected(ctx);
    if (displacement_ptr && displacement_ptr->active_face != shared::box_face_t::Invalid)
    {
      int grid_size = displacement_ptr->grid_size();
      color_t grid_color = colors::grey;

      // Draw grid lines along i
      for (int j = 0; j < grid_size; ++j)
      {
        for (int i = 0; i < grid_size - 1; ++i)
        {
          renderer.draw_line(displacement_ptr->get_vertex_world(i, j),
                             displacement_ptr->get_vertex_world(i + 1, j), grid_color);
        }
      }
      // Draw grid lines along j
      for (int i = 0; i < grid_size; ++i)
      {
        for (int j = 0; j < grid_size - 1; ++j)
        {
          renderer.draw_line(displacement_ptr->get_vertex_world(i, j),
                             displacement_ptr->get_vertex_world(i, j + 1), grid_color);
        }
      }
    }
  }

  // In paint mode: draw brush circle and normal arrow
  if (mode == Mode::Paint && cursor_valid)
  {
    renderer.draw_circle(cursor_position, brush_radius, cursor_normal, colors::yellow);

    // Draw normal arrow
    linalg::vec3 arrow_end = cursor_position + cursor_normal * (brush_radius * 0.5f);
    renderer.draw_line(cursor_position, arrow_end, colors::red);
  }

  // In select mode: highlight selected vertices and draw dragging box
  if (mode == Mode::Select && selected_uid != 0 && ctx.map)
  {
    auto *displacement_ptr = get_displacement_if_it_is_selected(ctx);
    if (displacement_ptr && displacement_ptr->active_face != shared::box_face_t::Invalid)
    {
      int grid_size = displacement_ptr->grid_size();
      if ((int)selected_vertices_bitmask.size() == grid_size * grid_size)
      {
        const float dot_r = 0.4f;
        linalg::vec3 fn = displacement_ptr->get_face_normal();
        for (int j = 0; j < grid_size; ++j)
        {
          for (int i = 0; i < grid_size; ++i)
          {
            if (selected_vertices_bitmask[(size_t)(j * grid_size + i)])
            {
              linalg::vec3 vp = displacement_ptr->get_vertex_world(i, j);
              renderer.draw_circle(vp, dot_r, fn, colors::cyan); // selected-vertex dot
            }
          }
        }
      }
    }
  }
}

// ===================================================================
// UI
// ===================================================================

void Displacement_Tool::on_draw_ui(editor_context_t &ctx)
{
  // Draw green selection rectangle while box-selecting vertices
  if (mode == Mode::Select && box_selecting)
  {
    float dx = box_end_screen.x - box_start_screen.x;
    float dy = box_end_screen.y - box_start_screen.y;

    if (dx * dx + dy * dy > 25)
    {
      ImDrawList *draw_list = ImGui::GetForegroundDrawList();
      ImVec2 p1 = ImVec2(box_start_screen.x, box_start_screen.y);
      ImVec2 p2 = ImVec2(box_end_screen.x, box_end_screen.y);
      draw_list->AddRect(p1, p2, IM_COL32(0, 255, 0, 255));
      draw_list->AddRectFilled(p1, p2, IM_COL32(0, 255, 0, 50));
    }
  }

  ImGui::SetNextWindowSize({220, 0}, ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Displacement"))
  {
    if (mode == Mode::Setup)
    {
      ImGui::Text("Mode: Setup");
      ImGui::Separator();

      if (selected_uid != 0)
      {
        auto* displacement_ptr = get_displacement_if_it_is_selected(ctx);
        if (displacement_ptr)
        {
          ImGui::Text("Displacement: %u", selected_uid);

          if (displacement_ptr->active_face != shared::box_face_t::Invalid)
          {
            const char *face_names[shared::box_face_count] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
            ImGui::Text("Face: %s",
                        face_names[static_cast<size_t>(displacement_ptr->active_face)]);
            ImGui::TextDisabled("(resize locked after displacement)");

            // Subdivision slider. Resamples the existing grid instead of
            // zeroing it, so re-subdividing keeps the sculpt — and the upper
            // bound is a UI choice now, not the 32 that the old fixed-size
            // schema_array_t<float32, 3267> could physically hold.
            if (ImGui::SliderInt("Subdivision", &pending_subdivision, 2, 64))
            {
              const shared::geometry_value_t before = *displacement_ptr;
              displacement_ptr->resize_grid_preserving(pending_subdivision);
              refresh_displacement_mesh(*displacement_ptr, selected_uid);

              if (ctx.transaction_system)
              {
                transaction_builder_t builder;
                builder.add_geometry_modified(selected_uid, before, *displacement_ptr);
                ctx.transaction_system->push(builder.take());
              }
              *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
            }

            ImGui::Separator();
            if (ImGui::Button("Enter Paint Mode (P)"))
            {
              mode = Mode::Paint;
            }
          }
          else
          {
            ImGui::Text("Drag face to resize");
            ImGui::Text("Shift+click face to begin displacement");
          }
        }
      }
      else
      {
        ImGui::Text("Click a displacement");
      }
    }
    else if (mode == Mode::Paint)
    {
      ImGui::Text("Mode: Paint");
      ImGui::Separator();

      ImGui::SliderFloat("Radius", &brush_radius, 1.0f, 256.0f);
      ImGui::SliderFloat("Strength", &brush_strength, 0.1f, 20.0f);

      ImGui::Separator();
      ImGui::Text("LMB: displace along normal");
      ImGui::Text("Shift+LMB: displace inward");
      ImGui::Text("S: Select mode  ESC: Setup");

      if (ImGui::Button("Back to Setup (ESC)"))
        mode = Mode::Setup;
    }
    else if (mode == Mode::Select)
    {
      ImGui::Text("Mode: Select");
      ImGui::Separator();

      int sel_count = 0;
      for (bool b : selected_vertices_bitmask)
        if (b) ++sel_count;
      ImGui::Text("Selected: %d vertices", sel_count);

      ImGui::SliderFloat("Snap", &height_snap, 1.0f, 512.0f);

      ImGui::Separator();
      ImGui::Text("Drag: box select");
      ImGui::Text("Shift+Drag: additive select");
      ImGui::Text("Q: raise  E: lower");
      ImGui::Text("P: Paint  ESC: Setup");

      if (ImGui::Button("Clear Selection"))
      {
        auto *displacement_ptr = get_displacement_if_it_is_selected(ctx);
        if (displacement_ptr) clear_selection(displacement_ptr->grid_size());
      }
      ImGui::SameLine();
      if (ImGui::Button("Back to Setup"))
      {
        commit_geometry_edit(ctx, select_start_geometry);
        auto *displacement_ptr = get_displacement_if_it_is_selected(ctx);
        if (displacement_ptr) clear_selection(displacement_ptr->grid_size());
        mode = Mode::Setup;
      }
    }
  }
  ImGui::End();
}

} // namespace client
