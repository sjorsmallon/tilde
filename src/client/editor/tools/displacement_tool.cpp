#include "displacement_tool.hpp"
#include "../../../shared/box_face.hpp"
#include "../../../shared/collision_detection.hpp"
#include "../../../shared/shapes.hpp"
#include "imgui.h"
#include "renderer.hpp"
#include <cmath>

namespace client
{

void Displacement_Tool::on_enable(editor_context_t &ctx)
{
  mode = Mode::Setup;
  painting = false;
  cursor_valid = false;
  resize_dragging = false;
  resize_moved = false;
}

void Displacement_Tool::on_disable(editor_context_t &ctx)
{
  painting = false;
  cursor_valid = false;
  box_selecting = false;
  commit_select_edit();

  if (resize_dragging && !resize_start_props.empty() && ctx.transaction_system &&
      ctx.map)
  {
    auto *entry = ctx.map->find_by_uid(selected_uid);
    if (entry && entry->entity)
    {
      transaction_builder_t builder;
      builder.add_modified_from_diff(selected_uid, resize_start_props,
                                     entry->entity->get_all_properties());
      ctx.transaction_system->push(builder.take());
    }
  }
  resize_dragging = false;
  resize_start_props.clear();
}

// ===================================================================
// Helpers
// ===================================================================

network::Displacement_Entity *
Displacement_Tool::get_selected(editor_context_t &ctx)
{
  if (selected_uid == shared::invalid_entity_uid|| !ctx.map)
    return nullptr;
  auto *entry = ctx.map->find_by_uid(selected_uid);
  if (!entry || !entry->entity)
    return nullptr;
  return shared::entity_as<network::Displacement_Entity>(entry->entity.get());
}

bool Displacement_Tool::raycast_displacement_mesh(
    const network::Displacement_Entity &ent, const linalg::vec3 &ray_origin,
    const linalg::vec3 &ray_dir, float &out_t, linalg::vec3 &out_normal)
{
  if (ent.active_face == shared::box_face_t::Invalid)
    return false;

  int grid_size = ent.grid_size();
  float best_t = 1e30f;
  linalg::vec3 best_normal = {0, 1, 0};
  bool hit = false;

  for (int j = 0; j < grid_size - 1; ++j)
  {
    for (int i = 0; i < grid_size - 1; ++i)
    {
      // construct the two triangles for this quad
      linalg::vec3 tl = ent.get_vertex_world(i, j);
      linalg::vec3 tr = ent.get_vertex_world(i + 1, j);
      linalg::vec3 bl = ent.get_vertex_world(i, j + 1);
      linalg::vec3 br = ent.get_vertex_world(i + 1, j + 1);

      float t{};
      // Triangle 1: tl, bl, tr
      if (ray_triangle(ray_origin, ray_dir, tl, bl, tr, t) && t < best_t)
      {
        best_t = t;
        linalg::vec3 n = linalg::cross(bl - tl, tr - tl);
        float len = linalg::length(n);
        best_normal = (len > 1e-6f) ? n * (1.0f / len) : ent.get_face_normal();
        hit = true;
      }
      // Triangle 2: tr, bl, br
      if (ray_triangle(ray_origin, ray_dir, tr, bl, br, t) && t < best_t)
      {
        best_t = t;
        linalg::vec3 n = linalg::cross(bl - tr, br - tr);
        float len = linalg::length(n);
        best_normal = (len > 1e-6f) ? n * (1.0f / len) : ent.get_face_normal();
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

void Displacement_Tool::apply_brush(network::Displacement_Entity &ent,
                                    float dt, bool invert)
{
  if (!cursor_valid || ent.active_face == shared::box_face_t::Invalid)
    return;

  int grid_size = ent.grid_size();
  linalg::vec3 face_normal = ent.get_face_normal();
  float sign = invert ? -1.0f : 1.0f;
  float sigma = 0.33f;

  for (int j = 0; j < grid_size; ++j)
  {
    for (int i = 0; i < grid_size; ++i)
    {
      linalg::vec3 vpos = ent.get_vertex_world(i, j);
      linalg::vec3 diff = vpos - cursor_pos;
      float dist = linalg::length(diff);
      if (dist > brush_radius)
        continue;

      float t = dist / brush_radius;
      float weight = std::exp(-(t * t) / (2.0f * sigma * sigma));

      linalg::vec3 d = ent.get_displacement(i, j);
      d = d + face_normal * (sign * brush_strength * weight * dt);
      ent.set_displacement(i, j, d);
    }
  }
}

void Displacement_Tool::regenerate_mesh(network::Displacement_Entity &ent,
                                        shared::entity_uid_t uid)
{
  std::string key = "__displacement_" + std::to_string(uid);
  auto handle = assets::find_mesh_in_cache(key.c_str());
  if (handle.valid())
  {
    // Update existing mesh in-place and re-upload to GPU
    auto *mesh = assets::get_mutable(handle);
    if (mesh)
      *mesh = network::generate_displacement_mesh(ent);
    renderer::invalidate_mesh_gpu(handle);
  }
  else
  {
    // First time: register the mesh
    auto mesh = network::generate_displacement_mesh(ent);
    assets::register_dynamic_mesh(key.c_str(), std::move(mesh));
  }
}

linalg::vec2 Displacement_Tool::project_to_screen(const linalg::vec3 &world_pos) const
{
  linalg::vec3 cam_pos = {cached_view.camera.position.x, cached_view.camera.position.y,
                          cached_view.camera.position.z};
  linalg::vec3 view_pos = linalg::world_to_view(world_pos, cam_pos,
                                                cached_view.camera.yaw,
                                                cached_view.camera.pitch);
  return linalg::view_to_screen(view_pos, cached_view.display_size,
                                cached_view.camera.orthographic,
                                cached_view.camera.ortho_height,
                                cached_view.fov);
}

void Displacement_Tool::commit_select_edit()
{
  select_start_props.clear();
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
      hovered_uid = 0;
      hovered_face = shared::box_face_t::Invalid;

      if (ctx.bvh)
      {
        ray_hit_result_t hit;
        if (bvh_intersect_ray(*ctx.bvh, view.mouse_ray.origin,
                               view.mouse_ray.dir, hit))
        {
          if (hit.id.type == Collision_Id::Type::Static_Geometry)
          {
            shared::entity_uid_t uid = hit.id.index;
            auto *entry = ctx.map->find_by_uid(uid);
            if (entry && entry->entity)
            {
              if (auto *disp = shared::entity_as<network::Displacement_Entity>(
                      entry->entity.get()))
              {
                hovered_uid = uid;

                // Face picking on the AABB bounds
                shared::aabb_t aabb;
                aabb.center = disp->position;
                aabb.half_extents = disp->volume.half_extents;
                float t;
                shared::box_face_t face;
                if (shared::ray_aabb_face_intersection(view.mouse_ray.origin,
                                                      view.mouse_ray.dir, aabb,
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
    auto *ent = get_selected(ctx);
    if (!ent)
      return;

    float t;
    linalg::vec3 normal;
    if (raycast_displacement_mesh(*ent, view.mouse_ray.origin,
                                  view.mouse_ray.dir, t, normal))
    {
      cursor_pos = view.mouse_ray.origin + view.mouse_ray.dir * t;
      cursor_normal = normal;
      cursor_valid = true;
    }

    // If currently painting, apply brush every frame
    if (painting && cursor_valid)
    {
      bool invert = input::current_modifiers().shift;
      apply_brush(*ent, dt, invert);
      regenerate_mesh(*ent, selected_uid);
      *ctx.geometry_updated = true;
    }
  }
}

// ===================================================================
// Mouse events
// ===================================================================

void Displacement_Tool::on_mouse_down(editor_context_t &ctx,
                                      const mouse_event_t &e)
{
  if (e.button != input::MouseButton::Left)
    return;

  if (mode == Mode::Setup)
  {
    if (hovered_uid != 0)
    {
      selected_uid = hovered_uid;
      auto *ent = get_selected(ctx);
      if (ent)
      {
        pending_subdivision = ent->subdivision_level;

        if (ent->active_face == shared::box_face_t::Invalid &&
            hovered_face != shared::box_face_t::Invalid)
        {
          if (e.mods.shift)
          {
            // Shift+click: initialize displacement on this face
            ent->init_displacement(hovered_face, pending_subdivision);
            regenerate_mesh(*ent, selected_uid);
            if (ctx.geometry_updated)
              *ctx.geometry_updated = true;
          }
          else
          {
            // Plain click: start face drag (resize)
            resize_dragging = true;
            resize_moved = false;
            resize_face = hovered_face;

            if (ctx.transaction_system && ctx.map)
            {
              auto *e = ctx.map->find_by_uid(selected_uid);
              if (e && e->entity)
                resize_start_props = e->entity->get_all_properties();
            }
          }
        }
      }
    }
  }
  else if (mode == Mode::Paint)
  {
    painting = true;
  }
  else if (mode == Mode::Select)
  {
    // Begin box selection drag
    box_selecting = true;
    box_start_screen = {(float)e.position.x, (float)e.position.y};
    box_end_screen   = box_start_screen;
    // Commit any pending height edit before starting a new selection
    commit_select_edit();
  }
}

void Displacement_Tool::on_mouse_drag(editor_context_t &ctx,
                                      const mouse_event_t &e)
{
  if (resize_dragging && selected_uid != 0 && ctx.map)
  {
    auto *ent = get_selected(ctx);
    if (!ent)
      return;

    resize_moved = true;

    using namespace linalg;

    vec3 current_center = ent->position;
    vec3 current_he = ent->volume.half_extents;

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
        ent->volume.half_extents[axis] += half_delta;
        ent->position[axis] += half_delta * face_sign;

        // Enforce the minimum extent, again keeping the opposite face anchored.
        float &extent = ent->volume.half_extents[axis];
        if (extent < editor::MIN_EXTENT)
        {
          float correction = editor::MIN_EXTENT - extent;
          extent = editor::MIN_EXTENT;
          ent->position[axis] -= correction * face_sign;
        }

        regenerate_mesh(*ent, selected_uid);
        *ctx.geometry_updated = true;
      }
    }
  }
  // Painting is handled in on_update while painting == true

  if (mode == Mode::Select && box_selecting)
  {
    box_end_screen.x += (float)e.delta.x;
    box_end_screen.y += (float)e.delta.y;
  }
}

void Displacement_Tool::on_mouse_up(editor_context_t &ctx,
                                    const mouse_event_t &e)
{
  painting = false;

  if (resize_dragging)
  {
    resize_dragging = false;

    // Commit the resize transaction
    if (!resize_start_props.empty() && ctx.transaction_system && ctx.map)
    {
      auto *entry = ctx.map->find_by_uid(selected_uid);
      if (entry && entry->entity)
      {
        transaction_builder_t builder;
        builder.add_modified_from_diff(selected_uid, resize_start_props,
                                       entry->entity->get_all_properties());
        ctx.transaction_system->push(builder.take());
      }
    }
    if (ctx.geometry_updated)
      *ctx.geometry_updated = true;
    resize_start_props.clear();
  }

  if (mode == Mode::Select && box_selecting)
  {
    box_selecting = false;

    auto *ent = get_selected(ctx);
    if (ent && ent->active_face != shared::box_face_t::Invalid)
    {
      int grid_size = ent->grid_size();
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
          linalg::vec2 sp = project_to_screen(ent->get_vertex_world(i, j));
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
  if (e.key == input::Key::P)
  {
    auto *ent = get_selected(ctx);
    if (ent && ent->active_face != shared::box_face_t::Invalid)
    {
      commit_select_edit();
      mode = Mode::Paint;
    }
  }
  else if (e.key == input::Key::S)
  {
    auto *ent = get_selected(ctx);
    if (ent && ent->active_face != shared::box_face_t::Invalid)
    {
      if (mode != Mode::Select)
      {
        mode = Mode::Select;
        int grid_size = ent->grid_size();
        if ((int)selected_vertices_bitmask.size() != grid_size * grid_size)
          clear_selection(grid_size);
      }
    }
  }
  else if (e.key == input::Key::Escape)
  {
    if (mode == Mode::Paint)
      mode = Mode::Setup;
    else if (mode == Mode::Select)
    {
      commit_select_edit();
      auto *ent = get_selected(ctx);
      if (ent)
        clear_selection(ent->grid_size());
      mode = Mode::Setup;
    }
  }
  else if (mode == Mode::Select &&
           (e.key == input::Key::Q || e.key == input::Key::E))
  {
    auto *ent = get_selected(ctx);
    if (!ent || ent->active_face == shared::box_face_t::Invalid)
      return;

    int grid_size = ent->grid_size();
    if ((int)selected_vertices_bitmask.size() != grid_size * grid_size)
      return;

    // Count selected verts
    int sel_count = 0;
    for (bool b : selected_vertices_bitmask)
      if (b) ++sel_count;
    if (sel_count == 0)
      return;

    // Snapshot on first height change (reset each time we enter Select mode)
    if (select_start_props.empty() && ctx.transaction_system && ctx.map)
    {
      auto *entry = ctx.map->find_by_uid(selected_uid);
      if (entry && entry->entity)
        select_start_props = entry->entity->get_all_properties();
    }

    float sign = (e.key == input::Key::Q) ? 1.0f : -1.0f;
    linalg::vec3 face_normal = ent->get_face_normal();
    linalg::vec3 delta = face_normal * (sign * height_snap);

    for (int j = 0; j < grid_size; ++j)
    {
      for (int i = 0; i < grid_size; ++i)
      {
        if (selected_vertices_bitmask[(size_t)(j * grid_size + i)])
        {
          linalg::vec3 d = ent->get_displacement(i, j);
          ent->set_displacement(i, j, d + delta);
        }
      }
    }

    regenerate_mesh(*ent, selected_uid);
    if (ctx.geometry_updated)
      *ctx.geometry_updated = true;
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
    auto *entry = ctx.map->find_by_uid(hovered_uid);
    if (entry && entry->entity)
    {
      if (auto *disp = shared::entity_as<network::Displacement_Entity>(
              entry->entity.get()))
      {
        linalg::vec3 he = disp->volume.half_extents;
        linalg::vec3 normal = shared::get_box_face_normal(hovered_face);
        // Move plane center to the face by going one half-extent along the
        // face normal, then flatten the box along that same axis (size = 0).
        linalg::vec3 p = disp->position +
                         linalg::vec3{normal.x * he.x, normal.y * he.y, normal.z * he.z};
        linalg::vec3 size = he;
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
    auto *ent = get_selected(ctx);
    if (ent && ent->active_face != shared::box_face_t::Invalid)
    {
      int grid_size = ent->grid_size();
      color_t grid_color = colors::grey;

      // Draw grid lines along i
      for (int j = 0; j < grid_size; ++j)
      {
        for (int i = 0; i < grid_size - 1; ++i)
        {
          renderer.draw_line(ent->get_vertex_world(i, j),
                             ent->get_vertex_world(i + 1, j), grid_color);
        }
      }
      // Draw grid lines along j
      for (int i = 0; i < grid_size; ++i)
      {
        for (int j = 0; j < grid_size - 1; ++j)
        {
          renderer.draw_line(ent->get_vertex_world(i, j),
                             ent->get_vertex_world(i, j + 1), grid_color);
        }
      }
    }
  }

  // In paint mode: draw brush circle and normal arrow
  if (mode == Mode::Paint && cursor_valid)
  {
    renderer.draw_circle(cursor_pos, brush_radius, cursor_normal, colors::yellow);

    // Draw normal arrow
    linalg::vec3 arrow_end = cursor_pos + cursor_normal * (brush_radius * 0.5f);
    renderer.draw_line(cursor_pos, arrow_end, colors::red);
  }

  // In select mode: highlight selected vertices and draw dragging box
  if (mode == Mode::Select && selected_uid != 0 && ctx.map)
  {
    auto *ent = get_selected(ctx);
    if (ent && ent->active_face != shared::box_face_t::Invalid)
    {
      int grid_size = ent->grid_size();
      if ((int)selected_vertices_bitmask.size() == grid_size * grid_size)
      {
        const float dot_r = 0.4f;
        linalg::vec3 fn = ent->get_face_normal();
        for (int j = 0; j < grid_size; ++j)
        {
          for (int i = 0; i < grid_size; ++i)
          {
            if (selected_vertices_bitmask[(size_t)(j * grid_size + i)])
            {
              linalg::vec3 vp = ent->get_vertex_world(i, j);
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
        auto *ent = get_selected(ctx);
        if (ent)
        {
          ImGui::Text("Entity: %u", selected_uid);

          if (ent->active_face != shared::box_face_t::Invalid)
          {
            const char *face_names[shared::box_face_count] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
            ImGui::Text("Face: %s",
                        face_names[static_cast<size_t>(ent->active_face)]);
            ImGui::TextDisabled("(resize locked after displacement)");

            // Subdivision slider
            if (ImGui::SliderInt("Subdivision", &pending_subdivision, 2, 32))
            {
              ent->init_displacement(ent->active_face, pending_subdivision);
              regenerate_mesh(*ent, selected_uid);
              *ctx.geometry_updated = true;
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
        ImGui::Text("Click a Displacement entity");
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
        auto *ent = get_selected(ctx);
        if (ent) clear_selection(ent->grid_size());
      }
      ImGui::SameLine();
      if (ImGui::Button("Back to Setup"))
      {
        commit_select_edit();
        auto *ent = get_selected(ctx);
        if (ent) clear_selection(ent->grid_size());
        mode = Mode::Setup;
      }
    }
  }
  ImGui::End();
}

} // namespace client
