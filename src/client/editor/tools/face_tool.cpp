#include "face_tool.hpp"

#include "../../../shared/assets/asset_id.hpp"
#include "../../../shared/collision/collision_detection.hpp"
#include "../../../shared/log.hpp"
#include "../../geometry_renderer.hpp"
#include "../../hud.hpp"
#include "../geometry_editor.hpp"
#include "imgui.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <variant>

namespace client
{

namespace
{

constexpr float OVERLAY_DEPTH_BIAS = -200.0f;
constexpr color_t SELECTED_FACE_FILL = with_alpha(colors::gold, 60);
constexpr color_t SELECTED_FACE_GRID = colors::gold;

void tangent_basis(const linalg::vec3 &normal, linalg::vec3 &tangent_u, linalg::vec3 &tangent_v)
{
  const linalg::vec3 reference =
      std::abs(normal.y) < 0.9f ? linalg::vec3{0.f, 1.f, 0.f} : linalg::vec3{1.f, 0.f, 0.f};
  tangent_u = linalg::normalize(linalg::cross(reference, normal));
  tangent_v = linalg::cross(normal, tangent_u);
}

bool face_grid_shape_changed(const shared::face_surface_t &before,
                             const shared::face_surface_t &after)
{
  if (before.subdivision_level != after.subdivision_level ||
      before.offsets.size() != after.offsets.size())
    return true;
  for (size_t vertex = 0; vertex < before.offsets.size(); ++vertex)
  {
    const linalg::vec3 &a = before.offsets[vertex];
    const linalg::vec3 &b = after.offsets[vertex];
    if (a.x != b.x || a.y != b.y || a.z != b.z)
      return true;
  }
  return false;
}

// The MATERIAL half of a face onto another: the layers, the draw flag, the
// smoothing group, the lightmap density, and the UV scale and shift. Never the
// axes, which are world-space and stretch a perpendicular face to infinity, and
// never the grid, which is the face's own shape. C/V and "copy to every face"
// are this one rule.
void copy_face_material(const shared::face_surface_t &pattern, shared::face_surface_t &face)
{
  for (int layer = 0; layer < BLEND_LAYER_COUNT; ++layer)
    shared::set_face_layer_material(face, layer, shared::face_layer_material(pattern, layer));
  face.emits_geometry = pattern.emits_geometry;
  face.smoothing_group = pattern.smoothing_group;
  face.lightmap_scale = pattern.lightmap_scale;
  face.uv.u_scale = pattern.uv.u_scale;
  face.uv.v_scale = pattern.uv.v_scale;
  face.uv.u_shift = pattern.uv.u_shift;
  face.uv.v_shift = pattern.uv.v_shift;
}

bool face_is_quad(const shared::mesh_face_t &face) { return face.vertex_indices.size() == 4; }

linalg::vec3 mesh_world_vertex(const shared::mesh_geometry_t &mesh, uint32_t vertex)
{
  return mesh.position + linalg::rotate(mesh.orientation, mesh.vertices[vertex]);
}

const char *material_label(const shared::map_t &map, uint16_t index)
{
  if (index >= map.materials.size())
    return "<out of range>";
  return map.materials[index].empty() ? "<untextured>" : map.materials[index].c_str();
}

} // namespace

void Face_Tool::on_enable(editor_context_t &ctx)
{
  assert(ctx.map);

  mode = Mode::Surface;
  hover = {};
  stroke = {};
  grid_cursor = {};
  select_face({});

  // Start from what the tool before handed over, when that was one mesh.
  if (ctx.selection_handed_over.size() == 1 && try_get_mesh(ctx, ctx.selection_handed_over[0]))
    select_face({ctx.selection_handed_over[0], std::nullopt});
}

void Face_Tool::on_disable(editor_context_t &ctx)
{
  // A stroke in progress is an edit already visible in the map, so it is
  // committed rather than left without an undo entry.
  end_grid_stroke(ctx);
}

Span<const shared::entity_uid_t> Face_Tool::selected_objects() const
{
  return Span<const shared::entity_uid_t>(selected_uids.data(), selected_uids.size());
}

shared::mesh_geometry_t *Face_Tool::try_get_mesh(editor_context_t &ctx, shared::entity_uid_t uid)
{
  if (uid == shared::null_entity_uid || !ctx.map)
    return nullptr;
  shared::map_geometry_t *entry = ctx.map->find_geometry_by_uid(uid);
  return entry ? std::get_if<shared::mesh_geometry_t>(&entry->value) : nullptr;
}

const shared::face_surface_t *Face_Tool::try_get_face_surface(editor_context_t &ctx,
                                                              const face_ref_t &face)
{
  const shared::mesh_geometry_t *mesh = try_get_mesh(ctx, face.uid);
  if (!mesh || !face.face_index || *face.face_index >= mesh->faces.size())
    return nullptr;
  return &mesh->faces[*face.face_index].surface;
}

void Face_Tool::select_face(const face_ref_t &face)
{
  selection = face;
  selected_uids.clear();
  if (selection.uid != shared::null_entity_uid)
    selected_uids.push_back(selection.uid);
}

Face_Tool::face_ref_t Face_Tool::face_under_cursor_or_selected() const
{
  if (hover.uid != shared::null_entity_uid && hover.face_index)
    return hover;
  return selection;
}

void Face_Tool::on_update(editor_context_t &ctx, const viewport_state_t &view, float dt)
{
  cached_view = view;

  // A live stroke owns the cursor, so hover stops tracking under it.
  if (!stroke.active)
  {
    hover = {};
    if (ctx.bvh)
    {
      ray_hit_result_t hit{};
      if (bvh_intersect_ray(*ctx.bvh, view.mouse_ray.origin, view.mouse_ray.direction, hit) &&
          hit.id.type == Collision_Id::Type::Static_Geometry)
      {
        const shared::entity_uid_t uid = hit.id.index;
        if (const shared::mesh_geometry_t *mesh = try_get_mesh(ctx, uid))
        {
          hover.uid = uid;
          if (const std::optional<shared::mesh_face_hit_t> face_hit = shared::try_pick_mesh_face(
                  *mesh, view.mouse_ray.origin, view.mouse_ray.direction))
            hover.face_index = face_hit->face_index;
        }
      }
    }
  }

  update_grid_stroke(ctx, dt);
}

void Face_Tool::on_mouse_down(editor_context_t &ctx, const input::mouse_event_t &e)
{
  if (e.button != input::mouse_button_t::Left)
    return;

  if (mode != Mode::Surface)
  {
    // A press on a DIFFERENT mesh picks it and does not stroke: the cursor is
    // resolved against the selected mesh, so working on one not yet selected
    // would write at a point measured on something else.
    if (hover.uid != shared::null_entity_uid && hover.uid != selection.uid)
    {
      select_face(hover);
      return;
    }

    if (!try_get_mesh(ctx, selection.uid) || !grid_cursor.position)
      return;

    stroke.geometry_at_the_start.clear();
    remember_brush_before_stroke(stroke.geometry_at_the_start, *ctx.map, selection.uid);
    stroke.inverted = e.mods.shift;
    stroke.active = true;
    return;
  }

  // Empty space clears; a mesh with no face under the cursor still selects.
  select_face(hover);
}

void Face_Tool::on_mouse_drag(editor_context_t &, const input::mouse_event_t &) {}

void Face_Tool::on_mouse_up(editor_context_t &ctx, const input::mouse_event_t &e)
{
  if (e.button != input::mouse_button_t::Left)
    return;
  if (stroke.active)
    end_grid_stroke(ctx);
}

void Face_Tool::on_key_down(editor_context_t &ctx, const key_event_t &e)
{
  switch (e.key)
  {
  case input::key_t::Tab:
    end_grid_stroke(ctx);
    mode = (mode == Mode::Surface) ? Mode::Paint : (mode == Mode::Paint) ? Mode::Sculpt : Mode::Surface;
    return;

  case input::key_t::Escape:
    end_grid_stroke(ctx);
    if (selection.face_index)
      select_face({selection.uid, std::nullopt});
    else
      select_face({});
    return;

  // Sample the face under the cursor.
  case input::key_t::C:
  {
    if (const shared::face_surface_t *sampled =
            try_get_face_surface(ctx, face_under_cursor_or_selected()))
      face_clipboard = *sampled;
    return;
  }

  // Apply its material half to the face under the cursor; the face keeps its
  // own projection axes and its own grid.
  case input::key_t::V:
  {
    if (!face_clipboard)
      return;
    edit_face_surface(ctx, face_under_cursor_or_selected(), "Paste face material",
                      [this](shared::face_surface_t &face) {
                        copy_face_material(*face_clipboard, face);
                      });
    return;
  }

  default:
    return;
  }
}

void Face_Tool::edit_face_surface(editor_context_t &ctx, const face_ref_t &target,
                                  const char *name,
                                  const std::function<void(shared::face_surface_t &)> &edit)
{
  shared::mesh_geometry_t *mesh = try_get_mesh(ctx, target.uid);
  if (!mesh || !target.face_index || *target.face_index >= mesh->faces.size())
    return;

  const shared::geometry_value_t before = *mesh;
  shared::face_surface_t &surface = mesh->faces[*target.face_index].surface;
  const shared::face_surface_t surface_before = surface;
  edit(surface);

  // A no-op edit pushes no transaction, so rebuilding for one is a wasted GPU
  // stall: update_mesh waits on the device.
  if (shared::geometry_values_equal(before, *mesh))
    return;

  refresh_generated_geometry_mesh(*mesh, target.uid, ctx.map->materials, ctx.map->lightmap,
                                  ctx.map->geometry);

  // The grid is the surface the player stands on; a material is not.
  if (face_grid_shape_changed(surface_before, surface) &&
      ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;

  transaction_t transaction;
  transaction.add_geometry_modified(target.uid, before, *mesh);
  ctx.transaction_system.push(name, std::move(transaction));
}

void Face_Tool::edit_mesh(editor_context_t &ctx, shared::entity_uid_t uid, const char *name,
                          const std::function<void(shared::mesh_geometry_t &)> &edit)
{
  shared::mesh_geometry_t *mesh = try_get_mesh(ctx, uid);
  if (!mesh)
    return;

  const shared::geometry_value_t before = *mesh;
  edit(*mesh);
  if (shared::geometry_values_equal(before, *mesh))
    return;

  refresh_generated_geometry_mesh(*mesh, uid, ctx.map->materials, ctx.map->lightmap,
                                  ctx.map->geometry);
  if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;

  transaction_t transaction;
  transaction.add_geometry_modified(uid, before, *mesh);
  ctx.transaction_system.push(name, std::move(transaction));
}

// ============================================================================
// Grid strokes: Paint writes WEIGHTS, Sculpt writes OFFSETS. Neither moves a
// mesh vertex. A sculpt covers every mesh under the sphere, so a hill crosses
// the seam between two meshes; a paint stays on the selected one.
// ============================================================================

void Face_Tool::update_grid_stroke(editor_context_t &ctx, float dt)
{
  grid_cursor.position.reset();

  if (mode == Mode::Surface || !ctx.map)
    return;

  shared::mesh_geometry_t *mesh = try_get_mesh(ctx, selection.uid);
  if (!mesh)
    return;

  if (const std::optional<shared::mesh_face_hit_t> hit = shared::try_pick_mesh_face(
          *mesh, cached_view.mouse_ray.origin, cached_view.mouse_ray.direction))
  {
    grid_cursor.position = hit->position;
    grid_cursor.surface_normal = hit->surface_normal;
    grid_cursor.face_normal = shared::compute_mesh_world_face_plane(*mesh, hit->face_index).normal;
  }

  if (!stroke.active || !grid_cursor.position)
    return;

  // Off dt rather than per mouse event: a stroke is a rate, so holding still
  // keeps building up and a sweep lays down the same amount per unit of travel
  // whatever the frame rate.
  if (mode == Mode::Paint)
  {
    const size_t touched = shared::paint_mesh_face_grid_blend(
        *mesh, *grid_cursor.position, paint.radius, paint.strength * dt, paint.layer);
    if (touched == 0)
      return;
    refresh_generated_geometry_mesh(*mesh, selection.uid, ctx.map->materials, ctx.map->lightmap,
                                    ctx.map->geometry);
    return;
  }

  const float amount = (stroke.inverted ? -1.0f : 1.0f) * sculpt.strength * dt;
  sculpt_meshes_under_stroke(ctx, stroke.geometry_at_the_start, *grid_cursor.position,
                             sculpt.radius, grid_cursor.face_normal * amount);
}

void Face_Tool::end_grid_stroke(editor_context_t &ctx)
{
  std::vector<stroke_brush_before_t> befores = std::move(stroke.geometry_at_the_start);
  stroke.geometry_at_the_start.clear();
  stroke.active = false;

  if (befores.empty() || !ctx.map)
    return;

  // The whole stroke is one entry, like a drag.
  if (!push_stroke_transaction(ctx, mode == Mode::Sculpt ? "Sculpt face grid" : "Paint face grid",
                               std::move(befores)))
    return;

  if (mode == Mode::Sculpt && ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
}

// ============================================================================
// Panel
// ============================================================================

void Face_Tool::draw_surface_ui(editor_context_t &ctx)
{
  ImGui::SeparatorText("Face surface");

  const shared::mesh_geometry_t *mesh = try_get_mesh(ctx, selection.uid);
  if (!mesh)
  {
    ImGui::TextDisabled("click a mesh face - this panel follows the SELECTION, not the cursor");
    return;
  }

  if (std::holds_alternative<shared::asset_mesh_source_t>(mesh->source))
  {
    ImGui::TextDisabled("mesh %u is asset-sourced: its faces are the asset's, not editable here",
                        selection.uid);
    return;
  }

  const shared::face_surface_t *current = try_get_face_surface(ctx, selection);
  if (!current)
  {
    ImGui::TextDisabled("mesh %u: click one of its %zu faces", selection.uid, mesh->faces.size());
    return;
  }

  const uint32_t face_index = *selection.face_index;
  const shared::mesh_face_t &face = mesh->faces[face_index];
  const Plane world_plane = shared::compute_mesh_world_face_plane(*mesh, face_index);
  ImGui::Text("mesh %u  face %u of %zu  (%zu corners, normal %.2f %.2f %.2f)", selection.uid,
              face_index, mesh->faces.size(), face.vertex_indices.size(),
              world_plane.normal.x, world_plane.normal.y, world_plane.normal.z);

  // One combo per LAYER: a third layer is another row of the same loop. The
  // table is browsed, never typed into by index: a face holds an index and an
  // author holds a path, and this is where the two meet.
  for (int layer = 0; layer < BLEND_LAYER_COUNT; ++layer)
  {
    const uint16_t material_index = shared::face_layer_material(*current, layer);

    char label[64];
    if (layer == 0)
      std::snprintf(label, sizeof(label), "material");
    else
      std::snprintf(label, sizeof(label), "blend material %d", layer);

    if (!ImGui::BeginCombo(label, material_label(*ctx.map, material_index)))
      continue;

    for (uint16_t index = 0; index < (uint16_t)ctx.map->materials.size(); ++index)
    {
      const bool selected_here = index == material_index;
      if (ImGui::Selectable(material_label(*ctx.map, index), selected_here))
        edit_face_surface(ctx, selection, "Set face material",
                          [index, layer](shared::face_surface_t &surface) {
                            shared::set_face_layer_material(surface, layer, index);
                          });
      if (selected_here)
        ImGui::SetItemDefaultFocus();
    }

    // What the BUILD has, under what this map has already used: the manifest
    // rather than a directory listing, which a pkg or embed build has none of.
    bool separator_drawn = false;
    for (const assets::asset_entry_t &available :
         assets::asset_class_entries(assets::asset_class_t::material_asset))
    {
      if (available.path.empty() || ctx.map->has_material(available.path.c_str()))
        continue;

      if (!separator_drawn)
      {
        ImGui::Separator();
        separator_drawn = true;
      }

      if (ImGui::Selectable(available.name.c_str()))
      {
        const uint16_t added = ctx.map->material_index_for(available.path.c_str());
        edit_face_surface(ctx, selection, "Set face material",
                          [added, layer](shared::face_surface_t &surface) {
                            shared::set_face_layer_material(surface, layer, added);
                          });
      }
    }
    ImGui::EndCombo();
  }

  ImGui::SetNextItemWidth(260.0f);
  const bool submitted = ImGui::InputTextWithHint(
      "##material_path", "resources/textures/harsh_bricks", material_path_input,
      sizeof(material_path_input), ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine();

  // In Paint mode the layer being painted is the one an author is browsing a
  // material FOR, so that is where a new entry lands.
  const int assign_layer = (mode == Mode::Paint) ? paint.layer : 0;
  char assign_label[64];
  std::snprintf(assign_label, sizeof(assign_label), "add + assign to layer %d", assign_layer);

  if ((ImGui::Button(assign_label) || submitted) && material_path_input[0] != '\0')
  {
    const uint16_t added = ctx.map->material_index_for(material_path_input);
    edit_face_surface(ctx, selection, "Set face material",
                      [added, assign_layer](shared::face_surface_t &surface) {
                        shared::set_face_layer_material(surface, assign_layer, added);
                      });
    material_path_input[0] = '\0';
  }

  bool emits_geometry = current->emits_geometry;
  if (ImGui::Checkbox("draws (off is nodraw)", &emits_geometry))
    edit_face_surface(ctx, selection, "Set face draws",
                      [emits_geometry](shared::face_surface_t &surface) {
                        surface.emits_geometry = emits_geometry;
                      });

  // The face property of geometry_def.md 11.3: the normal at a corner is the
  // mean over the faces of this mesh that share the vertex AND the group.
  int smoothing_group = current->smoothing_group;
  if (ImGui::InputInt("smoothing group (0 is flat)", &smoothing_group))
    edit_face_surface(ctx, selection, "Set face smoothing group",
                      [smoothing_group](shared::face_surface_t &surface) {
                        surface.smoothing_group = std::max(smoothing_group, 0);
                      });

  float lightmap_scale = current->lightmap_scale;
  if (ImGui::DragFloat("lightmap scale", &lightmap_scale, 0.05f, 0.05f, 16.0f, "%.2f"))
    edit_face_surface(ctx, selection, "Set face lightmap scale",
                      [lightmap_scale](shared::face_surface_t &surface) {
                        surface.lightmap_scale = lightmap_scale;
                      });

  float scale[2] = {current->uv.u_scale, current->uv.v_scale};
  if (ImGui::DragFloat2("scale (units per repeat)", scale, 1.0f, 1.0f, 4096.0f, "%.1f"))
    edit_face_surface(ctx, selection, "Set face UV scale", [scale](shared::face_surface_t &surface) {
      surface.uv.u_scale = scale[0];
      surface.uv.v_scale = scale[1];
    });

  float shift[2] = {current->uv.u_shift, current->uv.v_shift};
  if (ImGui::DragFloat2("shift", shift, 1.0f, 0.0f, 0.0f, "%.1f"))
    edit_face_surface(ctx, selection, "Set face UV shift", [shift](shared::face_surface_t &surface) {
      surface.uv.u_shift = shift[0];
      surface.uv.v_shift = shift[1];
    });

  if (ImGui::Button("align to face"))
    edit_face_surface(ctx, selection, "Align face UV",
                      [&world_plane](shared::face_surface_t &surface) {
                        surface.uv = shared::default_face_uv(world_plane.normal);
                      });

  ImGui::SameLine();
  if (ImGui::Button("copy material to every face"))
  {
    const shared::face_surface_t pattern = *current;
    edit_mesh(ctx, selection.uid, "Copy material to every face",
              [&pattern](shared::mesh_geometry_t &edited) {
                for (shared::mesh_face_t &other : edited.faces)
                  copy_face_material(pattern, other.surface);
              });
  }

  ImGui::Separator();

  // TESSELLATION. 0 is flat; anything above it makes the face a grid that
  // Sculpt mode brushes and Paint mode blends on. Only a quad carries one.
  if (!face_is_quad(face))
  {
    ImGui::TextDisabled("this face is not a quad - a grid is a patch over four corners");
  }
  else
  {
    int subdivision_level = current->subdivision_level;
    if (ImGui::SliderInt("subdivision", &subdivision_level, 0, 32))
      edit_face_surface(ctx, selection, "Set face subdivision",
                        [subdivision_level](shared::face_surface_t &surface) {
                          // Resamples rather than flattening, so dragging the
                          // slider keeps what has been sculpted.
                          shared::resize_face_grid(surface, subdivision_level);
                        });
    ImGui::TextDisabled("0 removes the grid - the slider IS how a face stops being subdivided");
  }

  if (shared::face_is_subdivided(*current))
  {
    const int size = shared::face_grid_size(current->subdivision_level);
    ImGui::TextDisabled("%d x %d grid, %zu vertices - Sculpt mode brushes them, Paint mode blends",
                        size, size, current->offsets.size());

    if (ImGui::Button("reset this face's sculpt to flat"))
      edit_face_surface(ctx, selection, "Reset face sculpt", [](shared::face_surface_t &surface) {
        for (linalg::vec3 &offset : surface.offsets)
          offset = {0, 0, 0};
      });

    // Adjacent grids at DIFFERENT levels leave a T-junction, and matching them
    // by hand is a trip through the slider per face. The LEVEL travels and
    // nothing else; a face that is not a quad is skipped, not a refusal.
    ImGui::SameLine();
    if (ImGui::Button("give every quad face this subdivision"))
    {
      const int level = current->subdivision_level;
      edit_mesh(ctx, selection.uid, "Give every face this subdivision",
                [level](shared::mesh_geometry_t &edited) {
                  for (shared::mesh_face_t &other : edited.faces)
                    if (face_is_quad(other))
                      shared::resize_face_grid(other.surface, level);
                });
    }
  }

  if (shared::face_is_blended(*current))
    ImGui::TextDisabled("blended - Paint mode brushes the weights");

  if (face_clipboard)
    ImGui::TextDisabled("clipboard: material %u, scale %.0f x %.0f", face_clipboard->material,
                        face_clipboard->uv.u_scale, face_clipboard->uv.v_scale);
  else
    ImGui::TextDisabled("clipboard: empty");
}

void Face_Tool::draw_paint_ui(editor_context_t &ctx)
{
  if (mode != Mode::Paint)
    return;

  ImGui::SeparatorText("Vertex paint");

  if (!try_get_mesh(ctx, selection.uid))
  {
    ImGui::TextDisabled("click a mesh to paint on it");
    return;
  }

  ImGui::DragFloat("radius", &paint.radius, 1.0f, 1.0f, 2048.0f, "%.0f");
  ImGui::DragFloat("strength", &paint.strength, 0.05f, 0.05f, 8.0f, "%.2f / sec");

  // The TARGET layer, not a sign: painting toward the base is the eraser.
  for (int layer = 0; layer < BLEND_LAYER_COUNT; ++layer)
  {
    char label[64];
    if (layer == 0)
      std::snprintf(label, sizeof(label), "base material");
    else
      std::snprintf(label, sizeof(label), "blend material %d", layer);

    if (ImGui::RadioButton(label, paint.layer == layer))
      paint.layer = layer;
  }

  const shared::face_surface_t *current = try_get_face_surface(ctx, selection);
  if (!current || !shared::face_is_subdivided(*current))
    ImGui::TextColored(ImVec4(1, 0.8f, 0.4f, 1),
                       "the selected face has no grid - subdivide it above to paint on it");
  else if (!shared::face_is_blended(*current))
    ImGui::TextColored(ImVec4(1, 0.8f, 0.4f, 1),
                       "both layers name one material - nothing will look different");

  if (!grid_cursor.position)
    ImGui::TextDisabled("cursor is off the mesh");
}

void Face_Tool::draw_sculpt_ui(editor_context_t &ctx)
{
  if (mode != Mode::Sculpt)
    return;

  ImGui::SeparatorText("Vertex sculpt");

  if (!try_get_mesh(ctx, selection.uid))
  {
    ImGui::TextDisabled("click a mesh to sculpt it");
    return;
  }

  ImGui::DragFloat("radius", &sculpt.radius, 1.0f, 1.0f, 2048.0f, "%.0f");
  ImGui::DragFloat("strength", &sculpt.strength, 1.0f, 1.0f, 4096.0f, "%.0f units / sec");
  ImGui::TextDisabled("drag pushes out along the face normal, shift+drag pulls in");

  const shared::face_surface_t *current = try_get_face_surface(ctx, selection);
  if (!current || !shared::face_is_subdivided(*current))
    ImGui::TextColored(ImVec4(1, 0.8f, 0.4f, 1),
                       "the selected face has no grid - subdivide it above to sculpt it");

  if (!grid_cursor.position)
    ImGui::TextDisabled("cursor is off the mesh");
}

void Face_Tool::on_draw_ui(editor_context_t &ctx)
{
  ImGui::Begin("Face Tool");

  constexpr Mode MODE_BUTTONS[] = {Mode::Surface, Mode::Paint, Mode::Sculpt};
  constexpr const char *MODE_LABELS[] = {"Surface", "Paint", "Sculpt"};

  int mode_index = 0;
  for (int index = 0; index < (int)std::size(MODE_BUTTONS); ++index)
    if (MODE_BUTTONS[index] == mode)
      mode_index = index;

  bool chose_a_mode = false;
  for (int index = 0; index < (int)std::size(MODE_BUTTONS); ++index)
    chose_a_mode = ImGui::RadioButton(MODE_LABELS[index], &mode_index, index) || chose_a_mode;

  if (chose_a_mode && MODE_BUTTONS[mode_index] != mode)
  {
    end_grid_stroke(ctx);
    mode = MODE_BUTTONS[mode_index];
  }

  ImGui::Separator();

  ImGui::PushID("surface");
  draw_surface_ui(ctx);
  ImGui::PopID();

  ImGui::PushID("paint");
  draw_paint_ui(ctx);
  ImGui::PopID();

  ImGui::PushID("sculpt");
  draw_sculpt_ui(ctx);
  ImGui::PopID();

  ImGui::Separator();
  ImGui::TextDisabled("Tab         surface / paint / sculpt mode");
  ImGui::TextDisabled("click       select the face under the cursor");
  ImGui::TextDisabled("C / V       sample / apply the face material under the cursor");
  ImGui::TextDisabled("drag        paint the target layer (Paint mode)");
  ImGui::TextDisabled("drag        sculpt the face grid out (Sculpt mode)");
  ImGui::TextDisabled("shift+drag  sculpt it in (Sculpt mode)");
  ImGui::TextDisabled("esc         drop the face, then the mesh");

  ImGui::End();
}

// ============================================================================
// Overlay
// ============================================================================

void Face_Tool::on_draw_overlay(editor_context_t &ctx, pass_builder_t &draws)
{
  // The stroke cursor: a ring lying on the DRAWN surface, where the stroke
  // measures its radius from.
  if (mode != Mode::Surface && grid_cursor.position)
  {
    const bool sculpting = (mode == Mode::Sculpt);
    const float radius = sculpting ? sculpt.radius : paint.radius;

    linalg::vec3 tangent_u{0, 0, 0};
    linalg::vec3 tangent_v{0, 0, 0};
    tangent_basis(grid_cursor.surface_normal, tangent_u, tangent_v);

    constexpr int SEGMENTS = 32;
    linalg::vec3 previous{0, 0, 0};
    for (int step = 0; step <= SEGMENTS; ++step)
    {
      const float angle = (float)step * (6.2831853f / (float)SEGMENTS);
      const linalg::vec3 point = *grid_cursor.position + tangent_u * (std::cos(angle) * radius) +
                                 tangent_v * (std::sin(angle) * radius);
      if (step > 0)
        draws.debug.line(previous, point, colors::cyan, -2.0f);
      previous = point;
    }

    // Which WAY the next stroke pushes, drawn rather than remembered.
    if (sculpting)
    {
      const bool inverted = stroke.active ? stroke.inverted : input::current_modifiers().shift;
      const float sign = inverted ? -1.0f : 1.0f;
      draws.debug.line(*grid_cursor.position,
                       *grid_cursor.position + grid_cursor.face_normal * (radius * sign),
                       inverted ? colors::red : colors::green, -2.0f);
    }
  }

  if (hover.uid != shared::null_entity_uid && hover.uid != selection.uid)
    if (const shared::mesh_geometry_t *hovered = try_get_mesh(ctx, hover.uid))
      draw_mesh_wireframe(draws, *hovered, colors::white, OVERLAY_DEPTH_BIAS);

  const shared::mesh_geometry_t *mesh = try_get_mesh(ctx, selection.uid);
  if (!mesh)
    return;

  draw_mesh_wireframe(draws, *mesh, colors::yellow, OVERLAY_DEPTH_BIAS);

  if (!selection.face_index || *selection.face_index >= mesh->faces.size())
    return;

  const uint32_t face_index = *selection.face_index;
  const shared::mesh_face_t &face = mesh->faces[face_index];

  // A subdivided face's surface IS its grid, so filling the base polygon would
  // shade the one place the face is not.
  if (face_is_quad(face) && shared::face_is_subdivided(face.surface))
  {
    const std::vector<linalg::vec3> grid =
        shared::compute_mesh_face_grid_world_vertices(*mesh, face_index);
    const int size = shared::face_grid_size(face.surface.subdivision_level);

    for (int v = 0; v + 1 < size; ++v)
    {
      for (int u = 0; u + 1 < size; ++u)
      {
        const linalg::vec3f cell[4] = {grid[(size_t)(v * size + u)], grid[(size_t)(v * size + u + 1)],
                                       grid[(size_t)((v + 1) * size + u + 1)],
                                       grid[(size_t)((v + 1) * size + u)]};
        draws.debug.filled_polygon(Span<const linalg::vec3f>(cell, 4), SELECTED_FACE_FILL, 0.0f,
                                   {.depth_bias = OVERLAY_DEPTH_BIAS});
      }
    }

    for (int v = 0; v < size; ++v)
    {
      for (int u = 0; u < size; ++u)
      {
        const linalg::vec3 &point = grid[(size_t)(v * size + u)];
        if (u + 1 < size)
          draws.debug.line(point, grid[(size_t)(v * size + u + 1)], SELECTED_FACE_GRID,
                           OVERLAY_DEPTH_BIAS);
        if (v + 1 < size)
          draws.debug.line(point, grid[(size_t)((v + 1) * size + u)], SELECTED_FACE_GRID,
                           OVERLAY_DEPTH_BIAS);
      }
    }
  }
  else
  {
    std::vector<linalg::vec3f> polygon;
    polygon.reserve(face.vertex_indices.size());
    for (uint32_t vertex : face.vertex_indices)
      polygon.push_back(mesh_world_vertex(*mesh, vertex));
    draws.debug.filled_polygon(polygon, SELECTED_FACE_FILL, 0.0f, {.depth_bias = OVERLAY_DEPTH_BIAS});
  }

  linalg::vec3 centroid{0, 0, 0};
  for (uint32_t vertex : face.vertex_indices)
    centroid = centroid + mesh_world_vertex(*mesh, vertex);
  centroid = centroid * (1.0f / (float)face.vertex_indices.size());
  const Plane world_plane = shared::compute_mesh_world_face_plane(*mesh, face_index);
  draws.debug.arrow(centroid, centroid + world_plane.normal * 32.0f, colors::yellow);
}

} // namespace client
