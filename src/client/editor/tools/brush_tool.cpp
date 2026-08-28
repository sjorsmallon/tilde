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

//@NOTE(SJM): you should probably re-evaluate this later because the intuition is lacking now.
// axis is always normal (since that's the axis you want to draw along)
// and the anchor is like the centroid or a picked point.
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

// How many picks did not survive as corners of the solid they were pulled into.
// Only the hull path can swallow one; the cell path accounts for every pick or
// declines the whole footprint.
// Why a point set will not become a brush, or nullptr when nothing here can
// say. Cheap on purpose -- it counts distinct points and NEVER builds a hull,
// because try_build_brush_polyhedron is O(n^4) in that same count and the
// footprint this most needs to explain is the big one.
//
// try_ and a nullable pointer for the same reason try_get_selected_brush uses
// them: the lookup can come up empty, and an optional over an already-nullable
// pointer would be a second way to spell that.
[[nodiscard]] const char *try_explain_unbuildable_footprint(Span<const linalg::vec3> points)
{
  const size_t distinct = shared::weld_brush_points(points).size();

  if (distinct > shared::MAX_BRUSH_VERTICES)
    return "too many points for one brush -- pick a rectilinear area so it "
           "splits into rectangles, or raise the grid with ]";

  if (distinct < 4)
    return "too few distinct points to enclose anything";

  return nullptr;
}

// The same question, once the hull build has ALREADY come back empty. Every
// remaining way to fail -- fewer than four supporting planes, fewer than four
// corners, fewer than four faces -- means one thing to the person holding the
// mouse.
[[nodiscard]] const char *explain_unbuildable_footprint(Span<const linalg::vec3> points)
{
  if (const char *cheap = try_explain_unbuildable_footprint(points))
    return cheap;

  return "the footprint is flat -- points in a line enclose no volume";
}

size_t count_picks_swallowed_by(const std::vector<linalg::vec3>& picks,
                                const shared::brush_polyhedron_t& solid)
{
  size_t swallowed = 0;

  for (const linalg::vec3 &pick : picks)
  {
    bool kept = false;
    for (const linalg::vec3 &vertex : solid.vertices)
    {
      if (std::abs(pick.x - vertex.x) <= shared::BRUSH_WELD_EPSILON &&
          std::abs(pick.y - vertex.y) <= shared::BRUSH_WELD_EPSILON &&
          std::abs(pick.z - vertex.z) <= shared::BRUSH_WELD_EPSILON)
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
  potential_selection = {};
  selection = {};
  hover = {};
  cancel_pending_extrusion();
  cancel_in_progress_gestures();
}

void Brush_Tool::on_disable(editor_context_t &ctx)
{
  // A pending extrusion is an edit the user asked for and has not taken back,
  // so leaving the tool keeps it rather than dropping it on the floor.
  commit_pending_extrusion(ctx);

  cancel_in_progress_gestures();
}

shared::brush_geometry_t*
Brush_Tool::try_get_selected_brush(editor_context_t &ctx)
{
  if (potential_selection.uid == shared::invalid_entity_uid || !ctx.map)
    return nullptr;

  shared::map_geometry_t* entry = ctx.map->find_geometry_by_uid(potential_selection.uid);
  if (!entry) return nullptr;

  return std::get_if<shared::brush_geometry_t>(&entry->value);
}

void Brush_Tool::clear_point_selection() { potential_selection.points.clear(); }

void Brush_Tool::cancel_in_progress_gestures() { gestures = {}; }

void Brush_Tool::cancel_pending_extrusion() { extrusion = {}; }


float Brush_Tool::grid_step_for(const editor_context_t& ctx,
                                const input::modifiers_t& modifiers) const
{
  if (modifiers.alt) return 0.0f;

  return ctx.grid ? ctx.grid->step() : 1.0f;
}

bool Brush_Tool::point_is_selected(const linalg::vec3 &point) const
{
  for (const linalg::vec3 &selected : potential_selection.points)
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

  // if a point is selected while additive mode is not on, replace whatever was there with this point.
  if (!additive)
  {
    potential_selection.points.assign(1, point);
    return;
  }

  // otherwise, if a point is selected that _was_ already selected, remove it from the selection.
  for (size_t i = 0; i < potential_selection.points.size(); ++i)
  {
    const linalg::vec3& selected = potential_selection.points[i];
    if (std::abs(point.x - selected.x) <= shared::BRUSH_WELD_EPSILON &&
        std::abs(point.y - selected.y) <= shared::BRUSH_WELD_EPSILON &&
        std::abs(point.z - selected.z) <= shared::BRUSH_WELD_EPSILON)
    {
      potential_selection.points.erase(potential_selection.points.begin() + (long)i);
      return;
    }
  }

  // if none of those are true, we must just be adding this point to the selection.
  potential_selection.points.push_back(point);
}


int Brush_Tool::try_pick_vertex_handle(const linalg::vec2 &screen_position) const
{
  int best_index = -1;
  float best_distance = HANDLE_PICK_RADIUS;

  for (size_t idx = 0; idx < selection.vertex_handles.size(); ++idx)
  {
    // where screen position does this point end up?
    const std::optional<linalg::vec2> projected =
        try_project_to_screen(cached_view, selection.vertex_handles[idx]);
    if (!projected)
      continue;

    const float dx = projected->x - screen_position.x;
    const float dy = projected->y - screen_position.y;
    const float distance = std::sqrt(dx * dx + dy * dy);

    if (distance < best_distance)
    {
      best_distance = distance;
      best_index = (int)idx;
    }
  }

  return best_index;
}

void Brush_Tool::resolve_band_press_as_click()
{
  const band_t &band = gestures.band;

  // Empty space: the click clears the whole selection, which is what the old
  // press-time reselect did when it hovered nothing.
  if (band.press_uid == shared::invalid_entity_uid)
  {
    potential_selection.uid = shared::invalid_entity_uid;
    potential_selection.face_normal.reset();
    clear_point_selection();
    return;
  }

  // A face is named by its NORMAL, not its index: the index is renumbered by
  // every edit, which is the same reason rebuild_hull_and_handles re-finds it
  // that way.
  const bool same_brush = band.press_uid == potential_selection.uid;
  const bool same_face =
      same_brush && band.press_face_normal && potential_selection.face_normal &&
      linalg::dot(*band.press_face_normal, *potential_selection.face_normal) > 0.99f;

  if (same_face)
  {
    // Bare area of the face already being edited. Dropping the points is what
    // this click has always meant, and an additive one still means "keep".
    if (!band.adds)
      clear_point_selection();
    return;
  }

  potential_selection.uid         = band.press_uid;
  potential_selection.face_normal = band.press_face_normal;
  clear_point_selection();
}

void Brush_Tool::rebuild_hull_and_handles(editor_context_t &ctx)
{
  selection.hull.reset();
  selection.face = INVALID_FACE;
  selection.vertex_handles.clear();

  const shared::brush_geometry_t* brush = try_get_selected_brush(ctx);
 
  if (!brush) return;

  selection.hull = shared::try_build_brush_polyhedron(brush->vertices);

  if (!selection.hull) return;

  if (!potential_selection.face_normal) return;

  // Re-find the face by normal. On a convex solid a normal names one face, so
  // this survives every edit that renumbers them.
  float best_alignment = 0.99f;
  for (size_t i = 0; i < selection.hull->faces.size(); ++i)
  {
    const float alignment = linalg::dot(selection.hull->faces[i].plane.normal,
                                        *potential_selection.face_normal);
    if (alignment > best_alignment)
    {
      best_alignment = alignment;
      selection.face = (int)i;
    }
  }

  if (selection.face < 0)
    return;

  // Track the face as it tilts: the key is what the LAST rebuild matched.
  potential_selection.face_normal =
      selection.hull->faces[(size_t)selection.face].plane.normal;

  // fetch the handles of the face that we were selecting.
  const shared::brush_face_t& face = selection.hull->faces[(size_t)selection.face];

  const float step = ctx.grid ? ctx.grid->step() : 128.0f;

  // Real corners are always handles, on the grid or not.
  for (uint32_t index : face.vertex_indices)
    selection.vertex_handles.push_back(selection.hull->vertices[index]);

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


  for (size_t i = 0; i < face.vertex_indices.size(); ++i)
  {
    const linalg::vec3& vertex = selection.hull->vertices[face.vertex_indices[i]];
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

  // Walking in world u/v means an axis-aligned face gets exactly the world
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

      if (!point_is_inside_face(*selection.hull, face, point)) continue;

      bool duplicates_a_corner = false;
      for (uint32_t index : face.vertex_indices)
      {
        const linalg::vec3 &corner = selection.hull->vertices[index];
        if (std::abs(point.x - corner.x) <= shared::BRUSH_WELD_EPSILON &&
            std::abs(point.y - corner.y) <= shared::BRUSH_WELD_EPSILON &&
            std::abs(point.z - corner.z) <= shared::BRUSH_WELD_EPSILON)
          duplicates_a_corner = true;
      }

      if (!duplicates_a_corner) selection.vertex_handles.push_back(point);
    }
  }
}

bool Brush_Tool::try_rebuild_selected_brush(editor_context_t &ctx, std::vector<linalg::vec3> vertices)
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

  // Store the hull CORNERS, not the points that were fed in: the hull already
  // dropped anything that is not a corner, and keeping the rest would leave
  // handles that do nothing.
  brush->vertices = candidate->vertices;

  refresh_generated_geometry_mesh(*brush, potential_selection.uid);

  if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;

  return true;
}

// nuding means meaneuver in the x,z plane, I think.
void Brush_Tool::nudge_selected_brush(editor_context_t &ctx,
                                      const linalg::vec3 &direction)
{
  shared::brush_geometry_t* brush = try_get_selected_brush(ctx);
  if (!brush)
    return;

  // nudge by a grid step.
  const float step = ctx.grid ? ctx.grid->step() : 0.0f;
  if (step <= 0.0f) return;

  const shared::geometry_value_t before = *brush;

  // move all vertices in the brush.
  for (linalg::vec3 &vertex : brush->vertices)
    vertex = vertex + direction * step;

  // because the vertices have moved, we need to update the mesh, since they are fully decoupled.
  refresh_generated_geometry_mesh(*brush, potential_selection.uid);

  for (linalg::vec3& point : potential_selection.points)
    point = point + direction * step;

  if (ctx.transaction_system)
  {
    transaction_builder_t builder;
    builder.add_geometry_modified(potential_selection.uid, before, *brush);
    ctx.transaction_system->push(builder.take());
  }

  if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
}

void Brush_Tool::delete_selected_brush(editor_context_t &ctx)
{
  if (!try_get_selected_brush(ctx))
    return;

  const shared::entity_uid_t uid = potential_selection.uid;
  const shared::geometry_value_t removed = ctx.map->find_geometry_by_uid(uid)->value;

  ctx.map->remove_geometry(uid);

  if (ctx.transaction_system)
  {
    transaction_builder_t builder;
    builder.add_geometry_removed(uid, removed);
    ctx.transaction_system->push(builder.take());
  }

  // Everything below is keyed to the brush that no longer exists.
  cancel_in_progress_gestures();
  cancel_pending_extrusion();
  potential_selection = {};
  selection = {};
  hover = {};

  if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
}

Brush_Tool::pending_extrusion_solids_t Brush_Tool::build_pending_extrusion_solids(float grid_step) const
{
  if (potential_selection.points.empty() || !potential_selection.face_normal)
    return {};

  // a brush is by definition convex so a concave footprint (footprint meaning the selected vertices, think like an L-shape) needs one brush per rectangle.
  std::optional<std::vector<std::vector<linalg::vec3>>> rectangles =
      shared::try_decompose_footprint_into_rectangles(
          potential_selection.points, *potential_selection.face_normal, grid_step);
  
  // the footprint decomposed into multiple brushes:
  if (rectangles)
  {
    pending_extrusion_solids_t pending;
    pending.every_pick_accounted_for = true;
    pending.point_sets.reserve(rectangles->size());

    for (const std::vector<linalg::vec3> &rectangle : *rectangles)
      pending.point_sets.push_back(shared::extrude_brush_hull(
          rectangle, *potential_selection.face_normal, extrusion.depth));

    return pending;
  }

  // it's just a single brush.
  return {{shared::extrude_brush_hull(potential_selection.points, *potential_selection.face_normal,
                                      extrusion.depth)},
          false};
}

void Brush_Tool::commit_pending_extrusion(editor_context_t &ctx)
{
  if (!extrusion.pending)
    return;

  const pending_extrusion_solids_t pending =
      build_pending_extrusion_solids(ctx.grid ? ctx.grid->step() : 0.0f);
  const std::vector<linalg::vec3> picks = potential_selection.points;
  cancel_pending_extrusion();

  // Read before the first add_geometry: adding one can move the map's geometry
  // out from under the pointer.
  shared::geometry_surface_t surface;
  if (const shared::brush_geometry_t *source = try_get_selected_brush(ctx))
    surface = source->surface;

  transaction_builder_t builder;
  shared::entity_uid_t last_created = shared::invalid_entity_uid;
  size_t created_count = 0;

  for (const std::vector<linalg::vec3> &points : pending.point_sets)
  {
    std::optional<shared::brush_polyhedron_t> solid =
        shared::try_build_brush_polyhedron(points);
    if (!solid)
      continue;

    if (!pending.every_pick_accounted_for)
    {
      const size_t swallowed = count_picks_swallowed_by(picks, *solid);
      if (swallowed > 0)
        log_warning(
            "brush_tool: {} picked point(s) ended up inside the new brush — a "
            "brush is convex, so a concave footprint fills in. Pick the whole "
            "area rather than its outline and it splits into a brush per "
            "rectangle instead",
            swallowed);
    }

    shared::brush_geometry_t created;
    created.vertices = solid->vertices;
    created.surface = surface;

    const shared::entity_uid_t uid = ctx.map->add_geometry(created);
    builder.add_geometry_created(uid, created);

    last_created = uid;
    ++created_count;
  }

  if (created_count == 0)
  {
    // The console is usually shut, so the banner is what the person pressing
    // Enter actually sees. The log keeps the detail.
    const char *why = pending.point_sets.empty()
                          ? "nothing picked"
                          : explain_unbuildable_footprint(pending.point_sets.front());
    hud::set_announcement("extrusion discarded — not a solid");
    log_warning("brush_tool: the pending extrusion is not a solid — discarded ({})",
                why);
    return;
  }

  // One transaction however many pieces it took, so the L undoes as the one
  // edit it was.
  ctx.transaction_system->push(builder.take());

  if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;

  // Follow the thing that was just made, the way every other create does.
  potential_selection.uid = last_created;
  potential_selection.face_normal.reset();
  clear_point_selection();
}

// ============================================================================
// Update
// ============================================================================

void Brush_Tool::on_update(editor_context_t &ctx, const viewport_state_t &view,
                           float dt)
{
  cached_view = view;
  (void)dt;

  if (!gestures.dragging_face && !gestures.dragging_vertices &&
      !extrusion.dragging && !gestures.band.active)
  {
    hover.uid = shared::invalid_entity_uid;
    hover.face_normal.reset();

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

            // The BVH hits the hull now, so hit.normal already names this
            // face. Refining here anyway is what keeps a camera INSIDE a brush
            // from hovering the entry face behind it: the hull clip reports
            // that face at t=0, this rejects every face it is behind.
            std::optional<shared::brush_polyhedron_t> hovered_hull =
                shared::try_build_brush_polyhedron(brush->vertices);
            if (hovered_hull)
            {
              float nearest = 0.0f;
              for (const shared::brush_face_t &face : hovered_hull->faces)
              {
                float distance = 0.0f;
                if (!try_ray_face_intersection(
                        *hovered_hull, face, view.mouse_ray.origin,
                        view.mouse_ray.direction, distance))
                  continue;

                if (!hover.face_normal || distance < nearest)
                {
                  nearest = distance;
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

  hover.face = -1;
  if (selection.hull && hover.uid == potential_selection.uid && hover.face_normal)
  {
    for (size_t i = 0; i < selection.hull->faces.size(); ++i)
    {
      if (linalg::dot(selection.hull->faces[i].plane.normal,
                      *hover.face_normal) > 0.99f)
        hover.face = (int)i;
    }
  }
}

// ============================================================================
// Mouse
// ============================================================================

void Brush_Tool::on_mouse_down(editor_context_t &ctx,
                               const input::mouse_event_t &e)
{
  if (e.button != input::mouse_button_t::Left)
    return;

  const linalg::vec2 screen_position{(float)e.position.x, (float)e.position.y};

  // SHIFT MEANS EXTRUDE AND NEVER ANYTHING ELSE. It does not need the press to
  // land on a handle, and it can never start a box selection.
  //
  // It used to: shift only reached the extrude path when the press was inside a
  // 14px handle circle, and otherwise fell through to arming an ADDITIVE band.
  // So the same gesture extruded or box-dragged depending on pixels the user
  // could not see, which is exactly as unreliable as it sounds. Additive
  // banding moved to ctrl, which has nothing else to do here.
  if (mode == Mode::Vertex && selection.hull && selection.face >= 0 &&
      e.mods.shift)
  {
    const int picked = try_pick_vertex_handle(screen_position);

    if (potential_selection.points.empty())
    {
      // Nothing picked yet: shift on a handle extrudes that one handle, and
      // shift on bare face has nothing to act on. Arming a band here is the one
      // thing it must not do.
      if (picked < 0)
        return;

      toggle_point_selection(selection.vertex_handles[(size_t)picked], false);
    }

    axis_drag.axis = *potential_selection.face_normal;

    // Anchor on the handle if there is one, otherwise on the middle of the
    // footprint -- the axis projection only needs a point on the drag line.
    if (picked >= 0)
    {
      axis_drag.anchor = selection.vertex_handles[(size_t)picked];
    }
    else
    {
      axis_drag.anchor = {0, 0, 0};
      for (const linalg::vec3 &point : potential_selection.points)
        axis_drag.anchor = axis_drag.anchor + point;
      axis_drag.anchor = axis_drag.anchor * (1.0f / (float)potential_selection.points.size());
    }

    float distance_along_axis = 0.0f;
    try_distance_along_axis(axis_drag.anchor, axis_drag.axis, cached_view.mouse_ray,
                            distance_along_axis);

    // Resume from the depth already set rather than snapping back to zero, so a
    // second shift-drag on a standing extrusion adjusts it instead of resetting
    // it.
    axis_drag.start_distance = distance_along_axis - extrusion.depth;

    extrusion.pending = true;
    extrusion.dragging = true;
    return;
  }

  // A standing extrusion swallows clicks on handles: that is how the
  // hull grows. Everything else commits it first.
  if (extrusion.pending)
  {
    const int picked = try_pick_vertex_handle(screen_position);
    if (picked >= 0)
    {
      toggle_point_selection(selection.vertex_handles[(size_t)picked], true);
      return;
    }

    commit_pending_extrusion(ctx);
    return;
  }

  if (mode == Mode::Vertex && selection.hull && selection.face >= 0)
  {
    const int picked = try_pick_vertex_handle(screen_position);
    if (picked >= 0)
    {
      const linalg::vec3 point = selection.vertex_handles[(size_t)picked];

      if (point_is_selected(point) && e.mods.ctrl)
      {
        toggle_point_selection(point, true); // ctrl on a picked point drops it
        return;
      }

      if (!point_is_selected(point))
        toggle_point_selection(point, e.mods.ctrl);

      gestures.dragging_vertices = true;
      gestures.vertex_start_points = potential_selection.points;
      axis_drag.axis = *potential_selection.face_normal;
      axis_drag.anchor = point;
      try_distance_along_axis(axis_drag.anchor, axis_drag.axis, cached_view.mouse_ray,
                              axis_drag.start_distance);

      if (shared::brush_geometry_t *brush = try_get_selected_brush(ctx))
        gestures.start_geometry = *brush;
      return;
    }

    // ARM a band, FROM ANYWHERE. It does not become one, and nothing is
    // selected or deselected, until the cursor actually travels.
    //
    // Arming from anywhere is the whole point: a rubber band over a face's
    // handles naturally starts beside the face, and while the press decided
    // what it meant, starting there picked whatever was under it instead --
    // so the only legal way to band a face was to begin on that same face.
    // Nothing here can pick a face, so nothing here can pick the wrong one.
    //
    // The cost is that picking a DIFFERENT face moves to the release, which is
    // where resolve_band_press_as_click does it. That is the same
    // arm-on-press / decide-on-release split the point selection above already
    // used, widened from "clear the points" to "what did this press mean".
    gestures.band.armed             = true;
    gestures.band.start             = screen_position;
    gestures.band.end               = screen_position;
    gestures.band.adds              = e.mods.ctrl;
    gestures.band.press_uid         = hover.uid;
    gestures.band.press_face_normal = hover.face_normal;
    return;
  }

  // Nothing tool-specific under the cursor: (re)select whatever is.
  if (hover.uid == shared::invalid_entity_uid)
  {
    potential_selection.uid = shared::invalid_entity_uid;
    potential_selection.face_normal.reset();
    clear_point_selection();
    return;
  }

  if (hover.uid != potential_selection.uid)
  {
    potential_selection.uid = hover.uid;
    clear_point_selection();
  }

  potential_selection.face_normal = hover.face_normal;

  if (mode == Mode::Face && potential_selection.face_normal)
  {
    rebuild_hull_and_handles(ctx);
    if (selection.face < 0)
      return;

    gestures.dragging_face = true;
    axis_drag.axis = *potential_selection.face_normal;
    axis_drag.anchor = selection.hull->faces[(size_t)selection.face].plane.point;
    if (!try_distance_along_axis(axis_drag.anchor, axis_drag.axis, cached_view.mouse_ray,
                                 axis_drag.start_distance))
      gestures.dragging_face = false;

    if (shared::brush_geometry_t *brush = try_get_selected_brush(ctx))
      gestures.start_geometry = *brush;
  }
}

void Brush_Tool::on_mouse_drag(editor_context_t &ctx,
                               const input::mouse_event_t &e)
{
  const linalg::vec2 screen_position{(float)e.position.x, (float)e.position.y};
  const float step = grid_step_for(ctx, e.mods);

  if (gestures.band.armed && !gestures.band.active)
  {
    const float travelled_x = screen_position.x - gestures.band.start.x;
    const float travelled_y = screen_position.y - gestures.band.start.y;
    if (std::sqrt(travelled_x * travelled_x + travelled_y * travelled_y) <
        BAND_DRAG_THRESHOLD)
      return;

    gestures.band.active = true;
    if (!gestures.band.adds)
      clear_point_selection();
  }

  if (gestures.band.active)
  {
    gestures.band.end = screen_position;

    const float min_x = std::min(gestures.band.start.x, gestures.band.end.x);
    const float max_x = std::max(gestures.band.start.x, gestures.band.end.x);
    const float min_y = std::min(gestures.band.start.y, gestures.band.end.y);
    const float max_y = std::max(gestures.band.start.y, gestures.band.end.y);

    if (!gestures.band.adds)
      clear_point_selection();

    for (const linalg::vec3 &point : selection.vertex_handles)
    {
      const std::optional<linalg::vec2> projected =
          try_project_to_screen(cached_view, point);
      if (!projected)
        continue;

      if (projected->x >= min_x && projected->x <= max_x &&
          projected->y >= min_y && projected->y <= max_y &&
          !point_is_selected(point))
        potential_selection.points.push_back(point);
    }
    return;
  }

  float distance_along_axis = 0.0f;
  if (!try_distance_along_axis(axis_drag.anchor, axis_drag.axis, cached_view.mouse_ray,
                               distance_along_axis))
    return;

  float travel = distance_along_axis - axis_drag.start_distance;
  if (step > 0.0f)
    travel = std::round(travel / step) * step;

  if (extrusion.dragging)
  {
    extrusion.depth = travel;
    return;
  }

  if (gestures.dragging_face && gestures.start_geometry)
  {
    // Move only the vertices that were ON the face when the drag began.
    const shared::brush_geometry_t &start =
        std::get<shared::brush_geometry_t>(*gestures.start_geometry);

    const float face_distance = linalg::dot(axis_drag.axis, axis_drag.anchor);

    std::vector<linalg::vec3> moved = start.vertices;
    for (linalg::vec3 &vertex : moved)
    {
      if (std::abs(linalg::dot(axis_drag.axis, vertex) - face_distance) <=
          shared::BRUSH_COPLANAR_EPSILON)
        vertex = vertex + axis_drag.axis * travel;
    }

    try_rebuild_selected_brush(ctx, std::move(moved));
    return;
  }

  if (gestures.dragging_vertices && gestures.start_geometry)
  {
    const shared::brush_geometry_t &start =
        std::get<shared::brush_geometry_t>(*gestures.start_geometry);

    // The footprint follows the points it names. Without this the handles read
    // as deselected the moment the drag starts, because the handles are rebuilt
    // at the new positions while the selection still holds the old ones.
    potential_selection.points = gestures.vertex_start_points;
    for (linalg::vec3 &point : potential_selection.points)
      point = point + axis_drag.axis * travel;

    std::vector<linalg::vec3> moved = start.vertices;
    for (linalg::vec3 &vertex : moved)
    {
      for (const linalg::vec3 &picked : gestures.vertex_start_points)
      {
        if (std::abs(vertex.x - picked.x) <= shared::BRUSH_WELD_EPSILON &&
            std::abs(vertex.y - picked.y) <= shared::BRUSH_WELD_EPSILON &&
            std::abs(vertex.z - picked.z) <= shared::BRUSH_WELD_EPSILON)
        {
          vertex = vertex + axis_drag.axis * travel;
          break;
        }
      }
    }

    // A drag that would collapse the brush holds at the last good state rather
    // than destroying it -- try_rebuild_selected_brush refuses and says so.
    try_rebuild_selected_brush(ctx, std::move(moved));
    return;
  }
}

void Brush_Tool::on_mouse_up(editor_context_t &ctx,
                             const input::mouse_event_t &e)
{
  if (e.button != input::mouse_button_t::Left)
    return;

  // A press that never travelled was a CLICK, and only the release can know
  // that. Everything the press deliberately did not do happens here.
  if (gestures.band.armed && !gestures.band.active)
    resolve_band_press_as_click();

  gestures.band.armed = false;
  gestures.band.active = false;

  // Releasing ENDS THE DRAG, not the extrusion: it stands until Enter or Escape
  // so more points can be added to its hull.
  extrusion.dragging = false;

  if ((gestures.dragging_face || gestures.dragging_vertices) &&
      gestures.start_geometry)
  {
    // The drag wrote straight into the map so it could be seen; the transaction
    // is the whole gesture, pushed once here.
    if (shared::brush_geometry_t *brush = try_get_selected_brush(ctx))
    {
      if (ctx.transaction_system)
      {
        transaction_builder_t builder;
        builder.add_geometry_modified(potential_selection.uid, *gestures.start_geometry,
                                      *brush);
        ctx.transaction_system->push(builder.take());
      }
    }
  }

  gestures.dragging_face = false;
  gestures.dragging_vertices = false;
  gestures.start_geometry.reset();
}

// ============================================================================
// Keyboard
// ============================================================================

void Brush_Tool::on_key_down(editor_context_t &ctx, const key_event_t &e)
{
  // Arrows nudge along the ground, Page Up / Page Down along Y -- the binding
  // every brush editor uses. The arrows are CAMERA-RELATIVE and then snapped to
  // whichever world axis they most point along, so "left" is screen-left from
  // wherever you happen to be orbiting rather than a fixed world direction.
  const linalg::vec3 forward =
      linalg::direction_from_angles(cached_view.camera.yaw, 0.0f);
  const linalg::vec3 right{-forward.z, 0.0f, forward.x};

  auto dominant_axis = [](const linalg::vec3 &direction) -> linalg::vec3
  {
    if (std::abs(direction.x) >= std::abs(direction.z))
      return {direction.x >= 0.0f ? 1.0f : -1.0f, 0, 0};
    return {0, 0, direction.z >= 0.0f ? 1.0f : -1.0f};
  };

  switch (e.key)
  {
  case input::key_t::Arrow_Left:
    nudge_selected_brush(ctx, dominant_axis(right * -1.0f));
    return;
  case input::key_t::Arrow_Right:
    nudge_selected_brush(ctx, dominant_axis(right));
    return;
  case input::key_t::Arrow_Up:
    nudge_selected_brush(ctx, dominant_axis(forward));
    return;
  case input::key_t::Arrow_Down:
    nudge_selected_brush(ctx, dominant_axis(forward * -1.0f));
    return;
  case input::key_t::Page_Up:
    nudge_selected_brush(ctx, {0, 1, 0});
    return;
  case input::key_t::Page_Down:
    nudge_selected_brush(ctx, {0, -1, 0});
    return;

  case input::key_t::Tab:
    // Tab is also the way OUT of a stuck gesture, so it clears them all. A
    // mode switch changes what every subsequent input means; carrying a live
    // band across it is how the tool ends up box-selecting forever.
    commit_pending_extrusion(ctx);
    cancel_in_progress_gestures();
    mode = (mode == Mode::Face) ? Mode::Vertex : Mode::Face;
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

  case input::key_t::Escape:
    cancel_in_progress_gestures();
    if (extrusion.pending)
    {
      cancel_pending_extrusion();
      return;
    }
    clear_point_selection();
    potential_selection.face_normal.reset();
    return;

  case input::key_t::A:
    // Select the whole face grid, which is the fast path to extruding all of
    // it.
    if (mode == Mode::Vertex && e.mods.ctrl)
      potential_selection.points = selection.vertex_handles;
    return;

  default:
    return;
  }
}

// ============================================================================
// Overlay
// ============================================================================

void Brush_Tool::on_draw_overlay(editor_context_t &ctx, pass_builder_t &draws)
{
  // Only the HOVERED brush gets an outline from the tool. Every brush already
  // has contour lines from draw_geometry_in_editor, so repeating them here
  // would be two lines down one edge, fighting over depth bias.
  if (hover.uid != shared::invalid_entity_uid && hover.uid != potential_selection.uid)
  {
    if (const shared::map_geometry_t *entry =
            ctx.map->find_geometry_by_uid(hover.uid))
    {
      if (const shared::brush_geometry_t *brush =
              std::get_if<shared::brush_geometry_t>(&entry->value))
      {
        std::optional<shared::brush_polyhedron_t> outline =
            shared::try_build_brush_polyhedron(brush->vertices);

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

  if (!selection.hull)
    return;

  for (const shared::brush_face_t &face : selection.hull->faces)
  {
    for (size_t i = 0; i < face.vertex_indices.size(); ++i)
    {
      draws.debug.line(
          selection.hull->vertices[face.vertex_indices[i]],
          selection.hull->vertices
              [face.vertex_indices[(i + 1) % face.vertex_indices.size()]],
          colors::yellow, OVERLAY_DEPTH_BIAS);
    }
  }

  // The picked face, tinted.
  if (selection.face >= 0)
  {
    const shared::brush_face_t &face =
        selection.hull->faces[(size_t)selection.face];

    std::vector<linalg::vec3f> polygon;
    polygon.reserve(face.vertex_indices.size());
    for (uint32_t index : face.vertex_indices)
      polygon.push_back(selection.hull->vertices[index]);

    draws.debug.filled_polygon(polygon, color_t{255, 200, 60, 60});

    const linalg::vec3 centroid = face.plane.point;
    draws.debug.arrow(centroid, centroid + face.plane.normal * 32.0f,
                      colors::yellow);
  }

  // The pending extrusion, translucent until it is committed. Drawn piece by
  // piece from the same function the commit builds from, so a footprint that
  // splits is previewed split rather than as the hull it is not going to be.
  if (extrusion.pending)
  {
    const pending_extrusion_solids_t pending =
        build_pending_extrusion_solids(ctx.grid ? ctx.grid->step() : 0.0f);

    std::vector<linalg::vec3> previewed_vertices;

    for (const std::vector<linalg::vec3> &points : pending.point_sets)
    {
      std::optional<shared::brush_polyhedron_t> preview =
          shared::try_build_brush_polyhedron(points);
      if (!preview)
      {
        // The convention draw_brush_wireframe already set for a brush that does
        // not hull: show the bound in RED so the piece is visibly wrong rather
        // than invisible. Skipping it silently is what made a doomed extrusion
        // look like nothing was happening at all, while the panel went on
        // reporting a depth.
        const shared::aabb_bounds_t bounds = shared::compute_brush_bounds(points);
        const linalg::vec3          middle = (bounds.min + bounds.max) * 0.5f;

        draws.debug.box(middle, (bounds.max - bounds.min) * 0.5f, colors::red,
                        renderer::fill_mode_t::wireframe, OVERLAY_DEPTH_BIAS);
        draws.debug.text(middle, explain_unbuildable_footprint(points), colors::red);
        continue;
      }

      for (const shared::brush_face_t &face : preview->faces)
      {
        std::vector<linalg::vec3f> polygon;
        polygon.reserve(face.vertex_indices.size());
        for (uint32_t index : face.vertex_indices)
          polygon.push_back(preview->vertices[index]);

        draws.debug.filled_polygon(polygon, color_t{80, 200, 255, 70});

        for (size_t i = 0; i < face.vertex_indices.size(); ++i)
        {
          draws.debug.line(
              preview->vertices[face.vertex_indices[i]],
              preview->vertices
                  [face.vertex_indices[(i + 1) % face.vertex_indices.size()]],
              colors::cyan, OVERLAY_DEPTH_BIAS);
        }
      }

      for (const linalg::vec3 &vertex : preview->vertices)
        previewed_vertices.push_back(vertex);
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
  else if (gestures.dragging_face && selection.face >= 0)
  {
    const shared::aabb_bounds_t bounds =
        shared::compute_brush_bounds(selection.hull->vertices);
    const linalg::vec3 size = bounds.max - bounds.min;

    char label[64];
    std::snprintf(label, sizeof(label), "X %.0f  Y %.0f  Z %.0f", size.x,
                  size.y, size.z);
    draws.debug.text((bounds.min + bounds.max) * 0.5f, label, colors::yellow);
  }
}

// ============================================================================
// 2D: the handles, and the panel
// ============================================================================

void Brush_Tool::on_draw_ui(editor_context_t &ctx)
{
  // Circles go through ImGui rather than the debug pipeline: an antialiased
  // disc at a constant pixel size is what a handle has to look like, and the
  // debug pipeline draws world-space lines. The BACKGROUND list puts them over
  // the scene but under the editor panels, which is the right precedence.
  if (mode == Mode::Vertex && selection.face >= 0)
  {
    ImDrawList *list = ImGui::GetBackgroundDrawList();

    const ImU32 selected_fill = IM_COL32(255, 190, 60, 255);
    const ImU32 plain_fill = IM_COL32(40, 40, 48, 220);
    const ImU32 outline = IM_COL32(255, 255, 255, 230);

    for (const linalg::vec3 &point : selection.vertex_handles)
    {
      const std::optional<linalg::vec2> projected =
          try_project_to_screen(cached_view, point);
      if (!projected)
        continue;

      const bool picked = point_is_selected(point);
      const float radius = picked ? CORNER_HANDLE_RADIUS : HANDLE_RADIUS;

      list->AddCircleFilled({projected->x, projected->y}, radius,
                            picked ? selected_fill : plain_fill);
      list->AddCircle({projected->x, projected->y}, radius, outline, 0, 1.5f);
    }

    if (gestures.band.active)
    {
      list->AddRect({gestures.band.start.x, gestures.band.start.y},
                    {gestures.band.end.x, gestures.band.end.y},
                    IM_COL32(255, 220, 120, 220));
      list->AddRectFilled({gestures.band.start.x, gestures.band.start.y},
                          {gestures.band.end.x, gestures.band.end.y},
                          IM_COL32(255, 220, 120, 40));
    }
  }

  ImGui::Begin("Brush Tool");

  // Both drawn unconditionally: || would short-circuit and stop drawing the
  // second button on the frame the first is clicked.
  int mode_index = (mode == Mode::Face) ? 0 : 1;
  const bool picked_face = ImGui::RadioButton("Face", &mode_index, 0);
  const bool picked_vertex = ImGui::RadioButton("Vertex", &mode_index, 1);

  if (picked_face || picked_vertex)
  {
    const Mode wanted = (mode_index == 0) ? Mode::Face : Mode::Vertex;
    if (wanted != mode)
    {
      commit_pending_extrusion(ctx);
      cancel_in_progress_gestures();
      mode = wanted;
      clear_point_selection();
    }
  }

  ImGui::Separator();

  if (potential_selection.uid == shared::invalid_entity_uid)
    ImGui::TextDisabled("no brush selected");
  else if (!selection.hull)
    ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "brush %u does not hull",
                       potential_selection.uid);
  else
    ImGui::Text("brush %u — %zu vertices, %zu faces", potential_selection.uid,
                selection.hull->vertices.size(), selection.hull->faces.size());

  if (mode == Mode::Vertex)
  {
    ImGui::Text("%zu of %zu points picked", potential_selection.points.size(),
                selection.vertex_handles.size());

    // Worth saying out loud: the handles ARE the editor grid sampled on the
    // face, so at the default 128 a 128-unit brush face carries its four
    // corners and one centre point and nothing else. That reads as a broken
    // grid rather than a coarse one, so the step and the way to change it are
    // on screen.
    ImGui::Text("grid %.0f  ( [ and ] )", ctx.grid ? ctx.grid->step() : 0.0f);

    if (selection.face >= 0 && selection.vertex_handles.size() <= 5)
      ImGui::TextColored(ImVec4(1, 0.8f, 0.4f, 1),
                         "few points — lower the grid with [");
  }

  if (extrusion.pending)
  {
    const pending_extrusion_solids_t pending =
        build_pending_extrusion_solids(ctx.grid ? ctx.grid->step() : 0.0f);

    // Cheap checks only, no hull build: the overlay already builds one per piece
    // per frame and that is the O(n^4) call, so a second pass here would double
    // the cost of the exact footprint this is trying to explain. The overlay is
    // where the remaining reasons get named, against the piece they belong to.
    const char *why_not = nullptr;
    for (const std::vector<linalg::vec3> &points : pending.point_sets)
    {
      why_not = try_explain_unbuildable_footprint(points);
      if (why_not)
        break;
    }

    if (why_not)
    {
      // Say the commit will do nothing BEFORE Enter is pressed. It used to
      // report a depth either way and then discard the extrusion into a log
      // line, which reads as the tool ignoring the keystroke.
      ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "cannot extrude this footprint");
      ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s", why_not);
      ImGui::TextDisabled("Esc cancels");
    }
    else
    {
      ImGui::TextColored(ImVec4(0.4f, 0.8f, 1, 1),
                         "extruding %.0f — Enter commits, Esc cancels",
                         extrusion.depth);

      // A concave footprint is not one convex brush, so say how many it is
      // about to become rather than letting the count be a surprise after the
      // commit.
      if (pending.point_sets.size() > 1)
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1, 1),
                           "concave — splits into %zu brushes",
                           pending.point_sets.size());
    }
  }

  ImGui::Separator();
  ImGui::TextDisabled("Tab         face / vertex mode");
  ImGui::TextDisabled("drag        move face (Face mode)");
  ImGui::TextDisabled("drag        box-select points (Vertex mode)");
  ImGui::TextDisabled("ctrl+drag   box-select, adding");
  ImGui::TextDisabled("drag handle move the picked points");
  ImGui::TextDisabled("shift+drag  extrude the picked points, anywhere");
  ImGui::TextDisabled("ctrl+A      pick the whole face grid");
  ImGui::TextDisabled("alt         ignore the grid while dragging");
  ImGui::TextDisabled("arrows      nudge the brush, camera-relative");
  ImGui::TextDisabled("pgup/pgdn   nudge the brush up / down");
  ImGui::TextDisabled("esc         cancel; Tab also clears a stuck drag");

  ImGui::End();
}

} // namespace client
