#include "geometry_editor.hpp"
#include "../../shared/asset.hpp"
#include "../../shared/editor_grid.hpp"
#include "../../shared/log.hpp"
#include "../geometry_renderer.hpp"
#include "../renderer.hpp"
#include "entity_editor_traits.hpp"
#include "imgui.h"
#include <cmath>
#include <string>

namespace client
{

// ============================================================================
// Placement
// ============================================================================

linalg::vec3 compute_geometry_placement_center(const shared::geometry_value_t &geometry,
                                               const linalg::vec3 &ghost_position)
{
  linalg::vec3 center = ghost_position;
  center.y += shared::get_half_extents(geometry).y;
  return center;
}

void draw_geometry_ghost(const shared::geometry_value_t &geometry,
                         overlay_renderer_t &renderer, const linalg::vec3 &center)
{
  // A static mesh previews as its own wireframe mesh when the asset resolves —
  // placing a prop by its bounding box tells you nothing about its orientation.
  if (shared::get_kind(geometry) == shared::geometry_kind_t::Static_Mesh)
  {
    const shared::static_mesh_geometry_t &static_mesh =
        std::get<shared::static_mesh_geometry_t>(geometry);
    const assets::asset_handle_t<assets::mesh_asset_t> mesh_handle =
        shared::resolve_surface_mesh(static_mesh.surface);
    if (mesh_handle.valid() && renderer::WireframeSupported())
    {
      renderer::draw_mesh(renderer.get_command_buffer(), mesh_handle,
                          {.position = center,
                           .scale = static_mesh.scale,
                           .rotation = static_mesh.orientation,
                           .color = colors::yellow,
                           .wireframe = true});
      return;
    }
  }

  renderer.draw_wire_box(center, shared::get_half_extents(geometry), colors::yellow);
}

// ============================================================================
// Viewport drawing
// ============================================================================

void draw_geometry_in_editor(const shared::geometry_value_t &geometry,
                             overlay_renderer_t &renderer, shared::entity_uid_t uid,
                             bool solid)
{
  // A box with no mesh is the one case the editor draws differently from the
  // game: random per-uid colors so adjacent brushes are visually separable, and
  // wireframe when the user has solid mode off.
  if (shared::get_kind(geometry) == shared::geometry_kind_t::Box)
  {
    const shared::box_geometry_t &box = std::get<shared::box_geometry_t>(geometry);
    if (box.surface.mesh_path.empty())
    {
      renderer::draw_AABB(renderer.get_command_buffer(),
                         box.position - box.half_extents,
                         box.position + box.half_extents, colors::white,
                         /*as_wireframe=*/!solid,
                         /*random_color=*/true,
                         /*random_seed=*/uid);
      return;
    }
  }

  draw_geometry(renderer.get_command_buffer(), geometry, uid);
}

// ============================================================================
// Selection highlight
// ============================================================================

namespace
{

// Grid lines across all six faces of a box at world-aligned `grid_step`
// intervals, so a selected brush reads as a measurable volume rather than just
// an outline. Ported unchanged from the AABB entity's editor trait.
void draw_box_face_grid(overlay_renderer_t &renderer, const linalg::vec3 &position,
                        const linalg::vec3 &half_extents, color_t color,
                        float grid_step)
{
  const linalg::vec3 &p = position;
  const linalg::vec3 &h = half_extents;
  const float x0 = p.x - h.x, x1 = p.x + h.x;
  const float y0 = p.y - h.y, y1 = p.y + h.y;
  const float z0 = p.z - h.z, z1 = p.z + h.z;

  auto draw_grid_xz = [&](float y, float xa, float xb, float za, float zb)
  {
    float xs = std::ceil(xa / grid_step) * grid_step;
    for (float x = xs; x <= xb + 1e-3f; x += grid_step)
      renderer.draw_line({x, y, za}, {x, y, zb}, color);
    float zs = std::ceil(za / grid_step) * grid_step;
    for (float z = zs; z <= zb + 1e-3f; z += grid_step)
      renderer.draw_line({xa, y, z}, {xb, y, z}, color);
  };

  auto draw_grid_xz_outer = [&](float y, float xa, float xb, float za, float zb)
  {
    // Always draw the 4 outer edges regardless of grid alignment
    renderer.draw_line({xa, y, za}, {xb, y, za}, color);
    renderer.draw_line({xb, y, za}, {xb, y, zb}, color);
    renderer.draw_line({xb, y, zb}, {xa, y, zb}, color);
    renderer.draw_line({xa, y, zb}, {xa, y, za}, color);
    draw_grid_xz(y, xa, xb, za, zb);
  };

  auto draw_grid_yz = [&](float x, float ya, float yb, float za, float zb)
  {
    float ys = std::ceil(ya / grid_step) * grid_step;
    for (float y = ys; y <= yb + 1e-3f; y += grid_step)
      renderer.draw_line({x, y, za}, {x, y, zb}, color);
    float zs = std::ceil(za / grid_step) * grid_step;
    for (float z = zs; z <= zb + 1e-3f; z += grid_step)
      renderer.draw_line({x, ya, z}, {x, yb, z}, color);
  };

  auto draw_grid_xy = [&](float z, float xa, float xb, float ya, float yb)
  {
    float xs = std::ceil(xa / grid_step) * grid_step;
    for (float x = xs; x <= xb + 1e-3f; x += grid_step)
      renderer.draw_line({x, ya, z}, {x, yb, z}, color);
    float ys = std::ceil(ya / grid_step) * grid_step;
    for (float y = ys; y <= yb + 1e-3f; y += grid_step)
      renderer.draw_line({xa, y, z}, {xb, y, z}, color);
  };

  // Top and bottom (Y faces)
  draw_grid_xz_outer(y1, x0, x1, z0, z1);
  draw_grid_xz_outer(y0, x0, x1, z0, z1);
  // Left and right (X faces) — vertical edges already drawn above
  draw_grid_yz(x0, y0, y1, z0, z1);
  draw_grid_yz(x1, y0, y1, z0, z1);
  // Front and back (Z faces)
  draw_grid_xy(z0, x0, x1, y0, y1);
  draw_grid_xy(z1, x0, x1, y0, y1);
}

} // namespace

void draw_geometry_selection_highlight(const shared::geometry_value_t &geometry,
                                       overlay_renderer_t &renderer, float time,
                                       float grid_step)
{
  const color_t color = compute_selection_pulse_color(time);

  // Strong depth bias so the highlight renders in front of the solid surface.
  renderer::set_line_depth_bias(-200.0f, -10.0f);

  switch (shared::get_kind(geometry))
  {
  case shared::geometry_kind_t::Box:
  {
    const shared::box_geometry_t &box = std::get<shared::box_geometry_t>(geometry);
    draw_box_face_grid(renderer, box.position, box.half_extents, color, grid_step);
    break;
  }

  case shared::geometry_kind_t::Static_Mesh:
  {
    const shared::static_mesh_geometry_t &static_mesh =
        std::get<shared::static_mesh_geometry_t>(geometry);
    const assets::asset_handle_t<assets::mesh_asset_t> mesh_handle =
        shared::resolve_surface_mesh(static_mesh.surface);
    if (mesh_handle.valid() && renderer::WireframeSupported())
    {
      renderer::draw_mesh(renderer.get_command_buffer(), mesh_handle,
                          {.position = static_mesh.position,
                           .scale = static_mesh.scale,
                           .rotation = static_mesh.orientation,
                           .color = color,
                           .wireframe = true});
      break;
    }

    const shared::aabb_bounds_t bounds = shared::get_bounds(geometry);
    renderer.draw_wire_box((bounds.min + bounds.max) * 0.5f,
                           (bounds.max - bounds.min) * 0.5f, color);
    break;
  }

  case shared::geometry_kind_t::Displacement:
  {
    // The box bound, not the displaced surface: it's the volume the user is
    // resizing, and it's what the sculpting brush rays against.
    const shared::displacement_geometry_t &displacement =
        std::get<shared::displacement_geometry_t>(geometry);
    renderer.draw_wire_box(displacement.position, displacement.half_extents, color);
    break;
  }
  }

  renderer::set_line_depth_bias(-2.0f, -1.0f);
}

// ============================================================================
// Inspector panels
//
// Four small handwritten panels — 3 to 6 properties each. This is what the
// schema system was buying for geometry: about eighty lines of ImGui calls, in
// exchange for a blittable / fixed-size / memcmp-comparable constraint on every
// field, including a 3267-float array with a hard subdivision cap. Handwriting
// them also means each kind gets the widget that actually suits it (a face
// dropdown, a subdivision slider that resamples the grid) instead of whatever
// the field's type happened to map onto.
// ============================================================================

namespace
{

bool draw_surface_inspector(shared::geometry_surface_t &surface)
{
  bool changed = false;

  if (!ImGui::CollapsingHeader("Surface", ImGuiTreeNodeFlags_DefaultOpen))
    return false;

  ImGui::PushID("surface");

  // ImGui needs a fixed buffer for text input; 256 comfortably covers a path.
  char mesh_path_buffer[256];
  std::snprintf(mesh_path_buffer, sizeof(mesh_path_buffer), "%s",
                surface.mesh_path.c_str());
  if (ImGui::InputText("mesh_path", mesh_path_buffer, sizeof(mesh_path_buffer)))
  {
    surface.mesh_path = mesh_path_buffer;
    changed = true;
  }

  const char *shader_names[] = {"lit", "unlit"};
  int shader_index = (surface.shader_type == "unlit") ? 1 : 0;
  if (ImGui::Combo("shader", &shader_index, shader_names, 2))
  {
    surface.shader_type = shader_names[shader_index];
    changed = true;
  }

  changed |= ImGui::ColorEdit3("color", &surface.color.x);
  changed |= ImGui::DragFloat("roughness", &surface.roughness, 0.01f, 0.f, 1.f);
  changed |= ImGui::Checkbox("visible", &surface.visible);
  ImGui::SameLine();
  changed |= ImGui::Checkbox("wireframe", &surface.is_wireframe);

  ImGui::PopID();
  return changed;
}

bool draw_box_inspector(shared::box_geometry_t &box)
{
  bool changed = false;
  ImGui::TextDisabled("box");
  changed |= ImGui::DragFloat3("position", &box.position.x, 0.5f);
  changed |= ImGui::DragFloat3("half_extents", &box.half_extents.x, 0.5f);
  changed |= draw_surface_inspector(box.surface);
  return changed;
}

bool draw_static_mesh_inspector(shared::static_mesh_geometry_t &static_mesh)
{
  bool changed = false;
  ImGui::TextDisabled("static mesh");
  changed |= ImGui::DragFloat3("position", &static_mesh.position.x, 0.5f);
  changed |= ImGui::DragFloat3("orientation", &static_mesh.orientation.x, 1.0f);
  changed |= ImGui::DragFloat3("scale", &static_mesh.scale.x, 0.01f);
  changed |= draw_surface_inspector(static_mesh.surface);
  return changed;
}

bool draw_displacement_inspector(shared::displacement_geometry_t &displacement)
{
  bool changed = false;
  ImGui::TextDisabled("displacement");
  changed |= ImGui::DragFloat3("position", &displacement.position.x, 0.5f);
  changed |= ImGui::DragFloat3("half_extents", &displacement.half_extents.x, 0.5f);

  // active_face as a named dropdown. "none" is index 0 and maps to Invalid, so
  // the enum's -1 never has to be typed into a widget.
  const char *face_names[] = {"none", "+X", "-X", "+Y", "-Y", "+Z", "-Z"};
  int face_index = (displacement.active_face == shared::box_face_t::Invalid)
                       ? 0
                       : (int)displacement.active_face + 1;
  if (ImGui::Combo("active_face", &face_index, face_names,
                   (int)shared::box_face_count + 1))
  {
    displacement.active_face = (face_index == 0)
                                   ? shared::box_face_t::Invalid
                                   : (shared::box_face_t)(face_index - 1);
    changed = true;
  }

  // Changing subdivision resamples the grid rather than flattening it, so
  // dragging this doesn't throw away sculpting work.
  int subdivision_level = displacement.subdivision_level;
  if (ImGui::SliderInt("subdivision", &subdivision_level, 1, 64))
  {
    displacement.resize_grid_preserving(subdivision_level);
    changed = true;
  }

  ImGui::TextDisabled("%d x %d grid, %zu vertices", displacement.grid_size(),
                      displacement.grid_size(), displacement.displacements.size());

  changed |= draw_surface_inspector(displacement.surface);
  return changed;
}

} // namespace

bool draw_geometry_inspector(shared::geometry_value_t &geometry)
{
  switch (shared::get_kind(geometry))
  {
  case shared::geometry_kind_t::Box:
    return draw_box_inspector(std::get<shared::box_geometry_t>(geometry));

  case shared::geometry_kind_t::Static_Mesh:
    return draw_static_mesh_inspector(std::get<shared::static_mesh_geometry_t>(geometry));

  case shared::geometry_kind_t::Displacement:
    return draw_displacement_inspector(
        std::get<shared::displacement_geometry_t>(geometry));
  }

  log_error("draw_geometry_inspector: unhandled geometry kind {}",
            (int)shared::get_kind(geometry));
  return false;
}

} // namespace client
