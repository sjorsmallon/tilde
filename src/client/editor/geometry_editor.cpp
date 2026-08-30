#include "geometry_editor.hpp"
#include "../../shared/asset.hpp"
#include "../../shared/editor_grid.hpp"
#include "../../shared/log.hpp"
#include "../geometry_renderer.hpp"
#include "../render_assets.hpp"
#include "../renderer.hpp"
#include "entity_editor_traits.hpp"
#include "imgui.h"
#include <cmath>
#include <string>

namespace client
{

namespace
{

// A wireframe preview of a mesh. False means the mesh did not resolve and the
// caller should fall back to a box.
bool push_wireframe_mesh(pass_builder_t &draws,
                         assets::asset_handle_t<assets::mesh_asset_t> mesh_asset,
                         const linalg::vec3f &position, const linalg::vec3f &rotation,
                         const linalg::vec3f &scale, color_t color)
{
  const renderer::mesh_handle_t mesh = get_render_mesh(mesh_asset);
  if (!mesh.valid() || !renderer::wireframe_supported())
    return false;

  renderer::mesh_draw_t draw{};
  draw.mesh      = mesh;
  draw.transform = linalg::compose_transform_euler(position, rotation, scale);
  draw.tint      = color;
  draw.fill      = renderer::fill_mode_t::wireframe;
  draws.meshes.push_back(draw);
  return true;
}

// Contours read as an ink line over the grey, not as another light source.
constexpr color_t BRUSH_CONTOUR_COLOR{30, 32, 38};

// Build the hull, then trace it. The callers here all draw a brush the user is
// editing, so the hull has to be rebuilt anyway; a caller holding a brush that
// is NOT changing should hoist the build and call draw_brush_hull_wireframe.
void draw_brush_wireframe(pass_builder_t &draws, const shared::brush_geometry_t &brush,
                          color_t color, float depth_bias,
                          const linalg::vec3 &translation = {0, 0, 0})
{
  std::optional<shared::brush_polyhedron_t> polyhedron =
      shared::try_build_brush_polyhedron(brush.vertices);

  if (!polyhedron)
  {
    // A brush that does not hull has no edges to trace. Show the bound so the
    // object is still selectable and visibly WRONG rather than invisible.
    const shared::aabb_bounds_t bounds = shared::compute_brush_bounds(brush.vertices);
    draws.debug.box((bounds.min + bounds.max) * 0.5f + translation,
                    (bounds.max - bounds.min) * 0.5f, colors::red,
                    renderer::fill_mode_t::wireframe, depth_bias);
    return;
  }

  draw_brush_hull_wireframe(draws, *polyhedron, translation, color, depth_bias);
}

} // namespace

// Every hull edge as a line loop per face. Shared edges are drawn twice, which
// costs nothing at brush sizes and keeps this to one loop with no edge table.
void draw_brush_hull_wireframe(pass_builder_t &draws,
                               const shared::brush_polyhedron_t &hull,
                               const linalg::vec3 &translation, color_t color,
                               float depth_bias)
{
  for (const shared::brush_face_t &face : hull.faces)
  {
    for (size_t i = 0; i < face.vertex_indices.size(); ++i)
    {
      const linalg::vec3 start = hull.vertices[face.vertex_indices[i]] + translation;
      const linalg::vec3 end =
          hull.vertices[face.vertex_indices[(i + 1) % face.vertex_indices.size()]] +
          translation;
      draws.debug.line(start, end, color, depth_bias);
    }
  }
}

// ============================================================================
// Placement
// ============================================================================

linalg::vec3 compute_geometry_placement_center(const shared::geometry_value_t &geometry,
                                               const linalg::vec3 &ghost_position,
                                               float grid_step)
{
  const linalg::vec3 half_extents = shared::get_half_extents(geometry);

  linalg::vec3 center = ghost_position;
  center.y += half_extents.y;

  if (grid_step <= 0.0f)
    return center;

  // Put the LOW corner on a grid line and derive the centre from it, so an
  // object of any size lands where the brush tool would snap a vertex.
  const linalg::vec3 low_corner = center - half_extents;
  const linalg::vec3 aligned{editor::snap(low_corner.x, grid_step),
                             editor::snap(low_corner.y, grid_step),
                             editor::snap(low_corner.z, grid_step)};

  return aligned + half_extents;
}

void draw_geometry_ghost(const shared::geometry_value_t &geometry,
                         pass_builder_t &draws, const linalg::vec3 &center)
{
  // A static mesh previews as its own wireframe mesh when the asset resolves —
  // placing a prop by its bounding box tells you nothing about its orientation.
  if (shared::get_kind(geometry) == shared::geometry_kind_t::Static_Mesh)
  {
    const shared::static_mesh_geometry_t &static_mesh =
        std::get<shared::static_mesh_geometry_t>(geometry);
    const assets::asset_handle_t<assets::mesh_asset_t> mesh_handle =
        shared::resolve_surface_mesh(static_mesh.surface);
    if (push_wireframe_mesh(draws, mesh_handle, center, static_mesh.orientation,
                            static_mesh.scale, colors::yellow))
      return;
  }

  // A brush's shape IS its point set, so a box around its bound previews a solid
  // it is usually not -- the same reason the selection highlight traces the hull
  // rather than the bound. The hull sits at the brush's own position, so it is
  // traced with the offset that would carry it to `center`.
  if (shared::get_kind(geometry) == shared::geometry_kind_t::Brush)
  {
    draw_brush_wireframe(draws, std::get<shared::brush_geometry_t>(geometry),
                         colors::yellow, 0.0f,
                         center - shared::get_position(geometry));
    return;
  }

  draws.debug.box(center, shared::get_half_extents(geometry), colors::yellow);
}

// ============================================================================
// Viewport drawing
// ============================================================================

void draw_geometry_in_editor(const shared::geometry_value_t &geometry,
                             pass_builder_t &draws, shared::entity_uid_t uid,
                             bool solid, Span<const std::string> materials)
{
  // Brushes carry their shape in their EDGES, not in a surface texture -- the
  // flat grey they draw as says nothing on its own. Contours here rather than in
  // draw_geometry because that one is shared with the game, where an outline
  // around every wall would be a rendering style nobody asked for.
  if (shared::get_kind(geometry) == shared::geometry_kind_t::Brush)
  {
    const shared::brush_geometry_t &brush = std::get<shared::brush_geometry_t>(geometry);
    if (solid)
      draw_geometry(draws, geometry, uid, materials);

    draw_brush_wireframe(draws, brush, BRUSH_CONTOUR_COLOR, -60.0f);
    return;
  }

  draw_geometry(draws, geometry, uid, materials);
}

// ============================================================================
// Selection highlight
// ============================================================================

namespace
{

// Grid lines across all six faces of a box at world-aligned `grid_step`
// intervals, so a selected brush reads as a measurable volume rather than just
// an outline. Ported unchanged from the AABB entity's editor trait.
void draw_box_face_grid(pass_builder_t &draws, const linalg::vec3 &position,
                        const linalg::vec3 &half_extents, color_t color,
                        float grid_step, float depth_bias)
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
      draws.debug.line({x, y, za}, {x, y, zb}, color, depth_bias);
    float zs = std::ceil(za / grid_step) * grid_step;
    for (float z = zs; z <= zb + 1e-3f; z += grid_step)
      draws.debug.line({xa, y, z}, {xb, y, z}, color, depth_bias);
  };

  auto draw_grid_xz_outer = [&](float y, float xa, float xb, float za, float zb)
  {
    // Always draw the 4 outer edges regardless of grid alignment
    draws.debug.line({xa, y, za}, {xb, y, za}, color, depth_bias);
    draws.debug.line({xb, y, za}, {xb, y, zb}, color, depth_bias);
    draws.debug.line({xb, y, zb}, {xa, y, zb}, color, depth_bias);
    draws.debug.line({xa, y, zb}, {xa, y, za}, color, depth_bias);
    draw_grid_xz(y, xa, xb, za, zb);
  };

  auto draw_grid_yz = [&](float x, float ya, float yb, float za, float zb)
  {
    float ys = std::ceil(ya / grid_step) * grid_step;
    for (float y = ys; y <= yb + 1e-3f; y += grid_step)
      draws.debug.line({x, y, za}, {x, y, zb}, color, depth_bias);
    float zs = std::ceil(za / grid_step) * grid_step;
    for (float z = zs; z <= zb + 1e-3f; z += grid_step)
      draws.debug.line({x, ya, z}, {x, yb, z}, color, depth_bias);
  };

  auto draw_grid_xy = [&](float z, float xa, float xb, float ya, float yb)
  {
    float xs = std::ceil(xa / grid_step) * grid_step;
    for (float x = xs; x <= xb + 1e-3f; x += grid_step)
      draws.debug.line({x, ya, z}, {x, yb, z}, color, depth_bias);
    float ys = std::ceil(ya / grid_step) * grid_step;
    for (float y = ys; y <= yb + 1e-3f; y += grid_step)
      draws.debug.line({xa, y, z}, {xb, y, z}, color, depth_bias);
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
                                       pass_builder_t &draws, float time,
                                       float grid_step)
{
  const color_t color = compute_selection_pulse_color(time);

  // Strong bias so the highlight renders in FRONT of the solid surface it
  // traces. It rides each line rather than being set and restored around the
  // switch below: three early-return paths each had to remember the restore,
  // and one of them getting it wrong was invisible.
  constexpr float highlight_bias = -200.0f;

  switch (shared::get_kind(geometry))
  {
  case shared::geometry_kind_t::Static_Mesh:
  {
    const shared::static_mesh_geometry_t &static_mesh =
        std::get<shared::static_mesh_geometry_t>(geometry);
    const assets::asset_handle_t<assets::mesh_asset_t> mesh_handle =
        shared::resolve_surface_mesh(static_mesh.surface);
    if (push_wireframe_mesh(draws, mesh_handle, static_mesh.position,
                            static_mesh.orientation, static_mesh.scale, color))
      break;

    const shared::aabb_bounds_t bounds = shared::get_bounds(geometry);
    draws.debug.box((bounds.min + bounds.max) * 0.5f, (bounds.max - bounds.min) * 0.5f, color,
                    renderer::fill_mode_t::wireframe, highlight_bias);
    break;
  }

  case shared::geometry_kind_t::Brush:
  {
    // The hull itself, not its bound: a brush is usually not box-shaped, and a
    // box around a ramp says nothing about what is selected. The one that IS a
    // box gets the measured face grid instead -- that is what a box brush
    // showed back when Box was its own kind, and a hull outline around it would
    // be strictly less information.
    const shared::brush_geometry_t &brush = std::get<shared::brush_geometry_t>(geometry);
    if (shared::brush_is_axis_aligned_box(brush.vertices))
    {
      const shared::aabb_bounds_t bounds = shared::get_bounds(geometry);
      draw_box_face_grid(draws, (bounds.min + bounds.max) * 0.5f,
                         (bounds.max - bounds.min) * 0.5f, color, grid_step,
                         highlight_bias);
      break;
    }

    draw_brush_wireframe(draws, brush, color, highlight_bias);
    break;
  }
  }
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

bool draw_brush_inspector(shared::brush_geometry_t &brush)
{
  bool changed = false;
  ImGui::TextDisabled("brush");

  const shared::aabb_bounds_t bounds = shared::compute_brush_bounds(brush.vertices);
  const linalg::vec3          size   = bounds.max - bounds.min;

  ImGui::Text("%zu vertices", brush.vertices.size());
  ImGui::Text("size  %.1f  %.1f  %.1f", size.x, size.y, size.z);

  if (!shared::try_build_brush_polyhedron(brush.vertices))
    ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "does not form a solid");

  // The bounds centre is the only position a brush has, and writing it back
  // translates the point set. There are no half_extents to edit here: resizing a
  // brush is what dragging its faces is for.
  linalg::vec3 center = (bounds.min + bounds.max) * 0.5f;
  if (ImGui::DragFloat3("position", &center.x, 0.5f))
  {
    shared::geometry_value_t value = brush;
    shared::set_position(value, center);
    brush = std::get<shared::brush_geometry_t>(value);
    changed = true;
  }

  changed |= draw_surface_inspector(brush.surface);
  return changed;
}

} // namespace

bool draw_geometry_inspector(shared::geometry_value_t &geometry)
{
  switch (shared::get_kind(geometry))
  {
  case shared::geometry_kind_t::Static_Mesh:
    return draw_static_mesh_inspector(std::get<shared::static_mesh_geometry_t>(geometry));

  case shared::geometry_kind_t::Brush:
    return draw_brush_inspector(std::get<shared::brush_geometry_t>(geometry));
  }

  log_error("draw_geometry_inspector: unhandled geometry kind {}",
            (int)shared::get_kind(geometry));
  return false;
}

} // namespace client
