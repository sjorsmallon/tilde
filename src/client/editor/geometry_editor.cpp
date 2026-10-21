#include "geometry_editor.hpp"
#include "../../shared/assets/asset.hpp"
#include "../../shared/map/brush/brush_holes.hpp"
#include "../../shared/map/map.hpp"
#include "../../shared/map/map_geometry_mesh.hpp"
#include "../../shared/map/editor_grid.hpp"
#include "../../shared/frame_timing.hpp"
#include "../../shared/log.hpp"
#include "../geometry_renderer.hpp"
#include "../render_assets.hpp"
#include "../renderer.hpp"
#include "entity_editor_traits.hpp"
#include "entity_inspector.hpp"
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
                         const linalg::vec3f& position, const linalg::quatf& rotation,
                         const linalg::vec3f& scale, color_t color)
{
  const renderer::mesh_handle_t mesh = get_render_mesh(mesh_asset);
  if (!mesh.valid() || !renderer::wireframe_supported())
    return false;

  renderer::mesh_draw_t draw{};
  draw.mesh      = mesh;
  draw.transform = linalg::compose_transform(position, rotation, scale);
  draw.tint      = color;
  draw.fill      = renderer::fill_mode_t::wireframe;
  draws.meshes.push_back(draw);
  return true;
}

// Contours read as an ink line over the grey, not as another light source.
constexpr color_t BRUSH_CONTOUR_COLOR{30, 32, 38};

// A brush get_collision_pieces refused. Loud on purpose -- it is a hole in the
// level, and the contour is the only thing about the brush that would otherwise
// have changed at all.
constexpr color_t NO_COLLISION_CONTOUR_COLOR{230, 40, 40};

// A hole: the volume that was authored, which the wall's mesh no longer shows.
constexpr color_t HOLE_FILL_COLOR{255, 150, 40, 60};
constexpr color_t HOLE_CONTOUR_COLOR{255, 150, 40};

// The box a selected static mesh collides as, and the same box when its
// collision is switched off and clip brushes are expected to stand in for it.
constexpr color_t STATIC_MESH_COLLISION_BOX_COLOR{60, 220, 120};
constexpr color_t STATIC_MESH_NO_COLLISION_BOX_COLOR{120, 120, 120};

// Trace an already-built hull, or the bound when there is none: a brush that
// does not hull has no edges, and the bound keeps it selectable and visibly
// WRONG rather than invisible.
void draw_brush_contour(pass_builder_t &draws, const shared::brush_geometry_t &brush,
                        const std::optional<shared::brush_polyhedron_t> &hull, color_t color,
                        float depth_bias, const linalg::vec3 &translation = {0, 0, 0})
{
  if (!hull)
  {
    const shared::aabb_bounds_t bounds = shared::compute_brush_bounds(brush.hull_points);
    draws.debug.box((bounds.min + bounds.max) * 0.5f + translation,
                    (bounds.max - bounds.min) * 0.5f, colors::red,
                    renderer::fill_mode_t::wireframe, depth_bias);
    return;
  }

  draw_brush_hull_wireframe(draws, *hull, translation, color, depth_bias);
}

// Build the hull, then trace it: for a value in TOOL STATE with no uid to cache
// under. A brush of the map goes through get_cached_brush_hull.
void draw_brush_wireframe(pass_builder_t &draws, const shared::brush_geometry_t &brush,
                          color_t color, float depth_bias,
                          const linalg::vec3 &translation = {0, 0, 0})
{
  draw_brush_contour(draws, brush, shared::try_build_brush_polyhedron(brush.hull_points), color,
                     depth_bias, translation);
}

} // namespace

void draw_mesh_wireframe(pass_builder_t &draws, const shared::mesh_geometry_t &mesh,
                         color_t color, float depth_bias, const linalg::vec3 &translation)
{
  const std::vector<shared::mesh_directed_edge_t> edges =
      shared::collect_mesh_directed_edges(mesh);
  for (const shared::mesh_directed_edge_t &edge : edges)
  {
    if (edge.a > edge.b && shared::try_find_mesh_face_walking_edge(edges, edge.b, edge.a))
      continue;
    const linalg::vec3 start =
        mesh.position + linalg::rotate(mesh.orientation, mesh.vertices[edge.a]) + translation;
    const linalg::vec3 end =
        mesh.position + linalg::rotate(mesh.orientation, mesh.vertices[edge.b]) + translation;
    draws.debug.line(start, end, color, depth_bias);
  }
}

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
  // The POSE is what is placed and the bounds are what is aligned; a mesh's
  // pose need not sit at its bounds centre, so the two are kept apart.
  const shared::aabb_bounds_t bounds = shared::get_bounds(geometry);
  const linalg::vec3 half_extents = (bounds.max - bounds.min) * 0.5f;
  const linalg::vec3 pose_from_bounds_center =
      shared::get_position(geometry) - (bounds.min + bounds.max) * 0.5f;
  linalg::vec3 center = ghost_position;
  center.y += half_extents.y;

  if (grid_step <= 0.0f)
    return center + pose_from_bounds_center;

  // Put the LOW corner on a grid line and derive the centre from it, so an
  // object of any size lands where the brush tool would snap a vertex.
  const linalg::vec3 low_corner = center - half_extents;
  const linalg::vec3 aligned{editor::snap(low_corner.x, grid_step),
                             editor::snap(low_corner.y, grid_step),
                             editor::snap(low_corner.z, grid_step)};

  return aligned + half_extents + pose_from_bounds_center;
}

void draw_geometry_ghost(const shared::geometry_value_t &authored_geometry,
                         pass_builder_t &draws, const linalg::vec3 &center)
{
  shared::geometry_value_t view_storage;
  const shared::geometry_value_t &geometry = drawn_geometry_value(authored_geometry, view_storage);

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

  // A mesh traces its stored edges, carried from its pose to `center`.
  if (const shared::mesh_geometry_t* mesh = std::get_if<shared::mesh_geometry_t>(&geometry))
  {
    draw_mesh_wireframe(draws, *mesh, colors::yellow, 0.0f, center - mesh->position);
    return;
  }

  draws.debug.box(center, shared::get_half_extents(geometry), colors::yellow);
}

// ============================================================================
// Viewport drawing
// ============================================================================

void draw_geometry_in_editor(const shared::geometry_value_t &authored_geometry,
                             pass_builder_t &draws, shared::entity_uid_t uid,
                             bool solid, Span<const std::string> materials,
                             const shared::lightmap_t &lightmap,
                             Span<const shared::map_geometry_t> map_geometry, bool collides,
                             const linalg::vec3 &translation, generated_mesh_check_t check)
{
  shared::geometry_value_t view_storage;
  const shared::geometry_value_t &geometry = drawn_geometry_value(authored_geometry, view_storage);
  const bool          moved = !(translation == linalg::vec3{0, 0, 0});
  const linalg::mat4f moved_by =
      linalg::compose_transform(translation, linalg::quatf::identity(), {1, 1, 1});

  // Brushes carry their shape in their EDGES, not in a surface texture -- the
  // flat grey they draw as says nothing on its own. Contours here rather than in
  // draw_geometry because that one is shared with the game, where an outline
  // around every wall would be a rendering style nobody asked for.
  if (shared::get_kind(geometry) == shared::geometry_kind_t::Brush)
  {
    const shared::brush_geometry_t &brush = std::get<shared::brush_geometry_t>(geometry);
    frame_timing::lap_zone_t lap("editor brush: cached hull (is_current)");
    const std::optional<shared::brush_polyhedron_t> &hull =
        get_cached_brush_hull(geometry, uid, materials, lightmap, map_geometry, check);
    lap.next("editor brush: contour + solid");

    // A hole has no mesh: its wall draws what it leaves. The authored volume is
    // drawn tinted so it stays visible, pickable and movable after the wall is built.
    if (brush.subtracts_from_uid != shared::null_entity_uid)
    {
      if (hull)
      {
        for (std::vector<linalg::vec3> &polygon : shared::brush_face_polygons(*hull))
        {
          for (linalg::vec3 &corner : polygon)
            corner = corner + translation;
          draws.debug.filled_polygon(Span<const linalg::vec3f>(polygon.data(), polygon.size()),
                                     HOLE_FILL_COLOR, 0.f, {.depth_bias = 0.5f});
        }
      }
      draw_brush_contour(draws, brush, hull, HOLE_CONTOUR_COLOR, -60.0f, translation);
      return;
    }

    // get_cached_brush_hull just compared and refreshed this brush, so the draw need not.
    if (solid)
      draw_geometry(draws, geometry, uid, materials, lightmap, map_geometry,
                    moved ? &moved_by : nullptr,
                    geometry_wipe_frame(geometry, linalg::quatf{0.0f, 0.0f, 0.0f, 1.0f}), {},
                    renderer::light_cut_t::none, false, generated_mesh_check_t::Trusted_Since_Load);

    draw_brush_contour(draws, brush, hull,
                       collides ? BRUSH_CONTOUR_COLOR : NO_COLLISION_CONTOUR_COLOR, -60.0f,
                       translation);
    return;
  }

  // A mesh's contour is its stored edges: no hull, nothing cached.
  if (const shared::mesh_geometry_t* mesh = std::get_if<shared::mesh_geometry_t>(&geometry))
  {
    if (solid)
      draw_geometry(draws, geometry, uid, materials, lightmap, map_geometry,
                    moved ? &moved_by : nullptr,
                    geometry_wipe_frame(geometry, linalg::quatf{0.0f, 0.0f, 0.0f, 1.0f}), {},
                    renderer::light_cut_t::none, false, check);
    draw_mesh_wireframe(draws, *mesh, collides ? BRUSH_CONTOUR_COLOR : NO_COLLISION_CONTOUR_COLOR,
                        -60.0f, translation);
    return;
  }

  draw_geometry(draws, geometry, uid, materials, lightmap, map_geometry, moved ? &moved_by : nullptr);
}

std::vector<hole_link_t> refresh_walls_of_changed_holes(const shared::map_t &map,
                                                        Span<const hole_link_t> previous)
{
  std::vector<hole_link_t> current;
  for (const shared::map_geometry_t &entry : map.geometry)
  {
    const shared::brush_geometry_t *brush = std::get_if<shared::brush_geometry_t>(&entry.value);
    if (brush != nullptr && brush->subtracts_from_uid != shared::null_entity_uid)
      current.push_back({entry.uid, brush->subtracts_from_uid, *brush});
  }

  std::vector<shared::entity_uid_t> walls_to_rebuild;
  const auto note_wall = [&walls_to_rebuild](shared::entity_uid_t wall) {
    for (shared::entity_uid_t noted : walls_to_rebuild)
      if (noted == wall)
        return;
    walls_to_rebuild.push_back(wall);
  };

  for (const hole_link_t &link : current)
  {
    const hole_link_t *before = nullptr;
    for (const hole_link_t &candidate : previous)
      if (candidate.hole == link.hole)
        before = &candidate;

    if (before == nullptr)
    {
      note_wall(link.wall);
      continue;
    }
    if (before->wall != link.wall ||
        !shared::geometry_values_equal(shared::geometry_value_t{before->value},
                                       shared::geometry_value_t{link.value}))
    {
      note_wall(before->wall);
      note_wall(link.wall);
    }
  }
  for (const hole_link_t &before : previous)
  {
    bool still_there = false;
    for (const hole_link_t &link : current)
      still_there |= link.hole == before.hole;
    if (!still_there)
      note_wall(before.wall);
  }

  for (shared::entity_uid_t wall : walls_to_rebuild)
  {
    const shared::map_geometry_t *entry = map.find_geometry_by_uid(wall);
    if (entry != nullptr)
      refresh_generated_geometry_mesh(entry->value, wall, map.materials, map.lightmap,
                                      map.geometry);
  }

  return current;
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

constexpr bool DRAW_BOX_BRUSH_FACE_GRID = false;

} // namespace

void draw_geometry_selection_highlight(const shared::geometry_value_t &authored_geometry,
                                       shared::entity_uid_t uid,
                                       Span<const std::string> materials,
                                       const shared::lightmap_t &lightmap,
                                       Span<const shared::map_geometry_t> map_geometry,
                                       pass_builder_t &draws, float time,
                                       float grid_step, bool is_outlined,
                                       const linalg::vec3 &translation)
{
  shared::geometry_value_t view_storage;
  const shared::geometry_value_t &geometry = drawn_geometry_value(authored_geometry, view_storage);
  const color_t color = compute_selection_pulse_color(time);

  if (is_outlined)
  {
    const shared::brush_geometry_t* brush = std::get_if<shared::brush_geometry_t>(&geometry);
    if (DRAW_BOX_BRUSH_FACE_GRID && brush && shared::brush_is_axis_aligned_box(brush->hull_points))
    {
      const shared::aabb_bounds_t bounds = shared::get_bounds(geometry);
      draw_box_face_grid(draws, (bounds.min + bounds.max) * 0.5f + translation,
                         (bounds.max - bounds.min) * 0.5f, with_alpha(color, 0x50), grid_step,
                         -200.0f);
    }
    return;
  }

  // Strong bias so the highlight renders in FRONT of the solid surface it
  // traces. It rides each line rather than being set and restored around the
  // switch below: three early-return paths each had to remember the restore,
  // and one of them getting it wrong was invisible.
  constexpr float highlight_bias = -200.0f;

  switch (shared::get_kind(geometry))
  {
  case shared::geometry_kind_t::Mesh:
    draw_mesh_wireframe(draws, std::get<shared::mesh_geometry_t>(geometry), color, highlight_bias,
                        translation);
    break;

  case shared::geometry_kind_t::Static_Mesh:
  {
    const shared::static_mesh_geometry_t &static_mesh =
        std::get<shared::static_mesh_geometry_t>(geometry);
    const assets::asset_handle_t<assets::mesh_asset_t> mesh_handle =
        shared::resolve_surface_mesh(static_mesh.surface);
    push_wireframe_mesh(draws, mesh_handle, static_mesh.position + translation,
                        static_mesh.orientation, static_mesh.scale, color);

    const color_t box_color =
        static_mesh.collides ? STATIC_MESH_COLLISION_BOX_COLOR : STATIC_MESH_NO_COLLISION_BOX_COLOR;
    for (const std::vector<linalg::vec3>& polygon :
         shared::static_mesh_collision_box(static_mesh).face_polygons)
    {
      for (size_t corner = 0; corner < polygon.size(); ++corner)
        draws.debug.line(polygon[corner] + translation,
                         polygon[(corner + 1) % polygon.size()] + translation, box_color,
                         highlight_bias);
    }
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
    if (DRAW_BOX_BRUSH_FACE_GRID && shared::brush_is_axis_aligned_box(brush.hull_points))
    {
      const shared::aabb_bounds_t bounds = shared::get_bounds(geometry);
      draw_box_face_grid(draws, (bounds.min + bounds.max) * 0.5f + translation,
                         (bounds.max - bounds.min) * 0.5f, color, grid_step,
                         highlight_bias);
      break;
    }

    draw_brush_contour(draws, brush,
                       get_cached_brush_hull(geometry, uid, materials, lightmap, map_geometry),
                       color, highlight_bias, translation);
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

// The mesh assets of this build as a combo over `path`; a path no asset carries
// is said so, not cleared.
bool draw_mesh_asset_combo(std::string &path)
{
  bool changed = false;

  const Span<const assets::asset_entry_t> meshes =
      assets::asset_class_entries(assets::asset_class_t::mesh_asset);

  const char* preview = path.empty() ? "(none)" : path.c_str();
  bool mesh_is_in_build = path.empty();
  for (const assets::asset_entry_t& mesh : meshes)
  {
    if (!mesh.path.empty() && path == mesh.path)
    {
      preview = mesh.name.c_str();
      mesh_is_in_build = true;
    }
  }

  if (ImGui::BeginCombo("mesh", preview))
  {
    if (ImGui::Selectable("(none)", path.empty()) && !path.empty())
    {
      path.clear();
      changed = true;
    }

    for (const assets::asset_entry_t& mesh : meshes)
    {
      if (mesh.path.empty())
        continue;

      const bool selected = path == mesh.path;
      if (ImGui::Selectable(mesh.name.c_str(), selected) && !selected)
      {
        path = mesh.path;
        changed = true;
      }
      if (selected)
        ImGui::SetItemDefaultFocus();
    }
    ImGui::EndCombo();
  }

  if (!mesh_is_in_build)
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "mesh not in this build");

  return changed;
}

// `offers_mesh_asset` is the brush-era mesh_path on the surface; a mesh names
// its asset on its source instead.
bool draw_surface_inspector(shared::geometry_surface_t &surface, bool offers_mesh_asset)
{
  bool changed = false;

  if (!ImGui::CollapsingHeader("Surface", ImGuiTreeNodeFlags_DefaultOpen))
    return false;

  ImGui::PushID("surface");

  if (offers_mesh_asset)
    changed |= draw_mesh_asset_combo(surface.mesh_path);

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
  changed |= edit_rotation_as_euler("rotation", static_mesh.orientation);
  changed |= ImGui::DragFloat3("scale", &static_mesh.scale.x, 0.01f);
  changed |= ImGui::Checkbox("collides as its box", &static_mesh.collides);
  if (!static_mesh.collides)
    ImGui::TextDisabled("no collision of its own: block it out with clip brushes");
  changed |= ImGui::Checkbox("casts shadows", &static_mesh.casts_shadows);
  if (!static_mesh.casts_shadows)
    ImGui::TextDisabled("a light inside it shines through it");
  changed |= draw_surface_inspector(static_mesh.surface, true);
  return changed;
}

bool draw_mesh_inspector(shared::mesh_geometry_t &mesh)
{
  bool changed = false;
  ImGui::TextDisabled("mesh");

  const shared::aabb_bounds_t bounds = shared::compute_mesh_world_bounds(mesh);
  const linalg::vec3          size   = bounds.max - bounds.min;
  ImGui::Text("size  %.1f  %.1f  %.1f", size.x, size.y, size.z);

  changed |= ImGui::DragFloat3("position", &mesh.position.x, 0.5f);
  changed |= edit_rotation_as_euler("rotation", mesh.orientation);

  if (shared::asset_mesh_source_t* asset = std::get_if<shared::asset_mesh_source_t>(&mesh.source))
  {
    ImGui::TextDisabled("source: asset");
    changed |= draw_mesh_asset_combo(asset->path);
    changed |= ImGui::DragFloat3("scale", &asset->scale.x, 0.01f);
    changed |= ImGui::Checkbox("collides as its box", &mesh.collides);
    if (!mesh.collides)
      ImGui::TextDisabled("no collision of its own: block it out with a clip mesh");
  }
  else
  {
    if (const shared::recipe_mesh_source_t* recipe =
            std::get_if<shared::recipe_mesh_source_t>(&mesh.source))
      ImGui::TextDisabled("source: %s recipe", shared::to_string(recipe->recipe.kind));
    else
      ImGui::TextDisabled("source: inline");
    ImGui::Text("%zu vertices, %zu faces", mesh.vertices.size(), mesh.faces.size());
    if (const std::optional<shared::mesh_defect_t> defect = shared::find_mesh_defect(mesh))
      ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "not a solid: %s",
                         shared::format_mesh_defect(*defect).c_str());
    changed |= ImGui::Checkbox("collides", &mesh.collides);
  }

  changed |= ImGui::Checkbox("casts shadows", &mesh.casts_shadows);
  if (!mesh.casts_shadows)
    ImGui::TextDisabled("a light inside it shines through it");
  changed |= draw_surface_inspector(mesh.surface, false);
  return changed;
}

bool draw_brush_inspector(shared::brush_geometry_t &brush, bool forms_solid)
{
  bool changed = false;
  ImGui::TextDisabled("brush");

  const shared::aabb_bounds_t bounds = shared::compute_brush_bounds(brush.hull_points);
  const linalg::vec3          size   = bounds.max - bounds.min;

  ImGui::Text("%zu hull points", brush.hull_points.size());
  ImGui::Text("size  %.1f  %.1f  %.1f", size.x, size.y, size.z);

  if (!forms_solid)
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

  changed |= draw_surface_inspector(brush.surface, true);
  return changed;
}

} // namespace

bool draw_geometry_inspector(shared::geometry_value_t &geometry, shared::entity_uid_t uid,
                             Span<const std::string> materials,
                             const shared::lightmap_t &lightmap,
                             Span<const shared::map_geometry_t> map_geometry, bool collides)
{
  if (!collides)
    ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1),
                       "no collision — players pass through this");

  switch (shared::get_kind(geometry))
  {
  case shared::geometry_kind_t::Static_Mesh:
    return draw_static_mesh_inspector(std::get<shared::static_mesh_geometry_t>(geometry));

  case shared::geometry_kind_t::Mesh:
    return draw_mesh_inspector(std::get<shared::mesh_geometry_t>(geometry));

  case shared::geometry_kind_t::Brush:
    return draw_brush_inspector(
        std::get<shared::brush_geometry_t>(geometry),
        get_cached_brush_hull(geometry, uid, materials, lightmap, map_geometry).has_value());
  }

  log_error("draw_geometry_inspector: unhandled geometry kind {}",
            (int)shared::get_kind(geometry));
  return false;
}

} // namespace client
