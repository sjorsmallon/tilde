#include "displacement_tool.hpp"
#include "../../../shared/collision_detection.hpp"
#include "../../../shared/shapes.hpp"
#include "imgui.h"
#include "renderer.hpp"
#include <SDL.h>
#include <cmath>

namespace client
{

// ===================================================================
// Ray-AABB face intersection (copied from sculpting tool)
// ===================================================================

static bool ray_aabb_face_intersection(const linalg::vec3 &ray_origin,
                                       const linalg::vec3 &ray_dir,
                                       const shared::aabb_t &aabb, float &out_t,
                                       int &out_face)
{
  linalg::vec3 min = aabb.center - aabb.half_extents;
  linalg::vec3 max = aabb.center + aabb.half_extents;

  float tmin = 0.0f;
  float tmax = 1e30f;

  auto slab = [&](float origin, float dir, float mn, float mx) -> bool
  {
    if (std::abs(dir) < 1e-6f)
      return (origin >= mn && origin <= mx);
    float ood = 1.0f / dir;
    float t1 = (mn - origin) * ood;
    float t2 = (mx - origin) * ood;
    if (t1 > t2)
      std::swap(t1, t2);
    if (t1 > tmin)
      tmin = t1;
    if (t2 < tmax)
      tmax = t2;
    return tmin <= tmax;
  };

  if (!slab(ray_origin.x, ray_dir.x, min.x, max.x))
    return false;
  if (!slab(ray_origin.y, ray_dir.y, min.y, max.y))
    return false;
  if (!slab(ray_origin.z, ray_dir.z, min.z, max.z))
    return false;

  out_t = tmin;
  linalg::vec3 p = ray_origin + ray_dir * tmin;
  const float eps = 1e-3f;

  if (std::abs(p.x - max.x) < eps)
    out_face = 0;
  else if (std::abs(p.x - min.x) < eps)
    out_face = 1;
  else if (std::abs(p.y - max.y) < eps)
    out_face = 2;
  else if (std::abs(p.y - min.y) < eps)
    out_face = 3;
  else if (std::abs(p.z - max.z) < eps)
    out_face = 4;
  else if (std::abs(p.z - min.z) < eps)
    out_face = 5;
  else
    return false;

  return true;
}

// ===================================================================
// Moller-Trumbore ray-triangle intersection
// ===================================================================

static bool ray_triangle(const linalg::vec3 &origin, const linalg::vec3 &dir,
                         const linalg::vec3 &v0, const linalg::vec3 &v1,
                         const linalg::vec3 &v2, float &out_t)
{
  linalg::vec3 e1 = v1 - v0;
  linalg::vec3 e2 = v2 - v0;
  linalg::vec3 h = linalg::cross(dir, e2);
  float a = linalg::dot(e1, h);
  if (std::abs(a) < 1e-8f)
    return false;
  float f = 1.0f / a;
  linalg::vec3 s = origin - v0;
  float u = f * linalg::dot(s, h);
  if (u < 0.0f || u > 1.0f)
    return false;
  linalg::vec3 q = linalg::cross(s, e1);
  float v = f * linalg::dot(dir, q);
  if (v < 0.0f || u + v > 1.0f)
    return false;
  float t = f * linalg::dot(e2, q);
  if (t < 1e-4f)
    return false;
  out_t = t;
  return true;
}

// ===================================================================
// Lifecycle
// ===================================================================

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

  if (resize_dragging && resize_edit && ctx.transaction_system)
  {
    resize_edit->finish(selected_uid);
    if (auto txn = resize_edit->take())
      ctx.transaction_system->push(*txn);
  }
  resize_dragging = false;
  resize_edit.reset();
}

// ===================================================================
// Helpers
// ===================================================================

network::Displacement_Entity *
Displacement_Tool::get_selected(editor_context_t &ctx)
{
  if (selected_uid == 0 || !ctx.map)
    return nullptr;
  auto *entry = ctx.map->find_by_uid(selected_uid);
  if (!entry || !entry->entity)
    return nullptr;
  return dynamic_cast<network::Displacement_Entity *>(entry->entity.get());
}

bool Displacement_Tool::raycast_displacement_mesh(
    const network::Displacement_Entity &ent, const linalg::vec3 &ray_origin,
    const linalg::vec3 &ray_dir, float &out_t, linalg::vec3 &out_normal)
{
  if (ent.active_face < 0)
    return false;

  int gs = ent.grid_size();
  float best_t = 1e30f;
  linalg::vec3 best_normal = {0, 1, 0};
  bool hit = false;

  for (int j = 0; j < gs - 1; ++j)
  {
    for (int i = 0; i < gs - 1; ++i)
    {
      linalg::vec3 tl = ent.get_vertex_world(i, j);
      linalg::vec3 tr = ent.get_vertex_world(i + 1, j);
      linalg::vec3 bl = ent.get_vertex_world(i, j + 1);
      linalg::vec3 br = ent.get_vertex_world(i + 1, j + 1);

      float t;
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
  if (!cursor_valid || ent.active_face < 0)
    return;

  int gs = ent.grid_size();
  linalg::vec3 face_normal = ent.get_face_normal();
  float sign = invert ? -1.0f : 1.0f;
  float sigma = 0.33f;

  for (int j = 0; j < gs; ++j)
  {
    for (int i = 0; i < gs; ++i)
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
  linalg::vec3 cam_pos = {cached_view.camera.x, cached_view.camera.y,
                          cached_view.camera.z};
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
  if (select_edit)
  {
    // select_edit tracks changes; just drop it (changes are already applied).
    select_edit.reset();
  }
}

void Displacement_Tool::clear_selection(int gs)
{
  sel_verts.assign((size_t)(gs * gs), false);
}

// ===================================================================
// Update
// ===================================================================

void Displacement_Tool::on_update(editor_context_t &ctx,
                                  const viewport_state_t &view)
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
      hovered_face = -1;

      if (ctx.bvh)
      {
        Ray_Hit hit;
        if (bvh_intersect_ray(*ctx.bvh, view.mouse_ray.origin,
                               view.mouse_ray.dir, hit))
        {
          if (hit.id.type == Collision_Id::Type::Static_Geometry)
          {
            shared::entity_uid_t uid = hit.id.index;
            auto *entry = ctx.map->find_by_uid(uid);
            if (entry && entry->entity)
            {
              if (auto *disp = dynamic_cast<network::Displacement_Entity *>(
                      entry->entity.get()))
              {
                hovered_uid = uid;

                // Face picking on the AABB bounds
                shared::aabb_t aabb;
                aabb.center = disp->position;
                aabb.half_extents = disp->half_extents;
                float t;
                int face;
                if (ray_aabb_face_intersection(view.mouse_ray.origin,
                                               view.mouse_ray.dir, aabb, t,
                                               face))
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
      bool invert = ImGui::GetIO().KeyShift;
      apply_brush(*ent, 1.0f / 60.0f, invert); // Approximate dt
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
  if (e.button != 1)
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

        if (ent->active_face < 0 && hovered_face >= 0)
        {
          if (e.shift_down)
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
              resize_edit.emplace(*ctx.map);
              resize_edit->track(selected_uid);
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
    box_start_screen = {(float)e.pos.x, (float)e.pos.y};
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
    vec3 current_he = ent->half_extents;

    vec3 normal = {0, 0, 0};
    vec3 center_offset = {0, 0, 0};
    switch (resize_face)
    {
    case 0: normal = {1, 0, 0}; center_offset = {current_he.x, 0, 0}; break;
    case 1: normal = {-1, 0, 0}; center_offset = {-current_he.x, 0, 0}; break;
    case 2: normal = {0, 1, 0}; center_offset = {0, current_he.y, 0}; break;
    case 3: normal = {0, -1, 0}; center_offset = {0, -current_he.y, 0}; break;
    case 4: normal = {0, 0, 1}; center_offset = {0, 0, current_he.z}; break;
    case 5: normal = {0, 0, -1}; center_offset = {0, 0, -current_he.z}; break;
    }

    vec3 face_center_world = current_center + center_offset;
    vec3 face_end_world = face_center_world + normal;

    vec3 cam_pos = {resize_last_view.camera.x, resize_last_view.camera.y,
                    resize_last_view.camera.z};
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
      vec2 s0 = view_to_screen(v0, resize_last_view.display_size,
                                resize_last_view.camera.orthographic,
                                resize_last_view.camera.ortho_height,
                                resize_last_view.fov);
      vec2 s1 = view_to_screen(v1, resize_last_view.display_size,
                                resize_last_view.camera.orthographic,
                                resize_last_view.camera.ortho_height,
                                resize_last_view.fov);

      vec2 screen_dir = {s1.x - s0.x, s1.y - s0.y};
      float screen_len_sq =
          screen_dir.x * screen_dir.x + screen_dir.y * screen_dir.y;

      if (screen_len_sq > 1e-4f)
      {
        vec2 mouse_delta = {(float)e.delta.x, (float)e.delta.y};
        float dot_prod =
            mouse_delta.x * screen_dir.x + mouse_delta.y * screen_dir.y;
        float k = dot_prod / screen_len_sq;
        float world_delta = k;

        float *ext = nullptr;
        float *cen = nullptr;

        if (resize_face < 2)
        {
          ext = &ent->half_extents.x;
          cen = &ent->position.x;
        }
        else if (resize_face < 4)
        {
          ext = &ent->half_extents.y;
          cen = &ent->position.y;
        }
        else
        {
          ext = &ent->half_extents.z;
          cen = &ent->position.z;
        }

        *ext += world_delta * 0.5f;
        if (resize_face % 2 == 0)
          *cen += world_delta * 0.5f;
        else
          *cen -= world_delta * 0.5f;

        if (*ext < editor::MIN_EXTENT)
        {
          float diff = editor::MIN_EXTENT - *ext;
          *ext = editor::MIN_EXTENT;
          if (resize_face % 2 == 0)
            *cen -= diff;
          else
            *cen += diff;
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
    if (resize_edit && ctx.transaction_system)
    {
      resize_edit->finish(selected_uid);
      if (auto txn = resize_edit->take())
        ctx.transaction_system->push(*txn);
    }
    if (ctx.geometry_updated)
      *ctx.geometry_updated = true;
    resize_edit.reset();
  }

  if (mode == Mode::Select && box_selecting)
  {
    box_selecting = false;

    auto *ent = get_selected(ctx);
    if (ent && ent->active_face >= 0)
    {
      int gs = ent->grid_size();
      sel_verts.resize((size_t)(gs * gs), false);

      float x0 = std::min(box_start_screen.x, box_end_screen.x);
      float x1 = std::max(box_start_screen.x, box_end_screen.x);
      float y0 = std::min(box_start_screen.y, box_end_screen.y);
      float y1 = std::max(box_start_screen.y, box_end_screen.y);

      // Shift = additive selection; otherwise replace
      if (!(ImGui::GetIO().KeyShift))
        clear_selection(gs);

      for (int j = 0; j < gs; ++j)
      {
        for (int i = 0; i < gs; ++i)
        {
          linalg::vec2 sp = project_to_screen(ent->get_vertex_world(i, j));
          if (sp.x >= x0 && sp.x <= x1 && sp.y >= y0 && sp.y <= y1)
            sel_verts[(size_t)(j * gs + i)] = true;
        }
      }
    }
  }
}

void Displacement_Tool::on_key_down(editor_context_t &ctx,
                                    const key_event_t &e)
{
  if (e.scancode == SDL_SCANCODE_P)
  {
    auto *ent = get_selected(ctx);
    if (ent && ent->active_face >= 0)
    {
      commit_select_edit();
      mode = Mode::Paint;
    }
  }
  else if (e.scancode == SDL_SCANCODE_S)
  {
    auto *ent = get_selected(ctx);
    if (ent && ent->active_face >= 0)
    {
      if (mode != Mode::Select)
      {
        mode = Mode::Select;
        int gs = ent->grid_size();
        if ((int)sel_verts.size() != gs * gs)
          clear_selection(gs);
      }
    }
  }
  else if (e.scancode == SDL_SCANCODE_ESCAPE)
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
           (e.scancode == SDL_SCANCODE_Q || e.scancode == SDL_SCANCODE_E))
  {
    auto *ent = get_selected(ctx);
    if (!ent || ent->active_face < 0)
      return;

    int gs = ent->grid_size();
    if ((int)sel_verts.size() != gs * gs)
      return;

    // Count selected verts
    int sel_count = 0;
    for (bool b : sel_verts)
      if (b) ++sel_count;
    if (sel_count == 0)
      return;

    // Begin transaction on first height change (reset each time we enter Select mode)
    if (!select_edit && ctx.transaction_system && ctx.map)
    {
      select_edit.emplace(*ctx.map);
      select_edit->track(selected_uid);
    }

    float sign = (e.scancode == SDL_SCANCODE_Q) ? 1.0f : -1.0f;
    linalg::vec3 face_normal = ent->get_face_normal();
    linalg::vec3 delta = face_normal * (sign * height_snap);

    for (int j = 0; j < gs; ++j)
    {
      for (int i = 0; i < gs; ++i)
      {
        if (sel_verts[(size_t)(j * gs + i)])
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
  if (mode == Mode::Setup && hovered_uid != 0 && hovered_face >= 0 && ctx.map)
  {
    auto *entry = ctx.map->find_by_uid(hovered_uid);
    if (entry && entry->entity)
    {
      if (auto *disp = dynamic_cast<network::Displacement_Entity *>(
              entry->entity.get()))
      {
        linalg::vec3 p = disp->position;
        linalg::vec3 he = disp->half_extents;
        linalg::vec3 size = he;

        switch (hovered_face)
        {
        case 0: p.x += he.x; size.x = 0; break;
        case 1: p.x -= he.x; size.x = 0; break;
        case 2: p.y += he.y; size.y = 0; break;
        case 3: p.y -= he.y; size.y = 0; break;
        case 4: p.z += he.z; size.z = 0; break;
        case 5: p.z -= he.z; size.z = 0; break;
        }

        renderer.draw_wire_box(p, size, 0xFF00FF00); // Green highlight
      }
    }
  }

  // Draw the grid wireframe for the selected displacement entity
  if (selected_uid != 0 && ctx.map)
  {
    auto *ent = get_selected(ctx);
    if (ent && ent->active_face >= 0)
    {
      int gs = ent->grid_size();
      uint32_t grid_color = 0xFF444444;

      // Draw grid lines along i
      for (int j = 0; j < gs; ++j)
      {
        for (int i = 0; i < gs - 1; ++i)
        {
          renderer.draw_line(ent->get_vertex_world(i, j),
                             ent->get_vertex_world(i + 1, j), grid_color);
        }
      }
      // Draw grid lines along j
      for (int i = 0; i < gs; ++i)
      {
        for (int j = 0; j < gs - 1; ++j)
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
    renderer.draw_circle(cursor_pos, brush_radius, cursor_normal, 0xFF00FFFF);

    // Draw normal arrow
    linalg::vec3 arrow_end = cursor_pos + cursor_normal * (brush_radius * 0.5f);
    renderer.draw_line(cursor_pos, arrow_end, 0xFF0000FF);
  }

  // In select mode: highlight selected vertices and draw dragging box
  if (mode == Mode::Select && selected_uid != 0 && ctx.map)
  {
    auto *ent = get_selected(ctx);
    if (ent && ent->active_face >= 0)
    {
      int gs = ent->grid_size();
      if ((int)sel_verts.size() == gs * gs)
      {
        const float dot_r = 0.4f;
        linalg::vec3 fn = ent->get_face_normal();
        for (int j = 0; j < gs; ++j)
        {
          for (int i = 0; i < gs; ++i)
          {
            if (sel_verts[(size_t)(j * gs + i)])
            {
              linalg::vec3 vp = ent->get_vertex_world(i, j);
              renderer.draw_circle(vp, dot_r, fn, 0xFFFFFF00); // Yellow dot
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

          if (ent->active_face >= 0)
          {
            const char *face_names[] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
            ImGui::Text("Face: %s", face_names[ent->active_face]);
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
      for (bool b : sel_verts)
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
