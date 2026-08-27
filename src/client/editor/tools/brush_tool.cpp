#include "brush_tool.hpp"

#include "../../../shared/collision_detection.hpp"
#include "../../../shared/log.hpp"
#include "../../geometry_renderer.hpp"
#include "imgui.h"
#include "renderer.hpp"

#include <algorithm>
#include <cmath>

namespace client
{

namespace
{

// Generous on purpose: the cost of over-picking a handle is grabbing the wrong
// one, the cost of under-picking it is losing the selection to a rubber band.
constexpr float HANDLE_PICK_RADIUS   = 14.0f; // pixels
constexpr float HANDLE_RADIUS        = 5.0f;  // pixels
constexpr float CORNER_HANDLE_RADIUS = 7.0f;  // pixels

// How far the cursor must travel before a press on empty face area becomes a
// box selection rather than a click.
constexpr float BAND_DRAG_THRESHOLD = 4.0f; // pixels

constexpr float OVERLAY_DEPTH_BIAS = -200.0f;

// Where a ray crosses a convex face, if it does. The polygon is wound
// counter-clockwise seen from outside, so an interior point is on the +normal
// side of every edge.
bool try_ray_face_intersection(const shared::brush_polyhedron_t &hull,
                               const shared::brush_face_t &face,
                               const linalg::vec3 &ray_origin,
                               const linalg::vec3 &ray_direction, float &out_distance)
{
  const float denominator = linalg::dot(face.plane.normal, ray_direction);
  if (denominator > -1e-6f)
    return false; // parallel, or hitting the back of the face

  const float distance =
      linalg::dot(face.plane.normal, face.plane.point - ray_origin) / denominator;
  if (distance < 0.0f)
    return false;

  const linalg::vec3 hit = ray_origin + ray_direction * distance;

  for (size_t i = 0; i < face.vertex_indices.size(); ++i)
  {
    const linalg::vec3 &start = hull.vertices[face.vertex_indices[i]];
    const linalg::vec3 &end =
        hull.vertices[face.vertex_indices[(i + 1) % face.vertex_indices.size()]];

    if (linalg::dot(linalg::cross(end - start, hit - start), face.plane.normal) < -1e-3f)
      return false;
  }

  out_distance = distance;
  return true;
}

bool point_is_inside_face(const shared::brush_polyhedron_t &hull,
                          const shared::brush_face_t &face, const linalg::vec3 &point)
{
  for (size_t i = 0; i < face.vertex_indices.size(); ++i)
  {
    const linalg::vec3 &start = hull.vertices[face.vertex_indices[i]];
    const linalg::vec3 &end =
        hull.vertices[face.vertex_indices[(i + 1) % face.vertex_indices.size()]];

    if (linalg::dot(linalg::cross(end - start, point - start), face.plane.normal) < -1e-3f)
      return false;
  }
  return true;
}

// How far along `axis` the mouse ray currently sits. The closest point between
// the drag line and the view ray -- the standard axis drag, and the reason a
// face keeps following the cursor when you look at it edge-on.
bool try_parameter_along_axis(const linalg::vec3 &anchor, const linalg::vec3 &axis,
                              const linalg::ray_t &ray, float &out_parameter)
{
  const linalg::vec3 between        = anchor - ray.origin;
  const float        axis_dot_ray   = linalg::dot(axis, ray.direction);
  const float        denominator    = 1.0f - axis_dot_ray * axis_dot_ray;

  if (std::abs(denominator) < 1e-5f)
    return false; // looking straight down the axis: no usable projection

  out_parameter = (linalg::dot(between, ray.direction) * axis_dot_ray -
                   linalg::dot(between, axis)) /
                  denominator;
  return true;
}

// How many picks did not survive as corners of the solid they were pulled into.
// Only the hull path can swallow one; the cell path accounts for every pick or
// declines the whole footprint.
size_t count_picks_swallowed_by(const std::vector<linalg::vec3> &picks,
                                const shared::brush_polyhedron_t &solid)
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

// ============================================================================
// Lifecycle
// ============================================================================

void Brush_Tool::on_enable(editor_context_t &ctx)
{
  assert(ctx.map);
  assert(ctx.transaction_system);

  mode = Mode::Face;
  selected_uid = shared::invalid_entity_uid;
  hovered_uid  = shared::invalid_entity_uid;
  selected_face_normal.reset();
  hovered_face_normal.reset();
  hull.reset();
  face_lattice.clear();
  clear_point_selection();
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

// ============================================================================
// Helpers
// ============================================================================

shared::brush_geometry_t *Brush_Tool::try_get_selected_brush(editor_context_t &ctx)
{
  if (selected_uid == shared::invalid_entity_uid || !ctx.map)
    return nullptr;

  shared::map_geometry_t *entry = ctx.map->find_geometry_by_uid(selected_uid);
  if (!entry)
    return nullptr;

  return std::get_if<shared::brush_geometry_t>(&entry->value);
}

void Brush_Tool::clear_point_selection() { selected_points.clear(); }

void Brush_Tool::cancel_in_progress_gestures()
{
  dragging_face     = false;
  dragging_vertices = false;
  band_armed        = false;
  rubber_banding    = false;
  drag_start_geometry.reset();
  vertex_drag_start_points.clear();
}

void Brush_Tool::cancel_pending_extrusion()
{
  pending_extrusion  = false;
  dragging_extrusion = false;
  pending_depth      = 0.0f;
}

float Brush_Tool::grid_step_for(const editor_context_t &ctx,
                                const input::modifiers_t &mods) const
{
  // Alt is free movement. The grid is a tool behaviour and this is the site that
  // decides it -- nothing below here knows the grid exists (brush.hpp, rule 3).
  if (mods.alt)
    return 0.0f;

  return ctx.grid ? ctx.grid->step() : 1.0f;
}

bool Brush_Tool::point_is_selected(const linalg::vec3 &point) const
{
  for (const linalg::vec3 &selected : selected_points)
  {
    if (std::abs(point.x - selected.x) <= shared::BRUSH_WELD_EPSILON &&
        std::abs(point.y - selected.y) <= shared::BRUSH_WELD_EPSILON &&
        std::abs(point.z - selected.z) <= shared::BRUSH_WELD_EPSILON)
      return true;
  }
  return false;
}

void Brush_Tool::toggle_point_selection(const linalg::vec3 &point, bool additive)
{
  if (!additive)
  {
    selected_points.assign(1, point);
    return;
  }

  for (size_t i = 0; i < selected_points.size(); ++i)
  {
    const linalg::vec3 &selected = selected_points[i];
    if (std::abs(point.x - selected.x) <= shared::BRUSH_WELD_EPSILON &&
        std::abs(point.y - selected.y) <= shared::BRUSH_WELD_EPSILON &&
        std::abs(point.z - selected.z) <= shared::BRUSH_WELD_EPSILON)
    {
      selected_points.erase(selected_points.begin() + (long)i);
      return;
    }
  }

  selected_points.push_back(point);
}

int Brush_Tool::try_pick_lattice_point(const linalg::vec2 &screen_position) const
{
  int   best_index    = -1;
  float best_distance = HANDLE_PICK_RADIUS;

  for (size_t i = 0; i < face_lattice.size(); ++i)
  {
    const std::optional<linalg::vec2> projected =
        try_project_to_screen(cached_view, face_lattice[i]);
    if (!projected)
      continue;

    const float dx       = projected->x - screen_position.x;
    const float dy       = projected->y - screen_position.y;
    const float distance = std::sqrt(dx * dx + dy * dy);

    if (distance < best_distance)
    {
      best_distance = distance;
      best_index    = (int)i;
    }
  }

  return best_index;
}

void Brush_Tool::refresh_hull_and_lattice(editor_context_t &ctx)
{
  hull.reset();
  selected_face = -1;
  face_lattice.clear();

  const shared::brush_geometry_t *brush = try_get_selected_brush(ctx);
  if (!brush)
    return;

  hull = shared::try_build_brush_polyhedron(brush->vertices);
  if (!hull)
    return;

  if (!selected_face_normal)
    return;

  // Re-find the face by normal. On a convex solid a normal names one face, so
  // this survives every edit that renumbers them.
  float best_alignment = 0.99f;
  for (size_t i = 0; i < hull->faces.size(); ++i)
  {
    const float alignment = linalg::dot(hull->faces[i].plane.normal, *selected_face_normal);
    if (alignment > best_alignment)
    {
      best_alignment = alignment;
      selected_face  = (int)i;
    }
  }

  if (selected_face < 0)
    return;

  // --- the lattice on that face ---------------------------------------------
  const shared::brush_face_t &face = hull->faces[(size_t)selected_face];
  const float                 step = ctx.grid ? ctx.grid->step() : 128.0f;

  // Real corners are always handles, on the grid or not.
  for (uint32_t index : face.vertex_indices)
    face_lattice.push_back(hull->vertices[index]);

  if (step <= 0.0f)
    return;

  linalg::vec3 tangent_u;
  linalg::vec3 tangent_v;
  shared::brush_face_grid_tangents(face.plane.normal, tangent_u, tangent_v);

  float min_u = 0.0f, max_u = 0.0f, min_v = 0.0f, max_v = 0.0f;
  for (size_t i = 0; i < face.vertex_indices.size(); ++i)
  {
    const linalg::vec3 &vertex = hull->vertices[face.vertex_indices[i]];
    const float         u      = linalg::dot(vertex, tangent_u);
    const float         v      = linalg::dot(vertex, tangent_v);

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

  // Walking in world u/v means an axis-aligned face gets exactly the world grid,
  // which is what makes the handles line up with everything else in the editor.
  const float plane_distance = linalg::dot(face.plane.normal, face.plane.point);
  const int   first_u        = (int)std::ceil(min_u / step);
  const int   last_u         = (int)std::floor(max_u / step);
  const int   first_v        = (int)std::ceil(min_v / step);
  const int   last_v         = (int)std::floor(max_v / step);

  // A face spanning a thousand grid cells is a wall of dots nobody can click.
  constexpr int MAX_LATTICE_SPAN = 64;
  if (last_u - first_u > MAX_LATTICE_SPAN || last_v - first_v > MAX_LATTICE_SPAN)
    return;

  for (int iv = first_v; iv <= last_v; ++iv)
  {
    for (int iu = first_u; iu <= last_u; ++iu)
    {
      // Rebuild the world point from its two in-plane coordinates and the plane.
      const linalg::vec3 in_plane =
          tangent_u * ((float)iu * step) + tangent_v * ((float)iv * step);
      const linalg::vec3 point =
          in_plane +
          face.plane.normal * (plane_distance - linalg::dot(face.plane.normal, in_plane));

      if (!point_is_inside_face(*hull, face, point))
        continue;

      bool duplicates_a_corner = false;
      for (uint32_t index : face.vertex_indices)
      {
        const linalg::vec3 &corner = hull->vertices[index];
        if (std::abs(point.x - corner.x) <= shared::BRUSH_WELD_EPSILON &&
            std::abs(point.y - corner.y) <= shared::BRUSH_WELD_EPSILON &&
            std::abs(point.z - corner.z) <= shared::BRUSH_WELD_EPSILON)
          duplicates_a_corner = true;
      }

      if (!duplicates_a_corner)
        face_lattice.push_back(point);
    }
  }
}

bool Brush_Tool::try_apply_vertices(editor_context_t &ctx,
                                    std::vector<linalg::vec3> vertices,
                                    bool push_transaction)
{
  shared::brush_geometry_t *brush = try_get_selected_brush(ctx);
  if (!brush)
    return false;

  std::optional<shared::brush_polyhedron_t> candidate =
      shared::try_build_brush_polyhedron(vertices);
  if (!candidate)
  {
    log_warning("brush_tool: that edit would collapse the brush — leaving it alone");
    return false;
  }

  const shared::geometry_value_t before = *brush;

  // Store the hull CORNERS, not the points that were fed in: the hull already
  // dropped anything that is not a corner, and keeping the rest would leave
  // handles that do nothing.
  brush->vertices = candidate->vertices;

  refresh_generated_geometry_mesh(*brush, selected_uid);

  if (push_transaction && ctx.transaction_system)
  {
    transaction_builder_t builder;
    builder.add_geometry_modified(selected_uid, before, *brush);
    ctx.transaction_system->push(builder.take());
  }

  if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;

  return true;
}

void Brush_Tool::nudge_selected_brush(editor_context_t &ctx, const linalg::vec3 &direction)
{
  shared::brush_geometry_t *brush = try_get_selected_brush(ctx);
  if (!brush)
    return;

  const float step = ctx.grid ? ctx.grid->step() : 0.0f;
  if (step <= 0.0f)
    return;

  const shared::geometry_value_t before = *brush;

  for (linalg::vec3 &vertex : brush->vertices)
    vertex = vertex + direction * step;

  refresh_generated_geometry_mesh(*brush, selected_uid);

  // The footprint names world positions, so it has to travel with the brush or
  // the next gesture acts on where the points used to be.
  for (linalg::vec3 &point : selected_points)
    point = point + direction * step;

  if (ctx.transaction_system)
  {
    transaction_builder_t builder;
    builder.add_geometry_modified(selected_uid, before, *brush);
    ctx.transaction_system->push(builder.take());
  }

  if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;
}

Brush_Tool::pending_extrusion_solids_t
Brush_Tool::build_pending_extrusion_solids(float grid_step) const
{
  if (selected_points.empty() || !selected_face_normal)
    return {};

  std::optional<std::vector<std::vector<linalg::vec3>>> rectangles =
      shared::try_decompose_footprint_into_rectangles(selected_points, *selected_face_normal,
                                                      grid_step);

  if (rectangles)
  {
    pending_extrusion_solids_t pending;
    pending.from_grid_cells = true;
    pending.point_sets.reserve(rectangles->size());

    for (const std::vector<linalg::vec3> &rectangle : *rectangles)
      pending.point_sets.push_back(
          shared::extrude_brush_hull(rectangle, *selected_face_normal, pending_depth));

    return pending;
  }

  return {{shared::extrude_brush_hull(selected_points, *selected_face_normal, pending_depth)},
          false};
}

void Brush_Tool::commit_pending_extrusion(editor_context_t &ctx)
{
  if (!pending_extrusion)
    return;

  const pending_extrusion_solids_t pending =
      build_pending_extrusion_solids(ctx.grid ? ctx.grid->step() : 0.0f);
  const std::vector<linalg::vec3> picks = selected_points;
  cancel_pending_extrusion();

  // Read before the first add_geometry: adding one can move the map's geometry
  // out from under the pointer.
  shared::geometry_surface_t surface;
  if (const shared::brush_geometry_t *source = try_get_selected_brush(ctx))
    surface = source->surface;

  transaction_builder_t builder;
  shared::entity_uid_t  last_created  = shared::invalid_entity_uid;
  size_t                created_count = 0;

  for (const std::vector<linalg::vec3> &points : pending.point_sets)
  {
    std::optional<shared::brush_polyhedron_t> solid =
        shared::try_build_brush_polyhedron(points);
    if (!solid)
      continue;

    if (!pending.from_grid_cells)
    {
      const size_t swallowed = count_picks_swallowed_by(picks, *solid);
      if (swallowed > 0)
        log_warning("brush_tool: {} picked point(s) ended up inside the new brush — a "
                    "brush is convex, so a concave footprint fills in. Pick the whole "
                    "area rather than its outline and it splits into a brush per "
                    "rectangle instead",
                    swallowed);
    }

    shared::brush_geometry_t created;
    created.vertices = solid->vertices;
    created.surface  = surface;

    const shared::entity_uid_t uid = ctx.map->add_geometry(created);
    builder.add_geometry_created(uid, created);

    last_created = uid;
    ++created_count;
  }

  if (created_count == 0)
  {
    log_warning("brush_tool: the pending extrusion is not a solid — discarded");
    return;
  }

  // One transaction however many pieces it took, so the L undoes as the one edit
  // it was.
  ctx.transaction_system->push(builder.take());

  if (ctx.geometry_updated_so_bvh_rebuild_is_needed)
    *ctx.geometry_updated_so_bvh_rebuild_is_needed = true;

  // Follow the thing that was just made, the way every other create does.
  selected_uid = last_created;
  selected_face_normal.reset();
  clear_point_selection();
}

// ============================================================================
// Update
// ============================================================================

void Brush_Tool::on_update(editor_context_t &ctx, const viewport_state_t &view, float dt)
{
  cached_view = view;
  (void)dt;

  if (!dragging_face && !dragging_vertices && !dragging_extrusion && !rubber_banding)
  {
    hovered_uid = shared::invalid_entity_uid;
    hovered_face_normal.reset();

    if (ctx.bvh)
    {
      ray_hit_result_t hit{};
      if (bvh_intersect_ray(*ctx.bvh, view.mouse_ray.origin, view.mouse_ray.direction, hit) &&
          hit.id.type == Collision_Id::Type::Static_Geometry)
      {
        const shared::entity_uid_t uid = hit.id.index;
        if (const shared::map_geometry_t *entry = ctx.map->find_geometry_by_uid(uid))
        {
          if (const shared::brush_geometry_t *brush =
                  std::get_if<shared::brush_geometry_t>(&entry->value))
          {
            hovered_uid = uid;

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
                if (!try_ray_face_intersection(*hovered_hull, face, view.mouse_ray.origin,
                                               view.mouse_ray.direction, distance))
                  continue;

                if (!hovered_face_normal || distance < nearest)
                {
                  nearest             = distance;
                  hovered_face_normal = face.plane.normal;
                }
              }
            }
          }
        }
      }
    }
  }

  refresh_hull_and_lattice(ctx);

  hovered_face = -1;
  if (hull && hovered_uid == selected_uid && hovered_face_normal)
  {
    for (size_t i = 0; i < hull->faces.size(); ++i)
    {
      if (linalg::dot(hull->faces[i].plane.normal, *hovered_face_normal) > 0.99f)
        hovered_face = (int)i;
    }
  }
}

// ============================================================================
// Mouse
// ============================================================================

void Brush_Tool::on_mouse_down(editor_context_t &ctx, const input::mouse_event_t &e)
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
  if (mode == Mode::Vertex && hull && selected_face >= 0 && e.mods.shift)
  {
    const int picked = try_pick_lattice_point(screen_position);

    if (selected_points.empty())
    {
      // Nothing picked yet: shift on a handle extrudes that one handle, and
      // shift on bare face has nothing to act on. Arming a band here is the one
      // thing it must not do.
      if (picked < 0)
        return;

      toggle_point_selection(face_lattice[(size_t)picked], false);
    }

    drag_axis = *selected_face_normal;

    // Anchor on the handle if there is one, otherwise on the middle of the
    // footprint -- the axis projection only needs a point on the drag line.
    if (picked >= 0)
    {
      drag_anchor = face_lattice[(size_t)picked];
    }
    else
    {
      drag_anchor = {0, 0, 0};
      for (const linalg::vec3 &point : selected_points)
        drag_anchor = drag_anchor + point;
      drag_anchor = drag_anchor * (1.0f / (float)selected_points.size());
    }

    float parameter = 0.0f;
    try_parameter_along_axis(drag_anchor, drag_axis, cached_view.mouse_ray, parameter);

    // Resume from the depth already set rather than snapping back to zero, so a
    // second shift-drag on a standing extrusion adjusts it instead of resetting it.
    drag_start_parameter = parameter - pending_depth;

    pending_extrusion  = true;
    dragging_extrusion = true;
    return;
  }

  // A standing extrusion swallows clicks on lattice points: that is how the hull
  // grows. Everything else commits it first.
  if (pending_extrusion)
  {
    const int picked = try_pick_lattice_point(screen_position);
    if (picked >= 0)
    {
      toggle_point_selection(face_lattice[(size_t)picked], true);
      return;
    }

    commit_pending_extrusion(ctx);
    return;
  }

  if (mode == Mode::Vertex && hull && selected_face >= 0)
  {
    const int picked = try_pick_lattice_point(screen_position);
    if (picked >= 0)
    {
      const linalg::vec3 point = face_lattice[(size_t)picked];

      if (point_is_selected(point) && e.mods.ctrl)
      {
        toggle_point_selection(point, true); // ctrl on a picked point drops it
        return;
      }

      if (!point_is_selected(point))
        toggle_point_selection(point, e.mods.ctrl);

      dragging_vertices        = true;
      vertex_drag_start_points = selected_points;
      drag_axis                = *selected_face_normal;
      drag_anchor              = point;
      try_parameter_along_axis(drag_anchor, drag_axis, cached_view.mouse_ray,
                               drag_start_parameter);

      if (shared::brush_geometry_t *brush = try_get_selected_brush(ctx))
        drag_start_geometry = *brush;
      return;
    }

    // ARM a band. It does not become one, and the selection is not touched,
    // until the cursor actually travels.
    //
    // Starting from OFF the brush counts. Requiring the press to land on the
    // selected face meant the natural rubber-band gesture -- begin outside the
    // silhouette, sweep across it -- fell through to the reselect path below and
    // deselected everything instead, which is the box select that "breaks".
    // Pressing on a DIFFERENT brush still selects that brush.
    if (hovered_face == selected_face || hovered_uid == shared::invalid_entity_uid)
    {
      band_armed = true;
      band_start = screen_position;
      band_end   = screen_position;
      band_adds  = e.mods.ctrl;
      return;
    }
  }

  // Nothing tool-specific under the cursor: (re)select whatever is.
  if (hovered_uid == shared::invalid_entity_uid)
  {
    selected_uid = shared::invalid_entity_uid;
    selected_face_normal.reset();
    clear_point_selection();
    return;
  }

  if (hovered_uid != selected_uid)
  {
    selected_uid = hovered_uid;
    clear_point_selection();
  }

  selected_face_normal = hovered_face_normal;

  if (mode == Mode::Face && selected_face_normal)
  {
    refresh_hull_and_lattice(ctx);
    if (selected_face < 0)
      return;

    dragging_face = true;
    drag_axis     = *selected_face_normal;
    drag_anchor   = hull->faces[(size_t)selected_face].plane.point;
    if (!try_parameter_along_axis(drag_anchor, drag_axis, cached_view.mouse_ray,
                                  drag_start_parameter))
      dragging_face = false;

    if (shared::brush_geometry_t *brush = try_get_selected_brush(ctx))
      drag_start_geometry = *brush;
  }
}

void Brush_Tool::on_mouse_drag(editor_context_t &ctx, const input::mouse_event_t &e)
{
  const linalg::vec2 screen_position{(float)e.position.x, (float)e.position.y};
  const float        step = grid_step_for(ctx, e.mods);

  if (band_armed && !rubber_banding)
  {
    const float travelled_x = screen_position.x - band_start.x;
    const float travelled_y = screen_position.y - band_start.y;
    if (std::sqrt(travelled_x * travelled_x + travelled_y * travelled_y) <
        BAND_DRAG_THRESHOLD)
      return;

    rubber_banding = true;
    if (!band_adds)
      clear_point_selection();
  }

  if (rubber_banding)
  {
    band_end = screen_position;

    const float min_x = std::min(band_start.x, band_end.x);
    const float max_x = std::max(band_start.x, band_end.x);
    const float min_y = std::min(band_start.y, band_end.y);
    const float max_y = std::max(band_start.y, band_end.y);

    if (!band_adds)
      clear_point_selection();

    for (const linalg::vec3 &point : face_lattice)
    {
      const std::optional<linalg::vec2> projected =
          try_project_to_screen(cached_view, point);
      if (!projected)
        continue;

      if (projected->x >= min_x && projected->x <= max_x && projected->y >= min_y &&
          projected->y <= max_y && !point_is_selected(point))
        selected_points.push_back(point);
    }
    return;
  }

  float parameter = 0.0f;
  if (!try_parameter_along_axis(drag_anchor, drag_axis, cached_view.mouse_ray, parameter))
    return;

  float travel = parameter - drag_start_parameter;
  if (step > 0.0f)
    travel = std::round(travel / step) * step;

  if (dragging_extrusion)
  {
    pending_depth = travel;
    return;
  }

  if (dragging_face && drag_start_geometry)
  {
    // Move only the vertices that were ON the face when the drag began.
    const shared::brush_geometry_t &start =
        std::get<shared::brush_geometry_t>(*drag_start_geometry);

    const float face_distance = linalg::dot(drag_axis, drag_anchor);

    std::vector<linalg::vec3> moved = start.vertices;
    for (linalg::vec3 &vertex : moved)
    {
      if (std::abs(linalg::dot(drag_axis, vertex) - face_distance) <=
          shared::BRUSH_COPLANAR_EPSILON)
        vertex = vertex + drag_axis * travel;
    }

    try_apply_vertices(ctx, std::move(moved), false);
    return;
  }

  if (dragging_vertices && drag_start_geometry)
  {
    const shared::brush_geometry_t &start =
        std::get<shared::brush_geometry_t>(*drag_start_geometry);

    // The footprint follows the points it names. Without this the handles read
    // as deselected the moment the drag starts, because the lattice is rebuilt
    // at the new positions while the selection still holds the old ones.
    selected_points = vertex_drag_start_points;
    for (linalg::vec3 &point : selected_points)
      point = point + drag_axis * travel;

    std::vector<linalg::vec3> moved = start.vertices;
    for (linalg::vec3 &vertex : moved)
    {
      for (const linalg::vec3 &picked : vertex_drag_start_points)
      {
        if (std::abs(vertex.x - picked.x) <= shared::BRUSH_WELD_EPSILON &&
            std::abs(vertex.y - picked.y) <= shared::BRUSH_WELD_EPSILON &&
            std::abs(vertex.z - picked.z) <= shared::BRUSH_WELD_EPSILON)
        {
          vertex = vertex + drag_axis * travel;
          break;
        }
      }
    }

    // A drag that would collapse the brush holds at the last good state rather
    // than destroying it -- try_apply_vertices refuses and says so.
    try_apply_vertices(ctx, std::move(moved), false);
    return;
  }
}

void Brush_Tool::on_mouse_up(editor_context_t &ctx, const input::mouse_event_t &e)
{
  if (e.button != input::mouse_button_t::Left)
    return;

  // A press that never travelled is a CLICK on empty face area, and that is what
  // clears the selection -- the press itself deliberately did not.
  if (band_armed && !rubber_banding && !band_adds)
    clear_point_selection();

  band_armed     = false;
  rubber_banding = false;

  // Releasing ENDS THE DRAG, not the extrusion: it stands until Enter or Escape
  // so more points can be added to its hull.
  dragging_extrusion = false;

  if ((dragging_face || dragging_vertices) && drag_start_geometry)
  {
    // The drag wrote straight into the map so it could be seen; the transaction
    // is the whole gesture, pushed once here.
    if (shared::brush_geometry_t *brush = try_get_selected_brush(ctx))
    {
      if (ctx.transaction_system)
      {
        transaction_builder_t builder;
        builder.add_geometry_modified(selected_uid, *drag_start_geometry, *brush);
        ctx.transaction_system->push(builder.take());
      }

    }
  }

  dragging_face     = false;
  dragging_vertices = false;
  drag_start_geometry.reset();
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

  case input::key_t::Escape:
    cancel_in_progress_gestures();
    if (pending_extrusion)
    {
      cancel_pending_extrusion();
      return;
    }
    clear_point_selection();
    selected_face_normal.reset();
    return;

  case input::key_t::A:
    // Select the whole face grid, which is the fast path to extruding all of it.
    if (mode == Mode::Vertex && e.mods.ctrl)
      selected_points = face_lattice;
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
  // has contour lines from draw_geometry_in_editor, so repeating them here would
  // be two lines down one edge, fighting over depth bias.
  if (hovered_uid != shared::invalid_entity_uid && hovered_uid != selected_uid)
  {
    if (const shared::map_geometry_t *entry = ctx.map->find_geometry_by_uid(hovered_uid))
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
              draws.debug.line(outline->vertices[face.vertex_indices[i]],
                               outline->vertices[face.vertex_indices[(i + 1) %
                                                                     face.vertex_indices.size()]],
                               colors::white, OVERLAY_DEPTH_BIAS);
            }
          }
        }
      }
    }
  }

  if (!hull)
    return;

  for (const shared::brush_face_t &face : hull->faces)
  {
    for (size_t i = 0; i < face.vertex_indices.size(); ++i)
    {
      draws.debug.line(
          hull->vertices[face.vertex_indices[i]],
          hull->vertices[face.vertex_indices[(i + 1) % face.vertex_indices.size()]],
          colors::yellow, OVERLAY_DEPTH_BIAS);
    }
  }

  // The picked face, tinted.
  if (selected_face >= 0)
  {
    const shared::brush_face_t &face = hull->faces[(size_t)selected_face];

    std::vector<linalg::vec3f> polygon;
    polygon.reserve(face.vertex_indices.size());
    for (uint32_t index : face.vertex_indices)
      polygon.push_back(hull->vertices[index]);

    draws.debug.filled_polygon(polygon, color_t{255, 200, 60, 60});

    const linalg::vec3 centroid = face.plane.point;
    draws.debug.arrow(centroid, centroid + face.plane.normal * 32.0f, colors::yellow);
  }

  // The pending extrusion, translucent until it is committed. Drawn piece by
  // piece from the same function the commit builds from, so a footprint that
  // splits is previewed split rather than as the hull it is not going to be.
  if (pending_extrusion)
  {
    const pending_extrusion_solids_t pending =
        build_pending_extrusion_solids(ctx.grid ? ctx.grid->step() : 0.0f);

    std::vector<linalg::vec3> previewed_vertices;

    for (const std::vector<linalg::vec3> &points : pending.point_sets)
    {
      std::optional<shared::brush_polyhedron_t> preview =
          shared::try_build_brush_polyhedron(points);
      if (!preview)
        continue;

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
              preview
                  ->vertices[face.vertex_indices[(i + 1) % face.vertex_indices.size()]],
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
      const linalg::vec3 size   = bounds.max - bounds.min;
      const linalg::vec3 middle = (bounds.min + bounds.max) * 0.5f;

      char label[96];
      if (pending.point_sets.size() > 1)
        std::snprintf(label, sizeof(label), "X %.0f  Y %.0f  Z %.0f  (%zu brushes)", size.x,
                      size.y, size.z, pending.point_sets.size());
      else
        std::snprintf(label, sizeof(label), "X %.0f  Y %.0f  Z %.0f", size.x, size.y,
                      size.z);
      draws.debug.text(middle, label, colors::cyan);
    }
  }
  else if (dragging_face && selected_face >= 0)
  {
    const shared::aabb_bounds_t bounds = shared::compute_brush_bounds(hull->vertices);
    const linalg::vec3          size   = bounds.max - bounds.min;

    char label[64];
    std::snprintf(label, sizeof(label), "X %.0f  Y %.0f  Z %.0f", size.x, size.y, size.z);
    draws.debug.text((bounds.min + bounds.max) * 0.5f, label, colors::yellow);
  }
}

// ============================================================================
// 2D: the handles, and the panel
// ============================================================================

void Brush_Tool::on_draw_ui(editor_context_t &ctx)
{
  // Circles go through ImGui rather than the debug pipeline: an antialiased disc
  // at a constant pixel size is what a handle has to look like, and the debug
  // pipeline draws world-space lines. The BACKGROUND list puts them over the
  // scene but under the editor panels, which is the right precedence.
  if (mode == Mode::Vertex && selected_face >= 0)
  {
    ImDrawList *list = ImGui::GetBackgroundDrawList();

    const ImU32 selected_fill = IM_COL32(255, 190, 60, 255);
    const ImU32 plain_fill    = IM_COL32(40, 40, 48, 220);
    const ImU32 outline       = IM_COL32(255, 255, 255, 230);

    for (const linalg::vec3 &point : face_lattice)
    {
      const std::optional<linalg::vec2> projected =
          try_project_to_screen(cached_view, point);
      if (!projected)
        continue;

      const bool  picked = point_is_selected(point);
      const float radius = picked ? CORNER_HANDLE_RADIUS : HANDLE_RADIUS;

      list->AddCircleFilled({projected->x, projected->y}, radius,
                            picked ? selected_fill : plain_fill);
      list->AddCircle({projected->x, projected->y}, radius, outline, 0, 1.5f);
    }

    if (rubber_banding)
    {
      list->AddRect({band_start.x, band_start.y}, {band_end.x, band_end.y},
                    IM_COL32(255, 220, 120, 220));
      list->AddRectFilled({band_start.x, band_start.y}, {band_end.x, band_end.y},
                          IM_COL32(255, 220, 120, 40));
    }
  }

  ImGui::Begin("Brush Tool");

  // Both drawn unconditionally: || would short-circuit and stop drawing the
  // second button on the frame the first is clicked.
  int        mode_index = (mode == Mode::Face) ? 0 : 1;
  const bool picked_face   = ImGui::RadioButton("Face", &mode_index, 0);
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

  if (selected_uid == shared::invalid_entity_uid)
    ImGui::TextDisabled("no brush selected");
  else if (!hull)
    ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "brush %u does not hull", selected_uid);
  else
    ImGui::Text("brush %u — %zu vertices, %zu faces", selected_uid, hull->vertices.size(),
                hull->faces.size());

  if (mode == Mode::Vertex)
  {
    ImGui::Text("%zu of %zu grid points picked", selected_points.size(),
                face_lattice.size());

    // Worth saying out loud: the face lattice IS the editor grid, so at the
    // default 128 a 128-unit brush face carries its four corners and one centre
    // point and nothing else. That reads as a broken grid rather than a coarse
    // one, so the step and the way to change it are on screen.
    ImGui::Text("grid %.0f  ( [ and ] )", ctx.grid ? ctx.grid->step() : 0.0f);

    if (selected_face >= 0 && face_lattice.size() <= 5)
      ImGui::TextColored(ImVec4(1, 0.8f, 0.4f, 1), "few points — lower the grid with [");
  }

  if (pending_extrusion)
  {
    const pending_extrusion_solids_t pending =
        build_pending_extrusion_solids(ctx.grid ? ctx.grid->step() : 0.0f);

    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1, 1), "extruding %.0f — Enter commits, Esc cancels",
                       pending_depth);

    // A concave footprint is not one convex brush, so say how many it is about
    // to become rather than letting the count be a surprise after the commit.
    if (pending.point_sets.size() > 1)
      ImGui::TextColored(ImVec4(0.4f, 0.8f, 1, 1), "concave — splits into %zu brushes",
                         pending.point_sets.size());
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
