#include "geometry_editor.hpp"
#include "../../shared/assets/asset.hpp"
#include "../../shared/map/map.hpp"
#include "../../shared/map/map_geometry_asset.hpp"
#include "../../shared/map/map_geometry_mesh.hpp"
#include "../../shared/map/editor_grid.hpp"
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
constexpr color_t CONTOUR_COLOR{30, 32, 38};

// A mesh get_collision_pieces refused. Loud on purpose -- it is a hole in the
// level, and the contour is the only thing about the mesh that would otherwise
// have changed at all.
constexpr color_t NO_COLLISION_CONTOUR_COLOR{230, 40, 40};

// The box a selected asset-sourced mesh collides as, and the same box when its
// collision is switched off and a clip mesh is expected to stand in for it.
constexpr color_t ASSET_MESH_COLLISION_BOX_COLOR{60, 220, 120};
constexpr color_t ASSET_MESH_NO_COLLISION_BOX_COLOR{120, 120, 120};

} // namespace

void draw_mesh_wireframe(pass_builder_t &draws, const shared::mesh_geometry_t &mesh,
                         color_t color, float depth_bias, const linalg::vec3 &translation)
{
  draw_mesh_edges(draws, mesh, color, depth_bias,
                  linalg::compose_transform(translation, linalg::quatf::identity(), {1.0f, 1.0f, 1.0f}));
}

// ============================================================================
// Placement
// ============================================================================

linalg::vec3 compute_geometry_placement_center(const shared::mesh_geometry_t &geometry,
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

void draw_geometry_ghost(const shared::mesh_geometry_t &geometry,
                         pass_builder_t &draws, const linalg::vec3 &center)
{
  // An asset-sourced mesh previews as its own wireframe mesh when the asset
  // resolves — placing a prop by its bounding box tells you nothing about its
  // orientation.
  const shared::mesh_geometry_t* asset_mesh = shared::find_asset_sourced_mesh(geometry);
  if (asset_mesh != nullptr)
  {
    const shared::asset_mesh_source_t &asset = shared::get_mesh_asset_source(*asset_mesh);
    if (push_wireframe_mesh(draws, shared::resolve_mesh_asset(asset), center,
                            asset_mesh->orientation, asset.scale, colors::yellow))
      return;
    draws.debug.box(center, shared::get_half_extents(geometry), colors::yellow);
    return;
  }

  // A mesh traces its stored edges, carried from its pose to `center`.
  draw_mesh_wireframe(draws, geometry, colors::yellow, 0.0f, center - geometry.position);
}

// ============================================================================
// Viewport drawing
// ============================================================================

void draw_geometry_in_editor(const shared::mesh_geometry_t &mesh, pass_builder_t &draws,
                             shared::entity_uid_t uid, bool solid,
                             Span<const std::string> materials,
                             const shared::lightmap_t &lightmap, bool collides,
                             const linalg::vec3 &translation, generated_mesh_check_t check)
{
  const bool          moved = !(translation == linalg::vec3{0, 0, 0});
  const linalg::mat4f moved_by =
      linalg::compose_transform(translation, linalg::quatf::identity(), {1, 1, 1});

  // An asset-sourced mesh has no stored edges of its own and draws as its asset.
  if (shared::find_asset_sourced_mesh(mesh) != nullptr)
  {
    draw_geometry(draws, mesh, uid, materials, lightmap, moved ? &moved_by : nullptr);
    return;
  }

  // A mesh's contour is its stored edges: no hull, nothing cached.
  if (solid)
    draw_geometry(draws, mesh, uid, materials, lightmap, moved ? &moved_by : nullptr,
                  geometry_wipe_frame(mesh, linalg::quatf{0.0f, 0.0f, 0.0f, 1.0f}), {},
                  renderer::light_cut_t::none, false, check);
  draw_mesh_wireframe(draws, mesh, collides ? CONTOUR_COLOR : NO_COLLISION_CONTOUR_COLOR,
                      -60.0f, translation);
}

// ============================================================================
// Selection highlight
// ============================================================================

void draw_geometry_selection_highlight(const shared::mesh_geometry_t &mesh,
                                       pass_builder_t &draws, float time, bool is_outlined,
                                       const linalg::vec3 &translation)
{
  if (is_outlined)
    return;

  const color_t color = compute_selection_pulse_color(time);

  // Strong bias so the highlight renders in FRONT of the solid surface it traces.
  constexpr float highlight_bias = -200.0f;

  if (shared::find_asset_sourced_mesh(mesh) == nullptr)
  {
    draw_mesh_wireframe(draws, mesh, color, highlight_bias, translation);
    return;
  }

  const shared::asset_mesh_source_t &asset = shared::get_mesh_asset_source(mesh);
  push_wireframe_mesh(draws, shared::resolve_mesh_asset(asset), mesh.position + translation,
                      mesh.orientation, asset.scale, color);

  const color_t box_color =
      mesh.collides ? ASSET_MESH_COLLISION_BOX_COLOR : ASSET_MESH_NO_COLLISION_BOX_COLOR;
  for (const std::vector<linalg::vec3>& polygon :
       shared::compute_asset_mesh_collision_box(mesh).face_polygons)
  {
    for (size_t corner = 0; corner < polygon.size(); ++corner)
      draws.debug.line(polygon[corner] + translation,
                       polygon[(corner + 1) % polygon.size()] + translation, box_color,
                       highlight_bias);
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

// A drag or slider commits on the frame it is let go after a change; its
// per-frame "changed" is a live value the drawn mesh follows by comparison.
bool item_committed(bool)
{
  return ImGui::IsItemDeactivatedAfterEdit();
}

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

bool draw_surface_inspector(shared::geometry_surface_t &surface)
{
  bool changed = false;

  if (!ImGui::CollapsingHeader("Surface", ImGuiTreeNodeFlags_DefaultOpen))
    return false;

  ImGui::PushID("surface");

  changed |= item_committed(ImGui::ColorEdit3("color", &surface.color.x));
  changed |= item_committed(ImGui::DragFloat("roughness", &surface.roughness, 0.01f, 0.f, 1.f));
  changed |= ImGui::Checkbox("visible", &surface.visible);
  ImGui::SameLine();
  changed |= ImGui::Checkbox("wireframe", &surface.is_wireframe);

  ImGui::PopID();
  return changed;
}

bool draw_mesh_inspector(shared::mesh_geometry_t &mesh)
{
  bool changed = false;
  ImGui::TextDisabled("mesh");

  const shared::aabb_bounds_t bounds = shared::compute_mesh_world_bounds(mesh);
  const linalg::vec3          size   = bounds.max - bounds.min;
  ImGui::Text("size  %.1f  %.1f  %.1f", size.x, size.y, size.z);

  changed |= item_committed(ImGui::DragFloat3("position", &mesh.position.x, 0.5f));
  changed |= item_committed(edit_rotation_as_euler("rotation", mesh.orientation));

  if (shared::asset_mesh_source_t* asset = std::get_if<shared::asset_mesh_source_t>(&mesh.source))
  {
    ImGui::TextDisabled("source: asset");
    changed |= draw_mesh_asset_combo(asset->path);
    changed |= item_committed(ImGui::DragFloat3("scale", &asset->scale.x, 0.01f));
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
  changed |= draw_surface_inspector(mesh.surface);
  return changed;
}

} // namespace

bool draw_geometry_inspector(shared::mesh_geometry_t &mesh, bool collides)
{
  if (!collides)
    ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1),
                       "no collision — players pass through this");

  return draw_mesh_inspector(mesh);
}

} // namespace client
