#include "brush_tool.hpp"

#include "../../../shared/collision_detection.hpp"
#include "../../../shared/log.hpp"
#include "../../hud/announcement.hpp"
#include "../../geometry_renderer.hpp"
#include "imgui.h"
#include "renderer.hpp"

#include <algorithm>
#include <cmath>

namespace client
{

namespace
{

constexpr float HANDLE_PICK_RADIUS = 14.0f;  // pixels
constexpr float HANDLE_RADIUS = 5.0f;        // pixels
constexpr float CORNER_HANDLE_RADIUS = 7.0f; // pixels

// How far the cursor must travel before a press on empty face area becomes a
// box selection rather than a click.
constexpr float BAND_DRAG_THRESHOLD = 4.0f; // pixels

constexpr float OVERLAY_DEPTH_BIAS = -200.0f;

constexpr color_t SELECTED_FACE_FILL = with_alpha(colors::gold, 60);
constexpr color_t SELECTED_FACE_GRID = colors::gold;


bool try_ray_face_intersection(const shared::brush_polyhedron_t& hull,
                               const shared::brush_face_t& face,
                               const linalg::vec3& ray_origin,
                               const linalg::vec3& ray_direction,
                               float& out_distance)
{
  const float denominator = linalg::dot(face.plane.normal, ray_direction);
  if (denominator > -1e-6f) return false; // parallel, or hitting the back of the face

  // plane.point is just the first point of the face. 
  //                   ^ normal
  //                   |
  //                   |
  // plane   ----------P---------X----------
  //                   |        /
  //                   |       /
  //                   h      /   <- distance  (returned; along the ray)
  //                   |     /
  //                   |    /
  //                   |   /
  //                   O--/
  //              ray_origin
  const float distance = linalg::dot(face.plane.normal, face.plane.point - ray_origin) / denominator;
  if (distance < 0.0f) return false;

  const linalg::vec3 hit = ray_origin + ray_direction * distance;

  // for all the edges of the face:
  // create a cross product between the a -> b and a -> intersection.
  // the winding is counter clockwise. that cross is either positive or negative. that depends on which side the hit is on.
  // if it is on the _left_ side (inside the face), it points along the normal. if it is on the _right_ side, it is inverted from the normal.
  // you do this for all edges, so if for all edges, the cross product points along the normal, you are inside this face.
  // note that this only works for convex polygons! another reason why that's so important.
  for (size_t i = 0; i < face.vertex_indices.size(); ++i)
  {
    const linalg::vec3 &start = hull.vertices[face.vertex_indices[i]];
    const linalg::vec3 &end =
        hull.vertices[face.vertex_indices[(i + 1) %
                                          face.vertex_indices.size()]];

    if (linalg::dot(linalg::cross(end - start, hit - start),
                    face.plane.normal) < -1e-3f)
      return false;
  }

  out_distance = distance;
  return true;
}

// A face is identified by its normal, which is what survives a rebuild when
// indices do not.
constexpr float FACE_NORMAL_MATCH = 0.99f;

std::optional<size_t>
try_find_face_matching_normal(const shared::brush_polyhedron_t& hull,
                              const linalg::vec3& normal)
{
  std::optional<size_t> best_index;
  float best_alignment = FACE_NORMAL_MATCH;

  for (size_t i = 0; i < hull.faces.size(); ++i)
  {
    const float alignment = linalg::dot(hull.faces[i].plane.normal, normal);
    if (alignment > best_alignment)
    {
      best_alignment = alignment;
      best_index = i;
    }
  }
  return best_index;
}

// similar principle as above.
bool point_is_inside_face(const shared::brush_polyhedron_t &hull,
                          const shared::brush_face_t& face,
                          const linalg::vec3& point)
{
  for (size_t i = 0; i < face.vertex_indices.size(); ++i)
  {
    const linalg::vec3 &start = hull.vertices[face.vertex_indices[i]];
    const linalg::vec3 &end =
        hull.vertices[face.vertex_indices[(i + 1) %
                                          face.vertex_indices.size()]];

    if (linalg::dot(linalg::cross(end - start, point - start),
                    face.plane.normal) < -1e-3f)
      return false;
  }
  return true;
}

// Which of `handles` is under `screen_position`, if any. Nearest wins, so two
// handles inside one pick radius collapse -- the caller decides WHICH set it is
// asking about.
[[nodiscard]] std::optional<size_t>
try_pick_vertex_handle(Span<const linalg::vec3> handles,
                       const viewport_state_t& view,
                       const linalg::vec2& screen_position)
{
  std::optional<size_t> best_index;
  float best_distance = HANDLE_PICK_RADIUS;

  for (size_t idx = 0; idx < handles.size(); ++idx)
  {
    // where, on the screen, does this end point up?
    const std::optional<linalg::vec2> projected =
        try_project_to_screen(view, handles[idx]);
    if (!projected) continue;

    const float dx = projected->x - screen_position.x;
    const float dy = projected->y - screen_position.y;
    const float distance = std::sqrt(dx * dx + dy * dy);

    if (distance < best_distance)
    {
      best_distance = distance;
      best_index = idx;
    }
  }

  return best_index;
}

//@NOTE(SJM): you should probably re-evaluate this later because the intuition is lacking now.
// axis is always normal (since that's the axis you want to draw along)
// and the anchor is like the centroid or a selected point.
bool try_distance_along_axis(const linalg::vec3& anchor,
                             const linalg::vec3& axis,
                             const linalg::ray_t& ray,
                             float& out_distance_along_axis)
{
  const linalg::vec3 between = anchor - ray.origin;
  // ray.direction is normalized here. we don't use between here.
  const float axis_dot_ray = linalg::dot(axis, ray.direction);
  const float denominator = 1.0f - axis_dot_ray * axis_dot_ray;

  if (std::abs(denominator) < 1e-5f)
    return false; // looking straight down the axis: no usable projection

  out_distance_along_axis = (linalg::dot(between, ray.direction) * axis_dot_ray -
                             linalg::dot(between, axis)) /
                            denominator;
  return true;
}


// this footprint cannot be built. why?
[[nodiscard]] const char *try_explain_unbuildable_footprint(Span<const linalg::vec3> points)
{
  // consolidate.
  const size_t distinct = shared::weld_brush_points(points).size();

  // arbitrary but just so it isn't slow.
  if (distinct > shared::MAX_BRUSH_VERTICES)
    return "too many points for one brush -- select a rectilinear area so it "
           "splits into rectangles, or raise the grid with ]";

  if (distinct < 4)
    return "too few distinct points to enclose anything";

  return nullptr;
}

// some functions call this, other functions call try. not quite sure why we make the distinction?
[[nodiscard]] const char *explain_unbuildable_footprint(Span<const linalg::vec3> points)
{
  if (const char *cheap = try_explain_unbuildable_footprint(points))
    return cheap;

  return "the footprint is flat -- points in a line enclose no volume";
}

// how many vertices were swallowed in the construction of the solid?
// note that welding vertices together does not count as swallowing here.
size_t count_selected_points_swallowed_by(const std::vector<linalg::vec3>& selected_points,
                                          const shared::brush_polyhedron_t& solid)
{
  size_t swallowed = 0;

  for (const linalg::vec3 &selected_point : selected_points)
  {
    bool kept = false;
    for (const linalg::vec3 &vertex : solid.vertices)
    {
      if (std::abs(selected_point.x - vertex.x) <= shared::BRUSH_WELD_EPSILON &&
          std::abs(selected_point.y - vertex.y) <= shared::BRUSH_WELD_EPSILON &&
          std::abs(selected_point.z - vertex.z) <= shared::BRUSH_WELD_EPSILON)
      {
        kept = true;
        break;
      }
    }

    if (!kept)
      ++swallowed;
  }

  return swallowed;
}

} // namespace

void Brush_Tool::on_enable(editor_context_t &ctx)
{
  assert(ctx.map);
  assert(ctx.transaction_system);

  mode = Mode::Face;
  select_brush_face(ctx, shared::invalid_entity_uid, std::nullopt);
  hover = {};
  cancel_pending_extrusion();
  end_drag();
}

void Brush_Tool::on_disable(editor_context_t &ctx)
{
  // A pending extrusion is an edit the user asked for and has not taken back,
  // so leaving the tool keeps it rather than dropping it on the floor.
  commit_pending_extrusion(ctx);

  // Same reasoning for a stroke in progress: it is an edit already visible in
  // the map, so it is committed rather than left without an undo entry.
  end_paint_stroke(ctx);

  end_drag();
}

shared::brush_geometry_t* Brush_Tool::try_get_selected_brush(editor_context_t& ctx)
{
  if (selection.uid == shared::invalid_entity_uid || !ctx.map)
    return nullptr;

  shared::map_geometry_t* entry = ctx.map->find_geometry_by_uid(selection.uid);

  if (!entry) return nullptr;

  return std::get_if<shared::brush_geometry_t>(&entry->value);
}

void Brush_Tool::clear_point_selection() { selection.points.clear(); }

void Brush_Tool::end_drag() { drag = {}; }

void Brush_Tool::cancel_pending_extrusion() { extrusion = {}; }


float Brush_Tool::grid_step_for(const editor_context_t& ctx,
                                const input::modifiers_t& modifiers) const
{
  if (modifiers.alt) return 0.0f;

  return ctx.grid ? ctx.grid->step() : 1.0f;
}

bool Brush_Tool::point_is_selected(const linalg::vec3 &point) const
{
  for (const linalg::vec3 &selected : selection.points)
  {
    if (std::abs(point.x - selected.x) <= shared::BRUSH_WELD_EPSILON &&
        std::abs(point.y - selected.y) <= shared::BRUSH_WELD_EPSILON &&
        std::abs(point.z - selected.z) <= shared::BRUSH_WELD_EPSILON)
      return true;
  }
  return false;
}

void Brush_Tool::toggle_point_selection(const linalg::vec3 &point,
                                        bool additive)
{

  // if a point is selected while additive mode is not on, replace whatever was there with this individual point.
  if (!additive)
  {
    selection.points.assign(1, point);
    return;
  }

  // otherwise, if a point is selected that _was_ already selected, remove it from the selection_geometry.
  for (size_t i = 0; i < selection.points.size(); ++i)
  {
    const linalg::vec3& selected = selection.points[i];
    if (std::abs(point.x - selected.x) <= shared::BRUSH_WELD_EPSILON &&
        std::abs(point.y - selected.y) <= shared::BRUSH_WELD_EPSILON &&
        std::abs(point.z - selected.z) <= shared::BRUSH_WELD_EPSILON)
    {
      selection.points.erase(selection.points.begin() + (long)i);
      return;
    }
  }

  // if none of those are true, we must just be adding this point to the selection_geometry.
  selection.points.push_back(point);
}


// A press that armed a band whose cursor never travelled was a click after all.
void Brush_Tool::resolve_band_press_as_click(editor_context_t &ctx)
{
  const band_t& band = drag.band;

  // Empty space: the click clears the whole selection, which is what the old
  // press-time reselect did when it hovered nothing.
  if (band.press_uid == shared::invalid_entity_uid)
  {
    select_brush_face(ctx, shared::invalid_entity_uid, std::nullopt);
    return;
  }

  const bool same_brush = band.press_uid == selection.uid;
  const bool same_face =
      same_brush && band.press_face_normal && selection.face_normal &&
      linalg::dot(*band.press_face_normal, *selection.face_normal) >
          FACE_NORMAL_MATCH;

  if (same_face)
  {
    if (!band.adds_to_point_selection) clear_point_selection();
    return;
  }

  select_brush_face(ctx, band.press_uid, band.press_face_normal);
}

void Brush_Tool::select_brush_face(editor_context_t &ctx,
                                   shared::entity_uid_t uid,
                                   std::optional<linalg::vec3> face_normal)
{
  // Points are handles ON a face, so leaving a face drops them. The two call
  // sites used to disagree about this: a click kept points from the face it
  // just left, which then extruded along the NEW face's normal.
  const bool same_face =
      uid != shared::invalid_entity_uid && uid == selection.uid &&
      face_normal && selection.face_normal &&
      linalg::dot(*face_normal, *selection.face_normal) > FACE_NORMAL_MATCH;

  if (!same_face) clear_point_selection();

  selection.uid = uid;
  selection.face_normal = face_normal;

  rebuild_hull_and_handles(ctx);
}

void Brush_Tool::rebuild_hull_and_handles(editor_context_t &ctx)
{
  selection_geometry.hull.reset();
  selection_geometry.face_idx = INVALID_FACE;
  selection_geometry.vertex_handles.clear();
  selection_geometry.handles_are_grid_vertices = false;

  const shared::brush_geometry_t* brush = try_get_selected_brush(ctx);
 
  if (!brush) return;

  selection_geometry.hull = shared::try_build_brush_polyhedron(brush->vertices);

  if (!selection_geometry.hull) return;

  if (!selection.face_normal) return;

  const std::optional<size_t> matched =
      try_find_face_matching_normal(*selection_geometry.hull, *selection.face_normal);

  if (!matched) return;

  selection_geometry.face_idx = (int)*matched;

  // track the face.
  selection.face_normal =
      selection_geometry.hull->faces[(size_t)selection_geometry.face_idx].plane.normal;

  // fetch the handles of the face that we were selecting.
  const shared::brush_face_t& face = selection_geometry.hull->faces[(size_t)selection_geometry.face_idx];

  // A subdivided face's handles are its GRID, and nothing else: its corners are
  // grid vertices too (the four of them), and the editor grid's points are not
  // on it at all once it has been sculpted.
  {
    const shared::brush_face_grids_t grids =
        shared::build_brush_face_grids(*brush, *selection_geometry.hull);
    const std::vector<linalg::vec3> &grid =
        grids.grid_vertices[(size_t)selection_geometry.face_idx];
    if (!grid.empty())
    {
      selection_geometry.vertex_handles           = grid;
      selection_geometry.handles_are_grid_vertices = true;
      return;
    }
  }

  const float step = ctx.grid ? ctx.grid->step() : 128.0f;

  // Real corners are always handles, on the grid or not.
  for (uint32_t index : face.vertex_indices)
    selection_geometry.vertex_handles.push_back(selection_geometry.hull->vertices[index]);

  if (step <= 0.0f) return;

  linalg::vec3 tangent_u;
  linalg::vec3 tangent_v;

  // note u and v are arbitrary but always the same. I was initially thinking
  // we could align one of the axes with the longest edge of the face, but that would mean
  // if the brush changes it can oscillate or change or whatever, and then it's unstable and it's not nice.
  // if dot(normal, world_up) almost 1, we are very flat and we just align u,v with x,z.
  shared::brush_face_grid_tangents(face.plane.normal, tangent_u, tangent_v);

  float min_u = 0.0f;
  float max_u = 0.0f;
  float min_v = 0.0f;
  float max_v = 0.0f;

  // ALL OF THIS IS IN SERVICE OF DRAWING GRID LINES ON THE FACE.
  //@NOTE(SJM): this loops over indices to initialize minuv? weird.)
  // it's ipmortant to conceptualize that we are asking:
  // How much of this position vector points in the u direction?
  // this is _NOT_ in relation to any point on the face.
  // so it's not:
  // measure this vertex relative to the u arrow sitting on the face.
  // it's:
  // project this world-space position onto the u direction.
  // find the u,v span of this face in world space coordinates?
  for (size_t i = 0; i < face.vertex_indices.size(); ++i)
  {
    const linalg::vec3& vertex = selection_geometry.hull->vertices[face.vertex_indices[i]];
    const float u = linalg::dot(vertex, tangent_u);
    const float v = linalg::dot(vertex, tangent_v);

    if (i == 0)
    {
      min_u = max_u = u;
      min_v = max_v = v;
      continue;
    }

    min_u = std::min(min_u, u);
    max_u = std::max(max_u, u);
    min_v = std::min(min_v, v);
    max_v = std::max(max_v, v);
  }

  // walking in world u/v means an axis-aligned face gets exactly the world
  // grid, which is what makes the handles line up with everything else in the
  // editor.
  const float plane_distance = linalg::dot(face.plane.normal, face.plane.point);
  const int first_u = (int)std::ceil(min_u / step);
  const int last_u  = (int)std::floor(max_u / step);
  const int first_v = (int)std::ceil(min_v / step);
  const int last_v  = (int)std::floor(max_v / step);

  // A face spanning a thousand grid cells is a wall of dots nobody can click.
  constexpr int MAX_VERTEX_HANDLE_SPAN = 64;
  if (last_u - first_u > MAX_VERTEX_HANDLE_SPAN ||
      last_v - first_v > MAX_VERTEX_HANDLE_SPAN)
    return;

  for (int iv = first_v; iv <= last_v; ++iv)
  {
    for (int iu = first_u; iu <= last_u; ++iu)
    {
      // Rebuild the world point from its two in-plane coordinates and the
      // plane.
      const linalg::vec3 in_plane =
          tangent_u * ((float)iu * step) + tangent_v * ((float)iv * step);
      const linalg::vec3 point = in_plane + face.plane.normal * (plane_distance - linalg::dot(face.plane.normal, in_plane));

      if (!point_is_inside_face(*selection_geometry.hull, face, point)) continue;

      bool duplicates_a_corner = false;
      for (uint32_t index : face.vertex_indices)
      {
        const linalg::vec3 &corner = selection_geometry.hull->vertices[index];
        if (std::abs(point.x - corner.x) <= shared::BRUSH_WELD_EPSILON &&
            std::abs(point.y - corner.y) <= shared::BRUSH_WELD_EPSILON &&
            std::abs(point.z - corner.z) <= shared::BRUSH_WELD_EPSILON)
          duplicates_a_corner = true;
      }

      if (!duplicates_a_corner) selection_geometry.vertex_handles.push_back(point);
    }
  }
}

// rebuild after a modification.
bool Brush_Tool::try_rebuild_selected_brush(editor_context_t& ctx, std::vector<linalg::vec3> vertices)
{
  shared::brush_geometry_t* brush = try_get_selected_brush(ctx);
  if (!brush) return false;

  std::optional<shared::brush_polyhedron_t> candidate = shared::try_build_brush_polyhedron(vertices);
  if (!candidate)
  {
    log_warning(
        "brush_tool: that edit would collapse the brush — leaving it alone");
    return false;
  }

  // hand the candidate data back over.
  brush->vertices = candidate->vertices;

  // Faces are DERIVED, so their surfaces have to be re-keyed against the hull
  // that just changed shape -- see geometry_def.md ss6.
  shared::sync_face_surfaces(*brush);

  refresh_generated_geometry_mesh(*brush, selection.uid, ctx.map->materials);

  if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;

  return true;
}

// nudging means meaneuver in the x,z plane, I think.
void Brush_Tool::nudge_selected_brush(editor_context_t &ctx,
                                      const linalg::vec3 &direction)
{
  shared::brush_geometry_t* brush = try_get_selected_brush(ctx);
  if (!brush) return;

  // nudge by a grid step.
  const float step = ctx.grid ? ctx.grid->step() : 0.0f;
  if (step <= 0.0f) return;

  const shared::geometry_value_t before = *brush;

  // Moves the points AND carries the face surfaces with them -- the keys along
  // their normals, the UV shifts the other way, so a nudge does not slide the
  // texture across the faces.
  shared::translate_brush(*brush, direction * step);

  // because the vertices have moved, we need to update the mesh, since they are fully decoupled.
  refresh_generated_geometry_mesh(*brush, selection.uid, ctx.map->materials);

  for (linalg::vec3& point : selection.points)
  {
    point = point + direction * step;
  }

  if (ctx.transaction_system)
  {
    transaction_t transaction;
    transaction.add_geometry_modified(selection.uid, before, *brush);
    ctx.transaction_system->push(std::move(transaction));
  }

  if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
}

void Brush_Tool::delete_selected_brush(editor_context_t &ctx)
{
  if (!try_get_selected_brush(ctx))
    return;

  const shared::entity_uid_t uid = selection.uid;
  const shared::geometry_value_t removed = ctx.map->find_geometry_by_uid(uid)->value;

  ctx.map->remove_geometry(uid);

  if (ctx.transaction_system)
  {
    transaction_t transaction;
    transaction.add_geometry_removed(uid, removed);
    ctx.transaction_system->push(std::move(transaction));
  }

  // Everything below is keyed to the brush that no longer exists.
  end_drag();
  cancel_pending_extrusion();
  select_brush_face(ctx, shared::invalid_entity_uid, std::nullopt);
  hover = {};

  if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
}

Brush_Tool::pending_extrusion_solids_t Brush_Tool::build_pending_extrusion_solids(float grid_step) const
{
  if (selection.points.empty() || !selection.face_normal)
    return {};

  // a brush is by definition convex so a concave footprint (footprint meaning the selected vertices, think like an L-shape) needs one brush per rectangle.
  std::optional<std::vector<std::vector<linalg::vec3>>> rectangles =
      shared::try_decompose_footprint_into_rectangles(
          selection.points, *selection.face_normal, grid_step);
  
  // the footprint decomposed into multiple brushes:
  if (rectangles)
  {
    pending_extrusion_solids_t pending;
    pending.every_selected_point_accounted_for = true;
    pending.point_sets.reserve(rectangles->size());

    for (const std::vector<linalg::vec3>& rectangle : *rectangles)
      pending.point_sets.push_back(shared::extrude_brush_hull(
          rectangle, *selection.face_normal, extrusion.depth));

    return pending;
  }

  // it's just a single brush.
  return {{shared::extrude_brush_hull(selection.points, *selection.face_normal,
                                      extrusion.depth)},
          false};
}

void Brush_Tool::commit_pending_extrusion(editor_context_t& ctx)
{
  if (!extrusion.pending) return;

  const pending_extrusion_solids_t pending = build_pending_extrusion_solids(ctx.grid ? ctx.grid->step() : 0.0f);
  const std::vector<linalg::vec3> selected_points = selection.points;
  cancel_pending_extrusion();

  shared::geometry_surface_t surface;
  if (const shared::brush_geometry_t* source = try_get_selected_brush(ctx))
    surface = source->surface;

  transaction_t transaction;
  shared::entity_uid_t last_created = shared::invalid_entity_uid;
  size_t created_count = 0;

  for (const std::vector<linalg::vec3> &points : pending.point_sets)
  {
    std::optional<shared::brush_polyhedron_t> solid =
        shared::try_build_brush_polyhedron(points);
    if (!solid) continue;

    if (!pending.every_selected_point_accounted_for)
    {
      const size_t number_of_selected_points_swallowed =
          count_selected_points_swallowed_by(selected_points, *solid);
      if (number_of_selected_points_swallowed > 0)
        log_warning(
            "brush_tool: {} selected point(s) ended up inside the new brush. brush is convex, so a concave footprint fills in. Select the whole area rather than its outline and it splits into a brush per rectangle instead",
            number_of_selected_points_swallowed);
    }

    auto created_brush_geometry = shared::brush_geometry_t{};
    created_brush_geometry.vertices = solid->vertices;
    created_brush_geometry.surface = surface;
    shared::sync_face_surfaces(created_brush_geometry);

    const shared::entity_uid_t uid = ctx.map->add_geometry(created_brush_geometry);
    transaction.add_geometry_created(uid, created_brush_geometry);

    last_created = uid;
    ++created_count;
  }

  if (created_count == 0)
  {
    const char* why = pending.point_sets.empty()
                          ? "nothing selected"
                          : explain_unbuildable_footprint(pending.point_sets.front());
    hud::set_announcement("extrusion discarded: not a solid");
    log_warning("brush_tool: the pending extrusion is not a solid: discarded ({})",
                why);
    return;
  }

  // One transaction however many pieces it took, so the L undoes as the one
  // edit it was.
  ctx.transaction_system->push(std::move(transaction));

  if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;

  // Follow the thing that was just made, on the face we just extruded to.
  select_brush_face(ctx, last_created, selection.face_normal);
}

void Brush_Tool::on_update(editor_context_t &ctx, const viewport_state_t &view,
                           float dt)
{
  cached_view = view;

  // A live drag owns the cursor, so hover stops tracking under it.
  if (!drag_is_live())
  {
    hover.uid = shared::invalid_entity_uid;
    hover.face_normal.reset();

    //raycast to find hovered brush / face.
    if (ctx.bvh)
    {
      ray_hit_result_t hit{};
      if (bvh_intersect_ray(*ctx.bvh, view.mouse_ray.origin,
                            view.mouse_ray.direction, hit) &&
          hit.id.type == Collision_Id::Type::Static_Geometry)
      {
        const shared::entity_uid_t uid = hit.id.index;
        if (const shared::map_geometry_t *entry =
                ctx.map->find_geometry_by_uid(uid))
        {
          if (const shared::brush_geometry_t *brush =
                  std::get_if<shared::brush_geometry_t>(&entry->value))
          {
            hover.uid = uid;

            std::optional<shared::brush_polyhedron_t> hovered_hull =
                shared::try_build_brush_polyhedron(brush->vertices);

            if (hovered_hull)
            {
              float distance_to_nearest_face = 0.0f;
              for (const shared::brush_face_t& face : hovered_hull->faces)
              {
                float distance = 0.0f;
                if (!try_ray_face_intersection(
                        *hovered_hull, face, view.mouse_ray.origin,
                        view.mouse_ray.direction, distance))
                  continue;

                if (!hover.face_normal || distance < distance_to_nearest_face)
                {
                  distance_to_nearest_face = distance;
                  hover.face_normal = face.plane.normal;
                }
              }
            }
          }
        }
      }
    }
  }

  rebuild_hull_and_handles(ctx);
  update_paint_cursor(ctx, dt);
}

// ============================================================================
// Mouse
// ============================================================================

void Brush_Tool::on_mouse_down(editor_context_t &ctx,
                               const input::mouse_event_t &e)
{
  // only lmb does anything.
  if (e.button != input::mouse_button_t::Left) return;

  const linalg::vec2 screen_position{(float)e.position.x, (float)e.position.y};

  if (mode == Mode::Paint)
  {
    // A press on a DIFFERENT brush picks it and does not paint: the cursor is
    // resolved against the selected brush, so painting one you have not
    // selected yet would write at a point measured on something else.
    if (hover.uid != shared::invalid_entity_uid && hover.uid != selection.uid)
    {
      select_brush_face(ctx, hover.uid, hover.face_normal);
      return;
    }

    shared::brush_geometry_t *brush = try_get_selected_brush(ctx);
    if (!brush || !paint.cursor)
      return;

    paint.geometry_at_the_start_of_stroke = *brush;
    paint.stroking = true;
    return;
  }

  // shift means extrude. Not on a subdivided face: an extrusion rebuilds the
  // point set, and the grid it would throw away is the whole face. Refused out
  // loud AND consuming the press -- falling through dragged a grid vertex
  // instead, which reads as the tool ignoring the shift.
  if (mode == Mode::Vertex && selection_geometry.hull && selection_geometry.face_idx >= 0 &&
      e.mods.shift && selection_geometry.handles_are_grid_vertices)
  {
    // ASCII: the announcement font bakes printable ASCII and nothing else.
    hud::set_announcement(
        "cannot extrude a subdivided face - set its subdivision to 0 first");
    return;
  }

  if (mode == Mode::Vertex && selection_geometry.hull && selection_geometry.face_idx >= 0 &&
      e.mods.shift && !selection_geometry.handles_are_grid_vertices)
  {
    const std::optional<size_t> clicked_handle = try_pick_vertex_handle(
        selection_geometry.vertex_handles, cached_view, screen_position);

    if (selection.points.empty())
    {
      // nothing to extrude.
      if (!clicked_handle)
        return;

      toggle_point_selection(selection_geometry.vertex_handles[*clicked_handle],
                             false);
    }

    drag.axis.direction = *selection.face_normal;

    if (clicked_handle)
    {
      drag.axis.anchor = selection_geometry.vertex_handles[*clicked_handle];
    }
    else // middle of the footprint.
    {
      drag.axis.anchor = {0, 0, 0};
      for (const linalg::vec3 &point : selection.points)
        drag.axis.anchor = drag.axis.anchor + point;
      drag.axis.anchor = drag.axis.anchor * (1.0f / (float)selection.points.size());
    }

    float cursor_distance_along_face_normal = 0.0f;
    try_distance_along_axis(drag.axis.anchor, drag.axis.direction, cached_view.mouse_ray,
                            cursor_distance_along_face_normal);

  
    // continue dragging from already-extruded height (depth?)
    drag.axis.start_distance = cursor_distance_along_face_normal - extrusion.depth;

    extrusion.pending = true;
    drag.kind = Drag::Extrusion_Depth;
    return;
  }


  if (extrusion.pending)
  {
    const std::optional<size_t> clicked_handle = try_pick_vertex_handle(
        selection_geometry.vertex_handles, cached_view, screen_position);
    if (clicked_handle)
    {
      toggle_point_selection(selection_geometry.vertex_handles[*clicked_handle],
                             true);
      return;
    }

    // click anywhere there isn't an handle while we are extruding.

    commit_pending_extrusion(ctx);
    return;
  }

  if (mode == Mode::Vertex && selection_geometry.hull && selection_geometry.face_idx >= 0)
  {
    const std::optional<size_t> clicked_handle = try_pick_vertex_handle(
        selection_geometry.vertex_handles, cached_view, screen_position);
   
    if (clicked_handle)
    {
      const linalg::vec3 point = selection_geometry.vertex_handles[*clicked_handle];

      if (point_is_selected(point) && e.mods.ctrl)
      {
        toggle_point_selection(point, true); // ctrl on a selected point drops it
        return;
      }

      if (!point_is_selected(point))
        toggle_point_selection(point, e.mods.ctrl);

      drag.kind = Drag::Vertices;
      drag.vertex_start_points = selection.points;
      drag.axis.direction = *selection.face_normal;
      drag.axis.anchor = point;
      try_distance_along_axis(drag.axis.anchor, drag.axis.direction, cached_view.mouse_ray,
                              drag.axis.start_distance);

      if (shared::brush_geometry_t *brush = try_get_selected_brush(ctx))
        drag.geometry_at_the_start_of_drag = *brush;
      return;
    }


    // if there's _no other way_ to interpret this click, we are box selecting (banding).

    drag.kind                         = Drag::Band_Armed;
    drag.band.start                   = screen_position;
    drag.band.end                     = screen_position;
    drag.band.adds_to_point_selection = e.mods.ctrl;
    drag.band.press_uid               = hover.uid;
    drag.band.press_face_normal       = hover.face_normal;
    return;
  }

  // nothing meaningful to select -> this is actually awful style. we should just clear the selection and fuck off instead of relying on select brush face
  // to filter out our nonsense. early return is fine but whatever.,
  if (hover.uid == shared::invalid_entity_uid)
  {
    select_brush_face(ctx, shared::invalid_entity_uid, std::nullopt);
    return;
  }

  // we have something to hover over.
  select_brush_face(ctx, hover.uid, hover.face_normal);

  if (mode == Mode::Face && selection.face_normal)
  {
    // invalid face? 
    if (selection_geometry.face_idx == INVALID_FACE) return;


    drag.axis.direction = *selection.face_normal;
    drag.axis.anchor = selection_geometry.hull->faces[(size_t)selection_geometry.face_idx].plane.point;
    if (!try_distance_along_axis(drag.axis.anchor, drag.axis.direction, cached_view.mouse_ray,
                                 drag.axis.start_distance))
      return;

    drag.kind = Drag::Face;
    if (shared::brush_geometry_t *brush = try_get_selected_brush(ctx))
      drag.geometry_at_the_start_of_drag = *brush;
  }
}

void Brush_Tool::on_mouse_drag(editor_context_t &ctx,
                               const input::mouse_event_t &e)
{
  const linalg::vec2 screen_position{(float)e.position.x, (float)e.position.y};
  const float step = grid_step_for(ctx, e.mods);

  if (drag.kind == Drag::Band_Armed)
  {
    const float band_x_extent = screen_position.x - drag.band.start.x;
    const float band_y_extent = screen_position.y - drag.band.start.y;
    if (std::sqrt(band_x_extent * band_x_extent + band_y_extent * band_y_extent) <
        BAND_DRAG_THRESHOLD)
      return;

    drag.kind = Drag::Band_Sizing;
  }

  // The rect is rebuilt from scratch every frame, which is what lets shrinking
  // it take a point back. A ctrl-band skips the clear and so only ever adds.
  if (drag.kind == Drag::Band_Sizing)
  {
    drag.band.end = screen_position;

    const float min_x = std::min(drag.band.start.x, drag.band.end.x);
    const float max_x = std::max(drag.band.start.x, drag.band.end.x);
    const float min_y = std::min(drag.band.start.y, drag.band.end.y);
    const float max_y = std::max(drag.band.start.y, drag.band.end.y);

    if (!drag.band.adds_to_point_selection)
      clear_point_selection();

    for (const linalg::vec3& point : selection_geometry.vertex_handles)
    {
      const std::optional<linalg::vec2> point_screen_position =
          try_project_to_screen(cached_view, point);
      if (!point_screen_position) continue;

      // point is in selection range and wasn't already selected.
      if (point_screen_position->x >= min_x && point_screen_position->x <= max_x &&
          point_screen_position->y >= min_y && point_screen_position->y <= max_y &&
          !point_is_selected(point))
        selection.points.push_back(point);
    }
    return;
  }

  // Everything left drags along drag.axis, and nothing else has one.
  if (drag.kind != Drag::Face && drag.kind != Drag::Vertices &&
      drag.kind != Drag::Extrusion_Depth)
    return;

  float distance_along_axis = 0.0f;
  if (!try_distance_along_axis(drag.axis.anchor, drag.axis.direction, cached_view.mouse_ray,
                               distance_along_axis))
    return;

  float travel_distance_along_axis = distance_along_axis - drag.axis.start_distance;
  if (step > 0.0f)
    travel_distance_along_axis = std::round(travel_distance_along_axis / step) * step;

  if (drag.kind == Drag::Extrusion_Depth)
  {
    extrusion.depth = travel_distance_along_axis;
    return;
  }

  if (drag.kind == Drag::Face && drag.geometry_at_the_start_of_drag)
  {
    // Move only the vertices that were ON the face when the drag began.
    // the brush_geometry is _not_ the brush polyhedron: we don't have particular faces here.
    // therefore, we need to re-find the vertices on the plane.
    const shared::brush_geometry_t& start =
        std::get<shared::brush_geometry_t>(*drag.geometry_at_the_start_of_drag);

    const float face_distance = linalg::dot(drag.axis.direction, drag.axis.anchor);

    std::vector<linalg::vec3> moved = start.vertices;
    for (linalg::vec3 &vertex : moved)
    {
      if (std::abs(linalg::dot(drag.axis.direction, vertex) - face_distance) <=
          shared::BRUSH_COPLANAR_EPSILON)
        vertex = vertex + drag.axis.direction * travel_distance_along_axis;
    }

    try_rebuild_selected_brush(ctx, std::move(moved));
    return;
  }

  if (drag.kind == Drag::Vertices && drag.geometry_at_the_start_of_drag)
  {
    const shared::brush_geometry_t &start =
        std::get<shared::brush_geometry_t>(*drag.geometry_at_the_start_of_drag);


    selection.points = drag.vertex_start_points;
    for (linalg::vec3 &point : selection.points)
      point = point + drag.axis.direction * travel_distance_along_axis;

    // A grid vertex is an OFFSET on the face, not a point in the brush's set, so
    // it moves without touching the hull -- which is what makes sculpting a face
    // not a re-hull per frame, and what keeps the face's identity through it.
    if (selection_geometry.handles_are_grid_vertices)
    {
      shared::brush_geometry_t *live = try_get_selected_brush(ctx);
      if (!live)
        return;

      *live = start;
      shared::nudge_brush_grid_vertices(
          *live, drag.vertex_start_points,
          drag.axis.direction * travel_distance_along_axis);

      refresh_generated_geometry_mesh(*live, selection.uid, ctx.map->materials);
      rebuild_hull_and_handles(ctx);
      return;
    }

    std::vector<linalg::vec3> moved = start.vertices;
    for (linalg::vec3 &vertex : moved)
    {
      for (const linalg::vec3 &start_point : drag.vertex_start_points)
      {
        if (std::abs(vertex.x - start_point.x) <= shared::BRUSH_WELD_EPSILON &&
            std::abs(vertex.y - start_point.y) <= shared::BRUSH_WELD_EPSILON &&
            std::abs(vertex.z - start_point.z) <= shared::BRUSH_WELD_EPSILON)
        {
          vertex = vertex + drag.axis.direction * travel_distance_along_axis;
          break;
        }
      }
    }

    // try to rebuild the brush if possible.
    try_rebuild_selected_brush(ctx, std::move(moved));
    return;
  }
}

void Brush_Tool::on_mouse_up(editor_context_t& ctx,
                             const input::mouse_event_t& e)
{
  // only lmb here.
  if (e.button != input::mouse_button_t::Left) return;

  if (paint.stroking)
  {
    end_paint_stroke(ctx);
    return;
  }

  // A press that never travelled was a CLICK, and only the release can know
  // that. Everything the press deliberately did not do happens here.
  if (drag.kind == Drag::Band_Armed)
    resolve_band_press_as_click(ctx);

  if ((drag.kind == Drag::Face || drag.kind == Drag::Vertices) &&
      drag.geometry_at_the_start_of_drag)
  {
    // The drag wrote straight into the map so it could be seen; the transaction
    // is the whole gesture, pushed once here.
    if (shared::brush_geometry_t* brush = try_get_selected_brush(ctx))
    {
      if (ctx.transaction_system)
      {
        transaction_t transaction;
        transaction.add_geometry_modified(selection.uid, *drag.geometry_at_the_start_of_drag,
                                      *brush);
        ctx.transaction_system->push(std::move(transaction));
      }
    }
  }

  // Releasing ends the DRAG, not the extrusion: that stands until Enter or
  // Escape so more points can be added to its hull.
  end_drag();
}

//@FIXME(SMIA): the direction_from_angles here is not really intuitive and I don't understand.
void Brush_Tool::on_key_down(editor_context_t &ctx, const key_event_t &e)
{

  // what this boils down to is, dependent on the camera angle, the arrow keys mean somethng different.
  const linalg::vec3 forward = linalg::direction_from_angles(cached_view.camera.yaw, 0.0f);
  const linalg::vec3 right{-forward.z, 0.0f, forward.x};
  const linalg::vec3 back = forward * -1.f;
  const linalg::vec3 left = right *  -1.f;
  const linalg::vec3 up = {0, 1, 0};
  const linalg::vec3 down = {0, -1, 0};

  auto dominant_axis = [](const linalg::vec3 &direction) -> linalg::vec3
  {
    if (std::abs(direction.x) >= std::abs(direction.z))
      return {direction.x >= 0.0f ? 1.0f : -1.0f, 0, 0};
    return {0, 0, direction.z >= 0.0f ? 1.0f : -1.0f};
  };

  switch (e.key)
  {
  case input::key_t::Arrow_Left:
    nudge_selected_brush(ctx, dominant_axis(left));
    return;
  case input::key_t::Arrow_Right:
    nudge_selected_brush(ctx, dominant_axis(right));
    return;
  case input::key_t::Arrow_Up:
    nudge_selected_brush(ctx, dominant_axis(forward));
    return;
  case input::key_t::Arrow_Down:
    nudge_selected_brush(ctx, dominant_axis(back));
    return;
  case input::key_t::Page_Up:
    nudge_selected_brush(ctx, up);
    return;
  case input::key_t::Page_Down:
    nudge_selected_brush(ctx, down);
    return;

  case input::key_t::Tab:
    // tab is another failsafe "get me out of here" to switch between selection modes.
    commit_pending_extrusion(ctx);
    end_drag();
    end_paint_stroke(ctx);
    mode = (mode == Mode::Face)     ? Mode::Vertex
           : (mode == Mode::Vertex) ? Mode::Paint
                                    : Mode::Face;
    clear_point_selection();
    return;

  case input::key_t::Enter:
    commit_pending_extrusion(ctx);
    return;

  case input::key_t::Delete:
  case input::key_t::Backspace:
    // A pending extrusion belongs to the brush that is about to go, so it is
    // dropped rather than committed onto a brush that no longer exists.
    delete_selected_brush(ctx);
    return;

  // get me out of this thing.
  case input::key_t::Escape:
  {
    end_drag();
    if (extrusion.pending)
    {
      cancel_pending_extrusion();
      return;
    }
    select_brush_face(ctx, selection.uid, std::nullopt);
    return;
  }
    
  // select all vertices on the face.
  case input::key_t::A:
  {
    if (mode == Mode::Vertex && e.mods.ctrl)
      selection.points = selection_geometry.vertex_handles;
    return;
  }

  // Sample the face under the cursor. Everything BUT the key: a key is the
  // face's own identity, and pasting one would re-point the target at the plane
  // it was copied from.
  case input::key_t::C:
  {
    const face_target_t target = resolve_face_target_under_cursor(ctx);
    if (!target.brush)
      return;

    const shared::face_surface_t *sampled =
        shared::find_face_surface(*target.brush, target.plane);
    face_clipboard = sampled ? *sampled : shared::face_surface_t{};
    if (!sampled)
      face_clipboard->uv = shared::default_face_uv(target.plane.normal);
    return;
  }

  // Apply it to the face under the cursor, keeping that face's own key and its
  // own projection axes -- world-space axes copied onto a perpendicular face
  // stretch the material to infinity along it.
  case input::key_t::V:
  {
    if (!face_clipboard)
      return;

    const face_target_t target = resolve_face_target_under_cursor(ctx);
    edit_face_surface(ctx, target, [this](shared::face_surface_t &face) {
      const linalg::vec3              key_normal   = face.key_normal;
      const float                     key_distance = face.key_distance;
      const shared::face_uv_channel_t existing     = face.uv;

      face              = *face_clipboard;
      face.key_normal   = key_normal;
      face.key_distance = key_distance;
      face.uv.u_axis    = existing.u_axis;
      face.uv.v_axis    = existing.v_axis;
    });
    return;
  }

  default:
    return;
  }
}


// ============================================================================
// Per-face surfaces
//
// A face's identity is its PLANE (geometry_def.md ss6), so every operation here
// resolves a plane and writes through face_surface_for -- never through an index
// into the derived face list, which means nothing across an edit.
// ============================================================================

// Hover first: texturing is a sweep, so requiring a select click before every
// assignment doubles the input for no decision.
Brush_Tool::face_target_t Brush_Tool::resolve_face_target_under_cursor(editor_context_t &ctx)
{
  if (hover.uid != shared::invalid_entity_uid)
    return resolve_face_target(ctx, hover.uid, hover.face_normal);

  return resolve_face_target(ctx, selection.uid, selection.face_normal);
}

// What the PANEL reads and writes -- never the hover, or every value in it is
// rewritten by whatever the cursor crossed on its way to the widget.
Brush_Tool::face_target_t Brush_Tool::resolve_selected_face_target(editor_context_t &ctx)
{
  return resolve_face_target(ctx, selection.uid, selection.face_normal);
}

Brush_Tool::face_target_t
Brush_Tool::resolve_face_target(editor_context_t &ctx, shared::entity_uid_t uid,
                                const std::optional<linalg::vec3> &face_normal)
{
  face_target_t target;
  if (uid == shared::invalid_entity_uid || !face_normal || !ctx.map)
    return target;

  shared::map_geometry_t *entry = ctx.map->find_geometry_by_uid(uid);
  if (!entry)
    return target;

  shared::brush_geometry_t *brush = std::get_if<shared::brush_geometry_t>(&entry->value);
  if (!brush)
    return target;

  // The plane has to come from the CURRENT hull rather than from the normal
  // alone: two parallel faces of one brush share a normal and are told apart by
  // distance, and a normal carries none.
  const std::optional<shared::brush_polyhedron_t> hull =
      shared::try_build_brush_polyhedron(brush->vertices);
  if (!hull)
    return target;

  const shared::brush_face_t *best = nullptr;
  float best_dot = FACE_NORMAL_MATCH;
  for (const shared::brush_face_t &face : hull->faces)
  {
    const float dot = linalg::dot(face.plane.normal, *face_normal);
    if (dot <= best_dot)
      continue;
    best     = &face;
    best_dot = dot;
  }

  if (!best)
    return target;

  target.uid   = uid;
  target.brush = brush;
  target.plane = best->plane;
  return target;
}

void Brush_Tool::edit_face_surface(
    editor_context_t &ctx, const face_target_t &target,
    const std::function<void(shared::face_surface_t &)> &edit)
{
  if (!target.brush)
    return;

  const shared::geometry_value_t before = *target.brush;
  edit(shared::face_surface_for(*target.brush, target.plane));

  // A no-op edit pushes no transaction (add_geometry_modified checks), so
  // rebuilding for one is wasted work AND a wasted GPU stall -- update_mesh
  // waits on the device.
  if (shared::geometry_values_equal(before, *target.brush))
    return;

  refresh_generated_geometry_mesh(*target.brush, target.uid, ctx.map->materials);

  // Subdividing REPLACES the handle set with the new grid, so the tool's
  // copy has to follow the edit rather than the next click.
  if (target.uid == selection.uid)
    rebuild_hull_and_handles(ctx);

  if (ctx.transaction_system)
  {
    transaction_t transaction;
    transaction.add_geometry_modified(target.uid, before, *target.brush);
    ctx.transaction_system->push(std::move(transaction));
  }
}

// ============================================================================
// Vertex paint
//
// A stroke writes WEIGHTS into the face grid and moves no vertex, so it costs
// no hull rebuild -- the same property that makes a grid drag cheap. The
// welding is shared::paint_brush_grid_blend's job, not the tool's.
// ============================================================================

void Brush_Tool::update_paint_cursor(editor_context_t &ctx, float dt)
{
  paint.cursor.reset();

  if (mode != Mode::Paint || !ctx.map)
    return;

  shared::brush_geometry_t *brush = try_get_selected_brush(ctx);
  if (!brush)
    return;

  const std::optional<shared::brush_grid_hit_t> hit = shared::try_pick_brush_grid(
      *brush, cached_view.mouse_ray.origin, cached_view.mouse_ray.direction);
  if (hit)
  {
    paint.cursor = hit->position;
    paint.cursor_normal = hit->normal;
  }

  if (!paint.stroking || !paint.cursor)
    return;

  // Off dt rather than per mouse event: a stroke is a rate, so holding still
  // keeps building up and a fast sweep lays down the same weight per unit of
  // travel whatever the frame rate is.
  if (shared::paint_brush_grid_blend(*brush, *paint.cursor, paint.radius,
                                     paint.strength * dt, paint.layer) == 0)
    return;

  refresh_generated_geometry_mesh(*brush, selection.uid, ctx.map->materials);
}

void Brush_Tool::end_paint_stroke(editor_context_t &ctx)
{
  const std::optional<shared::geometry_value_t> before =
      std::move(paint.geometry_at_the_start_of_stroke);
  paint.geometry_at_the_start_of_stroke.reset();
  paint.stroking = false;

  if (!before || !ctx.transaction_system)
    return;

  shared::brush_geometry_t *brush = try_get_selected_brush(ctx);
  if (!brush)
  {
    log_error("brush tool: brush {} vanished mid-stroke - the paint is not undoable",
              selection.uid);
    return;
  }

  // The whole stroke is one entry, like a drag: an undo per frame of painting
  // would be unusable.
  if (shared::geometry_values_equal(*before, shared::geometry_value_t{*brush}))
    return;

  transaction_t transaction;
  transaction.add_geometry_modified(selection.uid, *before, *brush);
  ctx.transaction_system->push(std::move(transaction));
}

void Brush_Tool::draw_paint_ui(editor_context_t &ctx)
{
  if (mode != Mode::Paint)
    return;

  ImGui::SeparatorText("Vertex paint");

  shared::brush_geometry_t *brush = try_get_selected_brush(ctx);
  if (!brush)
  {
    ImGui::TextDisabled("click a brush to paint on it");
    return;
  }

  ImGui::DragFloat("radius", &paint.radius, 1.0f, 1.0f, 2048.0f, "%.0f");
  ImGui::DragFloat("strength", &paint.strength, 0.05f, 0.05f, 8.0f, "%.2f / sec");

  // The TARGET layer, not a sign: painting toward the base is the eraser. It is
  // a loop rather than two buttons so a third layer needs nothing here.
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

  const face_target_t target = resolve_selected_face_target(ctx);
  const shared::face_surface_t *current =
      target.brush ? shared::find_face_surface(*target.brush, target.plane) : nullptr;

  if (!current || !shared::face_is_subdivided(*current))
    ImGui::TextColored(ImVec4(1, 0.8f, 0.4f, 1),
                       "this face has no grid - subdivide it above to paint on it");
  else if (!shared::face_is_blended(*current))
    ImGui::TextColored(ImVec4(1, 0.8f, 0.4f, 1),
                       "both layers name one material - nothing will look different");

  if (!paint.cursor)
    ImGui::TextDisabled("cursor is off the brush");
}

void Brush_Tool::draw_material_ui(editor_context_t &ctx)
{
  if (!ctx.map)
    return;

  const face_target_t target = resolve_selected_face_target(ctx);

  ImGui::SeparatorText("Face material");

  if (!target.brush)
  {
    ImGui::TextDisabled("click a brush face — this panel follows the SELECTION, "
                        "not the cursor");
    return;
  }

  const shared::face_surface_t* current =
      shared::find_face_surface(*target.brush, target.plane);

  ImGui::Text("brush %u  face %.2f %.2f %.2f", target.uid, target.plane.normal.x,
              target.plane.normal.y, target.plane.normal.z);

  // The table is browsed, never typed into by index: a face holds an index and
  // an author holds a path, and this is the one place the two meet.
  const auto label_for = [&ctx](uint16_t index) -> const char * {
    if (index >= ctx.map->materials.size())
      return "<out of range>";
    return ctx.map->materials[index].empty() ? "<untextured>"
                                             : ctx.map->materials[index].c_str();
  };

  // One combo per LAYER, not one for the material and a second bolted on for
  // the blend: a third layer is then another row of the same loop.
  for (int layer = 0; layer < BLEND_LAYER_COUNT; ++layer)
  {
    const uint16_t material_index =
        current ? shared::face_layer_material(*current, layer) : 0;

    char label[64];
    if (layer == 0)
      std::snprintf(label, sizeof(label), "material");
    else
      std::snprintf(label, sizeof(label), "blend material %d", layer);

    if (!ImGui::BeginCombo(label, label_for(material_index)))
      continue;

    for (uint16_t index = 0; index < (uint16_t)ctx.map->materials.size(); ++index)
    {
      const bool selected_here = index == material_index;
      if (ImGui::Selectable(label_for(index), selected_here))
        edit_face_surface(ctx, target, [index, layer](shared::face_surface_t &face) {
          shared::set_face_layer_material(face, layer, index);
        });
      if (selected_here)
        ImGui::SetItemDefaultFocus();
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
    edit_face_surface(ctx, target, [added, assign_layer](shared::face_surface_t &face) {
      shared::set_face_layer_material(face, assign_layer, added);
    });
    material_path_input[0] = '\0';
  }

  bool emits_geometry = current ? current->emits_geometry : true;
  if (ImGui::Checkbox("draws (off is nodraw)", &emits_geometry))
    edit_face_surface(ctx, target, [emits_geometry](shared::face_surface_t &face) {
      face.emits_geometry = emits_geometry;
    });

  const shared::face_uv_channel_t uv =
      current ? current->uv : shared::default_face_uv(target.plane.normal);

  float scale[2] = {uv.u_scale, uv.v_scale};
  if (ImGui::DragFloat2("scale (units per repeat)", scale, 1.0f, 1.0f, 4096.0f, "%.1f"))
    edit_face_surface(ctx, target, [scale](shared::face_surface_t &face) {
      face.uv.u_scale = scale[0];
      face.uv.v_scale = scale[1];
    });

  float shift[2] = {uv.u_shift, uv.v_shift};
  if (ImGui::DragFloat2("shift", shift, 1.0f, 0.0f, 0.0f, "%.1f"))
    edit_face_surface(ctx, target, [shift](shared::face_surface_t &face) {
      face.uv.u_shift = shift[0];
      face.uv.v_shift = shift[1];
    });

  if (ImGui::Button("align to face"))
    edit_face_surface(ctx, target, [&target](shared::face_surface_t &face) {
      face.uv = shared::default_face_uv(target.plane.normal);
    });

  ImGui::Separator();

  // TESSELLATION, and what a displacement became. 0 is flat, which is every face
  // that predates Track D; anything above it makes the face's handles in Vertex
  // mode its grid, and a drag there writes an offset rather than moving a point
  // of the brush.
  int subdivision_level = current ? current->subdivision_level : 0;
  if (ImGui::SliderInt("subdivision", &subdivision_level, 0, 32))
    edit_face_surface(ctx, target, [subdivision_level](shared::face_surface_t &face) {
      // Resamples rather than flattening, so dragging the slider does not throw
      // away what has been sculpted.
      shared::resize_face_grid(face, subdivision_level);
    });

  if (current && shared::face_is_subdivided(*current))
  {
    const int size = shared::face_grid_size(current->subdivision_level);
    ImGui::TextDisabled("%d x %d grid, %zu vertices — Vertex mode drags them", size,
                        size, current->offsets.size());

    if (ImGui::Button("flatten"))
      edit_face_surface(ctx, target, [](shared::face_surface_t &face) {
        for (linalg::vec3 &offset : face.offsets)
          offset = {0, 0, 0};
      });
  }
  else if (subdivision_level > 0)
  {
    // The one shape a grid cannot sit on, said where the author is looking at it
    // rather than only in the log.
    ImGui::TextDisabled("this face is not a quad — a grid is a patch over four "
                        "corners");
  }

  ImGui::SameLine();
  if (ImGui::Button("apply to whole brush") && current)
  {
    const shared::geometry_value_t before  = *target.brush;
    const shared::face_surface_t   pattern = *current;

    shared::sync_face_surfaces(*target.brush);
    for (shared::face_surface_t &face : target.brush->face_surfaces)
    {
      const linalg::vec3 key_normal   = face.key_normal;
      const float        key_distance = face.key_distance;
      face                            = pattern;
      face.key_normal                 = key_normal;
      face.key_distance               = key_distance;

      // The axes are world-space, so copying them wholesale would project the
      // material along the source face's plane onto every other one -- which on
      // a perpendicular face is an infinite stretch. Each face keeps its own
      // projection and takes only what the author chose.
      const shared::face_uv_channel_t own = shared::default_face_uv(key_normal);
      face.uv.u_axis                      = own.u_axis;
      face.uv.v_axis                      = own.v_axis;
    }

    refresh_generated_geometry_mesh(*target.brush, target.uid, ctx.map->materials);
    if (ctx.transaction_system)
    {
      transaction_t transaction;
      transaction.add_geometry_modified(target.uid, before, *target.brush);
      ctx.transaction_system->push(std::move(transaction));
    }
  }

  if (current && shared::face_is_blended(*current))
    ImGui::TextDisabled("blended - Paint mode brushes the weights");

  if (face_clipboard)
    ImGui::TextDisabled("clipboard: material %u, scale %.0f x %.0f",
                        face_clipboard->material, face_clipboard->uv.u_scale,
                        face_clipboard->uv.v_scale);
  else
    ImGui::TextDisabled("clipboard: empty");
}

void Brush_Tool::on_draw_overlay(editor_context_t &ctx, pass_builder_t &draws)
{
  // The paint cursor: a ring lying on the DISPLACED surface, which is where the
  // stroke actually measures its radius from.
  if (mode == Mode::Paint && paint.cursor)
  {
    linalg::vec3 tangent_u{0, 0, 0};
    linalg::vec3 tangent_v{0, 0, 0};
    shared::brush_face_grid_tangents(paint.cursor_normal, tangent_u, tangent_v);

    constexpr int SEGMENTS = 32;
    linalg::vec3 previous{0, 0, 0};
    for (int step = 0; step <= SEGMENTS; ++step)
    {
      const float angle = (float)step * (6.2831853f / (float)SEGMENTS);
      const linalg::vec3 point = *paint.cursor +
                                 tangent_u * (std::cos(angle) * paint.radius) +
                                 tangent_v * (std::sin(angle) * paint.radius);
      if (step > 0)
        draws.debug.line(previous, point, colors::cyan, -2.0f);
      previous = point;
    }
  }

  // hovering over a valid thing.
  if (hover.uid != shared::invalid_entity_uid && hover.uid != selection.uid)
  {
    // it's a geometrical thing.
    if (const shared::map_geometry_t* entry =
            ctx.map->find_geometry_by_uid(hover.uid))
    {
      // it's a brush.
      if (const shared::brush_geometry_t* brush =
              std::get_if<shared::brush_geometry_t>(&entry->value))
      {
        // build the polyhedron.
        std::optional<shared::brush_polyhedron_t> outline =
            shared::try_build_brush_polyhedron(brush->vertices);

        // if that succeeded, for all faces, draw the faces.
        if (outline)
        {
          for (const shared::brush_face_t &face : outline->faces)
          {
            for (size_t i = 0; i < face.vertex_indices.size(); ++i)
            {
              draws.debug.line(
                  outline->vertices[face.vertex_indices[i]],
                  outline->vertices[face.vertex_indices
                                        [(i + 1) % face.vertex_indices.size()]],
                  colors::white, OVERLAY_DEPTH_BIAS);
            }
          }
        }
      }
    }
  }

  if (!selection_geometry.hull)
  {
    log_warning("selection_geometry has no hull?");
    return;
  }

  // selecting a valid thing.
  for (const shared::brush_face_t &face : selection_geometry.hull->faces)
  {
    for (size_t i = 0; i < face.vertex_indices.size(); ++i)
    {
      draws.debug.line(
          selection_geometry.hull->vertices[face.vertex_indices[i]],
          selection_geometry.hull->vertices
              [face.vertex_indices[(i + 1) % face.vertex_indices.size()]],
          colors::yellow, OVERLAY_DEPTH_BIAS);
    }
  }

  // highlight the selected face.
  if (selection_geometry.face_idx >= 0)
  {
    const shared::brush_face_t &face =
        selection_geometry.hull->faces[(size_t)selection_geometry.face_idx];

    const shared::brush_geometry_t *brush = try_get_selected_brush(ctx);
    const shared::face_surface_t *surface =
        brush ? shared::find_face_surface(*brush, face.plane) : nullptr;
    const int subdivision_level = surface ? surface->subdivision_level : 0;

    bool drew_grid = false;

    // A subdivided face's surface IS its grid, so filling the base polygon
    // would shade the one place the face is not.
    if (brush && subdivision_level > 0)
    {
      const shared::brush_face_grids_t grids =
          shared::build_brush_face_grids(*brush, *selection_geometry.hull);
      const std::vector<linalg::vec3> &grid =
          grids.grid_vertices[(size_t)selection_geometry.face_idx];

      const int size = shared::face_grid_size(subdivision_level);
      if ((int)grid.size() == size * size)
      {
        for (int v = 0; v + 1 < size; ++v)
        {
          for (int u = 0; u + 1 < size; ++u)
          {
            const linalg::vec3f cell[4] = {grid[(size_t)(v * size + u)],
                                           grid[(size_t)(v * size + u + 1)],
                                           grid[(size_t)((v + 1) * size + u + 1)],
                                           grid[(size_t)((v + 1) * size + u)]};
            draws.debug.filled_polygon(Span<const linalg::vec3f>(cell, 4),
                                       SELECTED_FACE_FILL, 0.0f,
                                       {.depth_bias = OVERLAY_DEPTH_BIAS});
          }
        }

        for (int v = 0; v < size; ++v)
        {
          for (int u = 0; u < size; ++u)
          {
            const linalg::vec3 &point = grid[(size_t)(v * size + u)];
            if (u + 1 < size)
              draws.debug.line(point, grid[(size_t)(v * size + u + 1)],
                               SELECTED_FACE_GRID, OVERLAY_DEPTH_BIAS);
            if (v + 1 < size)
              draws.debug.line(point, grid[(size_t)((v + 1) * size + u)],
                               SELECTED_FACE_GRID, OVERLAY_DEPTH_BIAS);
          }
        }

        drew_grid = true;
      }
    }

    if (!drew_grid)
    {
      std::vector<linalg::vec3f> polygon;
      polygon.reserve(face.vertex_indices.size());
      for (uint32_t index : face.vertex_indices)
        polygon.push_back(selection_geometry.hull->vertices[index]);

      draws.debug.filled_polygon(polygon, SELECTED_FACE_FILL, 0.0f,
                                 {.depth_bias = OVERLAY_DEPTH_BIAS});
    }

    const linalg::vec3 centroid = face.plane.point;
    draws.debug.arrow(centroid, centroid + face.plane.normal * 32.0f,
                      colors::yellow);
  }

  // if there's an extrusion, draw it transparent.
  if (extrusion.pending)
  {
    const pending_extrusion_solids_t pending =
        build_pending_extrusion_solids(ctx.grid ? ctx.grid->step() : 0.0f);

    std::vector<linalg::vec3> previewed_vertices;

    // there can be multiple pending solids (think the L shape which ocntains of two rectangles.)
    for (const std::vector<linalg::vec3>& points : pending.point_sets)
    {
      std::optional<shared::brush_polyhedron_t> preview = shared::try_build_brush_polyhedron(points);

      if (!preview)
      {
        // preview failed: tell me why. draw at least the bounds of the shape.
        const shared::aabb_bounds_t bounds = shared::compute_brush_bounds(points);
        const linalg::vec3          middle = (bounds.min + bounds.max) * 0.5f;

        draws.debug.box(middle, (bounds.max - bounds.min) * 0.5f, colors::red,
                        renderer::fill_mode_t::wireframe, OVERLAY_DEPTH_BIAS);
        draws.debug.text(middle, explain_unbuildable_footprint(points), colors::red);
        continue;
      }

      auto nice_shade_of_blue_for_extrusion_preview =  color_t{80, 200, 255, 70};
      for (const shared::brush_face_t& face : preview->faces)
      {
        auto polygon = std::vector<linalg::vec3f>{};
        polygon.reserve(face.vertex_indices.size());

        for (uint32_t index : face.vertex_indices)
        {
          polygon.push_back(preview->vertices[index]);
        }
        
        draws.debug.filled_polygon(polygon,nice_shade_of_blue_for_extrusion_preview);

        // draw the borders.
        for (size_t i = 0; i < face.vertex_indices.size(); ++i)
        {
          draws.debug.line(
              preview->vertices[face.vertex_indices[i]],
              preview->vertices[face.vertex_indices[(i + 1) % face.vertex_indices.size()]],
              colors::cyan, OVERLAY_DEPTH_BIAS);
        }
      }

      for (const linalg::vec3& vertex : preview->vertices)
      {
        previewed_vertices.push_back(vertex);
      }
    }

    if (!previewed_vertices.empty())
    {
      const shared::aabb_bounds_t bounds =
          shared::compute_brush_bounds(previewed_vertices);
      const linalg::vec3 size = bounds.max - bounds.min;
      const linalg::vec3 middle = (bounds.min + bounds.max) * 0.5f;

      char label[96];
      if (pending.point_sets.size() > 1)
        std::snprintf(label, sizeof(label),
                      "X %.0f  Y %.0f  Z %.0f  (%zu brushes)", size.x, size.y,
                      size.z, pending.point_sets.size());
      else
        std::snprintf(label, sizeof(label), "X %.0f  Y %.0f  Z %.0f", size.x,
                      size.y, size.z);
      draws.debug.text(middle, label, colors::cyan);
    }
  }
  // we're dragging a face.
  else if (drag.kind == Drag::Face && selection_geometry.face_idx >= 0)
  {
    const shared::aabb_bounds_t bounds =
        shared::compute_brush_bounds(selection_geometry.hull->vertices);
    const linalg::vec3 size = bounds.max - bounds.min;

    char label[64];
    std::snprintf(label, sizeof(label), "X %.0f  Y %.0f  Z %.0f", size.x,
                  size.y, size.z);
    draws.debug.text((bounds.min + bounds.max) * 0.5f, label, colors::yellow);
  }
}

void Brush_Tool::on_draw_ui(editor_context_t &ctx)
{
  // a convoluted reason to draw the vertex selection discs here is that the debug mode draws in world space.
  // this thing draws in screen space. sounds like a crutch.

   // we have a face selected.so draw all those points.
  if (mode == Mode::Vertex && selection_geometry.face_idx >= 0)
  {
    ImDrawList *list = ImGui::GetBackgroundDrawList();

    const ImU32 selected_fill = IM_COL32(255, 190, 60, 255);
    const ImU32 plain_fill = IM_COL32(40, 40, 48, 220);
    const ImU32 outline = IM_COL32(255, 255, 255, 230);

    for (const linalg::vec3& point : selection_geometry.vertex_handles)
    {
      const std::optional<linalg::vec2> vertex_position_in_screen_space =
          try_project_to_screen(cached_view, point);
      if (!vertex_position_in_screen_space)
        continue;

      const bool is_selected = point_is_selected(point);
      const float radius = is_selected ? CORNER_HANDLE_RADIUS : HANDLE_RADIUS;

      list->AddCircleFilled(
        {vertex_position_in_screen_space->x, vertex_position_in_screen_space->y},
         radius,
        is_selected ? selected_fill : plain_fill);
      list->AddCircle(
        {vertex_position_in_screen_space->x, vertex_position_in_screen_space->y},
         radius,
         outline,
          0, 1.5f);
    }

    // draw the drag overlay.
    if (drag.kind == Drag::Band_Sizing)
    {
      list->AddRect({drag.band.start.x, drag.band.start.y},
                    {drag.band.end.x, drag.band.end.y},
                    IM_COL32(255, 220, 120, 220));
      list->AddRectFilled({drag.band.start.x, drag.band.start.y},
                          {drag.band.end.x, drag.band.end.y},
                          IM_COL32(255, 220, 120, 40));
    }
  }

  ImGui::Begin("Brush Tool");

  // Both drawn unconditionally: || would short-circuit and stop drawing the
  // second button on the frame the first is clicked.
  int mode_index = (mode == Mode::Face) ? 0 : (mode == Mode::Vertex ? 1 : 2);
  const bool chose_face_mode = ImGui::RadioButton("Face", &mode_index, 0);
  const bool chose_vertex_mode = ImGui::RadioButton("Vertex", &mode_index, 1);
  const bool chose_paint_mode = ImGui::RadioButton("Paint", &mode_index, 2);

  if (chose_face_mode || chose_vertex_mode || chose_paint_mode)
  {
    const Mode wanted =
        (mode_index == 0) ? Mode::Face : (mode_index == 1 ? Mode::Vertex : Mode::Paint);
    if (wanted != mode)
    {
      commit_pending_extrusion(ctx);
      end_drag();
      end_paint_stroke(ctx);
      mode = wanted;
      clear_point_selection();
    }
  }

  ImGui::Separator();

  if (selection.uid == shared::invalid_entity_uid)
    ImGui::TextDisabled("no brush selected");
  else if (!selection_geometry.hull)
    ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "brush %u does not hull",
                       selection.uid);
  else
    ImGui::Text("brush %u — %zu vertices, %zu faces", selection.uid,
                selection_geometry.hull->vertices.size(), selection_geometry.hull->faces.size());

  if (mode == Mode::Vertex)
  {
    ImGui::Text("%zu of %zu points selected", selection.points.size(),
                selection_geometry.vertex_handles.size());

    // Worth saying out loud: the handles ARE the editor grid sampled on the
    // face, so at the default 128 a 128-unit brush face carries its four
    // corners and one centre point and nothing else. That reads as a broken
    // grid rather than a coarse one, so the step and the way to change it are
    // on screen.
    ImGui::Text("grid %.0f  ( [ and ] )", ctx.grid ? ctx.grid->step() : 0.0f);

    if (selection_geometry.face_idx >= 0 && selection_geometry.vertex_handles.size() <= 5 &&
        !selection_geometry.handles_are_grid_vertices)
      ImGui::TextColored(ImVec4(1, 0.8f, 0.4f, 1),
                         "few points — lower the grid with [");

    // The two handle regimes do NOT do the same things, so which is live is
    // said here rather than discovered by a shift+drag that does something else.
    if (selection_geometry.handles_are_grid_vertices)
    {
      ImGui::TextColored(ImVec4(0.4f, 0.8f, 1, 1),
                         "subdivided face: the handles are its GRID");
      ImGui::TextColored(ImVec4(1, 0.8f, 0.4f, 1),
                         "a drag writes an offset — shift+drag cannot extrude here");
      ImGui::TextDisabled("set subdivision to 0 below to extrude this face again");
    }
    else if (selection_geometry.face_idx >= 0)
    {
      ImGui::TextDisabled("flat face: the handles are its corners and grid points");
    }
  }

  if (extrusion.pending)
  {
    const pending_extrusion_solids_t pending =
        build_pending_extrusion_solids(ctx.grid ? ctx.grid->step() : 0.0f);

    // Cheap checks only, no hull build: the overlay already builds one per piece
    // per frame and that is the O(n^4) call, so a second pass here would double
    // the cost of the exact footprint this is trying to explain. The overlay is
    // where the remaining reasons get named, against the piece they belong to.
    const char* why_not = nullptr;
    for (const std::vector<linalg::vec3> &points : pending.point_sets)
    {
      why_not = try_explain_unbuildable_footprint(points);
      if (why_not) break;
    }

    if (why_not)
    {
      // Say the commit will do nothing BEFORE Enter is pressed. It used to
      // report a depth either way and then discard the extrusion into a log
      // line, which reads as the tool ignoring the keystroke.
      ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "cannot extrude this footprint");
      ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s", why_not);
      ImGui::TextDisabled("<Esc> cancels");
    }
    else
    {
      ImGui::TextColored(ImVec4(0.4f, 0.8f, 1, 1),
                         "extruding %.0f.  <Enter> commits, <Esc> cancels",
                         extrusion.depth);

      // A concave footprint is not one convex brush, so say how many it is
      // about to become rather than letting the count be a surprise after the
      // commit.
      if (pending.point_sets.size() > 1)
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1, 1),
                           "concave footprint: splits into %zu brushes",
                           pending.point_sets.size());
    }
  }

  draw_material_ui(ctx);
  draw_paint_ui(ctx);

  ImGui::Separator();
  ImGui::TextDisabled("Tab         face / vertex / paint mode");
  ImGui::TextDisabled("drag        move face (Face mode)");
  ImGui::TextDisabled("drag        box-select points (Vertex mode)");
  ImGui::TextDisabled("ctrl+drag   box-select, adding");
  ImGui::TextDisabled("drag handle move the selected points");
  ImGui::TextDisabled("shift+drag  extrude the selected points (flat faces only)");
  ImGui::TextDisabled("ctrl+A      select the whole face grid");
  ImGui::TextDisabled("alt         ignore the grid while dragging");
  ImGui::TextDisabled("arrows      nudge the brush, camera-relative");
  ImGui::TextDisabled("pgup/pgdn   nudge the brush up / down");
  ImGui::TextDisabled("C / V       sample / apply the face material under the cursor");
  ImGui::TextDisabled("drag        paint the target layer (Paint mode)");
  ImGui::TextDisabled("esc         cancel; Tab also clears a stuck drag");

  ImGui::End();
}

} // namespace client
