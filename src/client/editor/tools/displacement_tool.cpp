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
}

void Displacement_Tool::on_disable(editor_context_t &ctx)
{
  painting = false;
  cursor_valid = false;
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

// ===================================================================
// Update
// ===================================================================

void Displacement_Tool::on_update(editor_context_t &ctx,
                                  const viewport_state_t &view)
{
  if (!ctx.map)
    return;

  if (mode == Mode::Setup)
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
                                             view.mouse_ray.dir, aabb, t, face))
              {
                hovered_face = face;
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
          // First click on a face: initialize displacement on that face
          ent->init_displacement(hovered_face, pending_subdivision);
          regenerate_mesh(*ent, selected_uid);
          *ctx.geometry_updated = true;
        }
      }
    }
  }
  else if (mode == Mode::Paint)
  {
    painting = true;
  }
}

void Displacement_Tool::on_mouse_drag(editor_context_t &ctx,
                                      const mouse_event_t &e)
{
  // Painting is handled in on_update while painting == true
}

void Displacement_Tool::on_mouse_up(editor_context_t &ctx,
                                    const mouse_event_t &e)
{
  painting = false;
}

void Displacement_Tool::on_key_down(editor_context_t &ctx,
                                    const key_event_t &e)
{
  if (e.scancode == SDL_SCANCODE_P)
  {
    auto *ent = get_selected(ctx);
    if (ent && ent->active_face >= 0)
      mode = Mode::Paint;
  }
  else if (e.scancode == SDL_SCANCODE_ESCAPE)
  {
    if (mode == Mode::Paint)
      mode = Mode::Setup;
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
            ImGui::Text("Click a face to start displacement");
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
      ImGui::Text("ESC: back to setup");

      if (ImGui::Button("Back to Setup (ESC)"))
      {
        mode = Mode::Setup;
      }
    }
  }
  ImGui::End();
}

} // namespace client
