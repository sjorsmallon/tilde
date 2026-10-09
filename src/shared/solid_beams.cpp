#include "solid_beams.hpp"

#include "entities/generated/entities/spot_light_entity_generated.hpp"
#include "entity_system.hpp"
#include "lighting.hpp"
#include "log.hpp"
#include "map_geometry.hpp"
#include "mover_path.hpp"
#include "reveal_light.hpp"
#include "shapes.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace shared
{

namespace
{

// Thinner than this is a sliver, not a piece: a corner this close to a cut is on it. The wire rounds to 1/32.
constexpr float ON_PLANE_TOLERANCE = 0.5f;
// A corner this close to the plane a polygon is clipped by is kept on both sides (convex_decomposition's).
constexpr float SPLIT_EPSILON = 1e-4f;

Plane flipped(const Plane& plane)
{
  return {.point = plane.point, .normal = plane.normal * -1.f};
}

enum class piece_side_t : uint8_t
{
  Inside,
  Outside,
  Split
};

// Where the piece's corners lie against `plane`: all behind it (inside), all in front (outside), or both.
piece_side_t classify_piece_against_plane(const solid_beam_arena_t& arena, uint32_t piece_index, const Plane& plane)
{
  const solid_beam_piece_t& piece       = arena.pieces[piece_index];
  bool                      any_inside  = false;
  bool                      any_outside = false;
  for (uint32_t face = piece.face_first; face < piece.face_first + piece.face_count; ++face)
  {
    const solid_beam_face_t& polygon = arena.faces[face];
    for (uint32_t corner = polygon.corner_first; corner < polygon.corner_first + polygon.corner_count; ++corner)
    {
      const float height = linalg::dot(arena.corners[corner] - plane.point, plane.normal);
      any_inside         = any_inside || height < -ON_PLANE_TOLERANCE;
      any_outside        = any_outside || height > ON_PLANE_TOLERANCE;
    }
  }
  if (any_inside && any_outside)
    return piece_side_t::Split;
  return any_outside ? piece_side_t::Outside : piece_side_t::Inside;
}

// Appends the part of face `face` behind `plane` to the arena's corners, from `corner_first`, and returns
// how many corners it has. Only indices are held across the pushes: the corner array may move.
uint32_t append_face_clipped_behind_plane(solid_beam_arena_t& arena, std::vector<float>& heights, uint32_t face,
                                          const Plane& plane)
{
  const uint32_t first = arena.faces[face].corner_first;
  const uint32_t count = arena.faces[face].corner_count;
  heights.resize(count);
  for (uint32_t index = 0; index < count; ++index)
    heights[index] = linalg::dot(arena.corners[first + index] - plane.point, plane.normal);

  uint32_t written = 0;
  for (uint32_t index = 0; index < count; ++index)
  {
    const uint32_t next          = (index + 1) % count;
    const float    height        = heights[index];
    const float    next_height   = heights[next];
    const bool     crosses       = (height > SPLIT_EPSILON && next_height < -SPLIT_EPSILON) ||
                                   (height < -SPLIT_EPSILON && next_height > SPLIT_EPSILON);
    if (height <= SPLIT_EPSILON)
    {
      const linalg::vec3 corner = arena.corners[first + index];
      arena.corners.push_back(corner);
      ++written;
    }
    if (crosses)
    {
      const linalg::vec3 from     = arena.corners[first + index];
      const linalg::vec3 to       = arena.corners[first + next];
      const float        fraction = height / (height - next_height);
      arena.corners.push_back(from + (to - from) * fraction);
      ++written;
    }
  }
  return written;
}

// The cap a cut leaves: every clipped corner on the plane, once, wound about their centre, appended as
// one more face of the half whose faces start at `face_first`.
void append_cap_face(solid_beam_arena_t& arena, std::vector<linalg::vec3>& points, uint32_t face_first,
                     const Plane& plane)
{
  points.clear();
  for (uint32_t face = face_first; face < arena.faces.size(); ++face)
  {
    const solid_beam_face_t& polygon = arena.faces[face];
    for (uint32_t corner = polygon.corner_first; corner < polygon.corner_first + polygon.corner_count; ++corner)
    {
      const linalg::vec3 candidate = arena.corners[corner];
      if (std::fabs(linalg::dot(candidate - plane.point, plane.normal)) > ON_PLANE_TOLERANCE)
        continue;
      bool known = false;
      for (const linalg::vec3& point : points)
        known = known || linalg::length(point - candidate) < ON_PLANE_TOLERANCE;
      if (!known)
        points.push_back(candidate);
    }
  }
  if (points.size() < 3)
    return;

  linalg::vec3 centre = {0.f, 0.f, 0.f};
  for (const linalg::vec3& point : points)
    centre = centre + point;
  centre = centre * (1.f / static_cast<float>(points.size()));

  const linalg::vec3 u = linalg::normalize(points[0] - centre);
  const linalg::vec3 v = linalg::cross(plane.normal, u);
  std::sort(points.begin(), points.end(), [&](const linalg::vec3& a, const linalg::vec3& b)
            {
              const linalg::vec3 da = a - centre;
              const linalg::vec3 db = b - centre;
              return std::atan2(linalg::dot(da, v), linalg::dot(da, u)) <
                     std::atan2(linalg::dot(db, v), linalg::dot(db, u));
            });

  const uint32_t corner_first = static_cast<uint32_t>(arena.corners.size());
  for (const linalg::vec3& point : points)
    arena.corners.push_back(point);
  arena.faces.push_back({.plane = plane, .corner_first = corner_first, .corner_count = static_cast<uint32_t>(points.size())});
}

// The half of piece `source` behind `cut`, appended as a new piece: each face clipped to the half, the cut
// itself one new face, the source's cut planes plus `cut` when `record_cut`. Nothing when the half is no
// solid. No hull is rebuilt: the half's planes are the faces that survived plus the cut.
std::optional<uint32_t> append_half_behind_plane(solid_beam_arena_t& arena, solid_beam_carve_scratch_t& scratch,
                                                 uint32_t source, const Plane& cut, bool record_cut)
{
  const uint32_t face_first   = static_cast<uint32_t>(arena.faces.size());
  const uint32_t corner_first = static_cast<uint32_t>(arena.corners.size());
  const uint32_t source_face_first = arena.pieces[source].face_first;
  const uint32_t source_face_count = arena.pieces[source].face_count;
  for (uint32_t face = source_face_first; face < source_face_first + source_face_count; ++face)
  {
    const uint32_t clipped_first = static_cast<uint32_t>(arena.corners.size());
    const uint32_t clipped_count = append_face_clipped_behind_plane(arena, scratch.heights, face, cut);
    if (clipped_count < 3)
    {
      arena.corners.resize(clipped_first);
      continue;
    }
    const Plane plane = arena.faces[face].plane;
    arena.faces.push_back({.plane = plane, .corner_first = clipped_first, .corner_count = clipped_count});
  }
  append_cap_face(arena, scratch.cap_points, face_first, cut);
  if (arena.faces.size() - face_first < 4)
  {
    arena.faces.resize(face_first);
    arena.corners.resize(corner_first);
    return std::nullopt;
  }

  const uint32_t cut_first        = static_cast<uint32_t>(arena.cut_planes.size());
  const uint32_t source_cut_first = arena.pieces[source].cut_first;
  const uint32_t source_cut_count = arena.pieces[source].cut_count;
  for (uint32_t index = 0; index < source_cut_count; ++index)
  {
    const Plane inherited = arena.cut_planes[source_cut_first + index];
    arena.cut_planes.push_back(inherited);
  }
  if (record_cut)
    arena.cut_planes.push_back(cut);

  solid_beam_piece_t piece = {.face_first = face_first,
                              .face_count = static_cast<uint32_t>(arena.faces.size()) - face_first,
                              .cut_first  = cut_first,
                              .cut_count  = static_cast<uint32_t>(arena.cut_planes.size()) - cut_first,
                              .bounds     = {arena.corners[corner_first], arena.corners[corner_first]},
                              .live       = true};
  for (uint32_t corner = corner_first; corner < arena.corners.size(); ++corner)
    expand_aabb_to_include_point(piece.bounds, arena.corners[corner]);
  arena.pieces.push_back(piece);
  return static_cast<uint32_t>(arena.pieces.size() - 1);
}

struct split_piece_t
{
  std::optional<uint32_t> behind;
  std::optional<uint32_t> in_front;
};

// Piece `source` cut by one plane into the halves behind it and in front of it, either absent when the plane
// misses it; the source is retired.
split_piece_t split_piece_by_plane(solid_beam_arena_t& arena, solid_beam_carve_scratch_t& scratch, uint32_t source,
                                   const Plane& plane, bool record_cut)
{
  split_piece_t halves;
  halves.behind   = append_half_behind_plane(arena, scratch, source, plane, record_cut);
  halves.in_front = append_half_behind_plane(arena, scratch, source, flipped(plane), record_cut);
  arena.pieces[source].live = false;
  return halves;
}

uint32_t keep_behind_plane(solid_beam_arena_t& arena, solid_beam_carve_scratch_t& scratch, uint32_t piece,
                           const Plane& plane)
{
  if (classify_piece_against_plane(arena, piece, plane) != piece_side_t::Split)
    return piece;
  split_piece_t halves = split_piece_by_plane(arena, scratch, piece, plane, false);
  if (!halves.behind)
    fatal_error("solid beam: a cone plane left nothing of the beam");
  return *halves.behind;
}

// Appends `aabb` as a piece with no cut planes.
uint32_t append_aabb_piece(solid_beam_arena_t& arena, const aabb_t& aabb)
{
  const std::vector<Plane>                     planes   = compute_collision_planes(aabb);
  const std::vector<std::vector<linalg::vec3>> polygons = compute_face_polygons(aabb);
  const uint32_t                               face_first = static_cast<uint32_t>(arena.faces.size());
  for (size_t face = 0; face < planes.size(); ++face)
  {
    const uint32_t corner_first = static_cast<uint32_t>(arena.corners.size());
    for (const linalg::vec3& corner : polygons[face])
      arena.corners.push_back(corner);
    arena.faces.push_back({.plane = planes[face], .corner_first = corner_first, .corner_count = static_cast<uint32_t>(polygons[face].size())});
  }
  arena.pieces.push_back({.face_first = face_first,
                          .face_count = static_cast<uint32_t>(planes.size()),
                          .cut_first  = static_cast<uint32_t>(arena.cut_planes.size()),
                          .cut_count  = 0,
                          .bounds     = get_bounds(aabb),
                          .live       = true});
  return static_cast<uint32_t>(arena.pieces.size() - 1);
}

// Copies piece `source` of `from` into `to` as a new live piece.
uint32_t append_piece_copy(solid_beam_arena_t& to, const solid_beam_arena_t& from, uint32_t source)
{
  const solid_beam_piece_t& piece      = from.pieces[source];
  const uint32_t            face_first = static_cast<uint32_t>(to.faces.size());
  for (uint32_t face = piece.face_first; face < piece.face_first + piece.face_count; ++face)
  {
    const solid_beam_face_t& polygon      = from.faces[face];
    const uint32_t           corner_first = static_cast<uint32_t>(to.corners.size());
    to.corners.insert(to.corners.end(), from.corners.begin() + polygon.corner_first,
                      from.corners.begin() + polygon.corner_first + polygon.corner_count);
    to.faces.push_back({.plane = polygon.plane, .corner_first = corner_first, .corner_count = polygon.corner_count});
  }
  const uint32_t cut_first = static_cast<uint32_t>(to.cut_planes.size());
  to.cut_planes.insert(to.cut_planes.end(), from.cut_planes.begin() + piece.cut_first,
                       from.cut_planes.begin() + piece.cut_first + piece.cut_count);
  to.pieces.push_back({.face_first = face_first,
                       .face_count = piece.face_count,
                       .cut_first  = cut_first,
                       .cut_count  = piece.cut_count,
                       .bounds     = piece.bounds,
                       .live       = true});
  return static_cast<uint32_t>(to.pieces.size() - 1);
}

void clear_arena(solid_beam_arena_t& arena)
{
  arena.faces.clear();
  arena.corners.clear();
  arena.cut_planes.clear();
  arena.pieces.clear();
}

// The box around the cone cut to the reveal cone's planes, the flat cap at `range` (one plane tangent to
// the range sphere, at the axis) and more tangents to that sphere: at the rim through every side's azimuth
// and at half the half-angle between them. Each tangent half-space holds the whole sphere, so what beam.frag
// draws stays inside the solid, and the solid reaches past the sphere by at most one over the cosine of half
// the largest gap between tangent points. Built into `out` as its one live piece, apex at the origin along
// CANONICAL_BEAM_AXIS.
constexpr linalg::vec3f CANONICAL_BEAM_AXIS = {1.f, 0.f, 0.f};

void build_pyramid(solid_beam_arena_t& out, solid_beam_carve_scratch_t& scratch, float range, float half_angle_degrees)
{
  const linalg::vec3f        apex  = {0.f, 0.f, 0.f};
  const linalg::vec3f        axis  = CANONICAL_BEAM_AXIS;
  const axis_frame_t         frame = frame_about_axis(axis);
  const reveal_cone_planes_t cone  = planes_of_reveal_cone({.apex                 = apex,
                                                            .axis                 = axis,
                                                            .range                = range,
                                                            .cosine_of_half_angle = std::cos(linalg::to_radians(half_angle_degrees))});

  solid_beam_arena_t& arena = scratch.arena;
  clear_arena(arena);
  uint32_t piece = append_aabb_piece(arena, {.center = apex, .half_extents = {range, range, range}});
  for (const Plane& side : cone.sides)
    piece = keep_behind_plane(arena, scratch, piece, side);
  piece = keep_behind_plane(arena, scratch, piece, {.point = apex + axis * range, .normal = axis});
  for (uint32_t ring = 1; ring <= 2; ++ring)
  {
    const float polar          = linalg::to_radians(half_angle_degrees) * (static_cast<float>(ring) / 2.f);
    const float azimuth_offset = ring == 1 ? 0.5f : 0.f;
    for (uint32_t side = 0; side < REVEAL_CONE_SIDE_COUNT; ++side)
    {
      const float azimuth = 2.f * linalg::PI * (static_cast<float>(side) + azimuth_offset) /
                            static_cast<float>(REVEAL_CONE_SIDE_COUNT);
      const linalg::vec3f direction = axis * std::cos(polar) +
                                      (frame.right * std::cos(azimuth) + frame.up * std::sin(azimuth)) * std::sin(polar);
      piece = keep_behind_plane(arena, scratch, piece, {.point = apex + direction * range, .normal = direction});
    }
  }
  clear_arena(out);
  append_piece_copy(out, arena, piece);
}

const solid_beam_arena_t& find_or_build_canonical_pyramid(solid_beam_cache_t& cache, float range, float half_angle)
{
  for (const solid_beam_canonical_pyramid_t& pyramid : cache.pyramids)
    if (pyramid.range == range && pyramid.half_angle == half_angle)
      return pyramid.arena;
  cache.pyramids.push_back({.range = range, .half_angle = half_angle});
  build_pyramid(cache.pyramids.back().arena, cache.scratch, range, half_angle);
  return cache.pyramids.back().arena;
}

// The canonical pyramid moved to `apex` along `axis`, appended to `arena` as a live piece. The frame about
// the axis is frame_about_axis's, the one planes_of_reveal_cone turns the volumes' beam clip by, so the
// solid's twelve sides and the volumes' are the same polygon of one cone.
uint32_t append_pyramid_at_pose(solid_beam_arena_t& arena, const solid_beam_arena_t& canonical, const linalg::vec3f& apex,
                                const linalg::vec3f& axis)
{
  const axis_frame_t canonical_frame = frame_about_axis(CANONICAL_BEAM_AXIS);
  const axis_frame_t frame           = frame_about_axis(axis);
  const auto         turn            = [&](const linalg::vec3f& vector) -> linalg::vec3f
  {
    return frame.right * linalg::dot(vector, canonical_frame.right) + frame.up * linalg::dot(vector, canonical_frame.up) +
           axis * linalg::dot(vector, CANONICAL_BEAM_AXIS);
  };
  const uint32_t      piece  = append_piece_copy(arena, canonical, 0);
  solid_beam_piece_t& placed = arena.pieces[piece];
  for (uint32_t face = placed.face_first; face < placed.face_first + placed.face_count; ++face)
  {
    solid_beam_face_t& polygon = arena.faces[face];
    polygon.plane.point        = apex + turn(polygon.plane.point);
    polygon.plane.normal       = turn(polygon.plane.normal);
    for (uint32_t corner = polygon.corner_first; corner < polygon.corner_first + polygon.corner_count; ++corner)
      arena.corners[corner] = apex + turn(arena.corners[corner]);
  }
  const uint32_t corner_first = arena.faces[placed.face_first].corner_first;
  placed.bounds               = {arena.corners[corner_first], arena.corners[corner_first]};
  for (uint32_t face = placed.face_first; face < placed.face_first + placed.face_count; ++face)
    for (uint32_t corner = arena.faces[face].corner_first;
         corner < arena.faces[face].corner_first + arena.faces[face].corner_count; ++corner)
      expand_aabb_to_include_point(placed.bounds, arena.corners[corner]);
  return piece;
}

// Piece `piece` less `volume`, in place in the arena: the pieces that tile what is not in shadow are appended
// live and the piece is retired. The shadow is convex (inside every side plane and every front plane), so
// piece k is inside the volume's first k - 1 planes and outside the k-th, and what is inside every plane is
// the shadow and is dropped. Returns false, changing nothing, when the volume misses the piece. Only a plane
// that SPLITS what is left earns a cut; one the remainder is wholly inside of constrains nothing.
bool try_carve_piece_by_volume(solid_beam_arena_t& arena, solid_beam_carve_scratch_t& scratch, uint32_t piece,
                               const shadow_volume_t& volume)
{
  const Plane* planes[MAX_SHADOW_VOLUME_PLANES];
  uint32_t     plane_count = 0;
  for (uint32_t side = 0; side < volume.side_plane_count; ++side)
    planes[plane_count++] = &volume.side_planes[side];
  for (uint32_t front = 0; front < volume.front_plane_count; ++front)
    planes[plane_count++] = &volume.front_planes[front];
  for (uint32_t index = 0; index < plane_count; ++index)
    if (classify_piece_against_plane(arena, piece, *planes[index]) == piece_side_t::Outside)
      return false;

  uint32_t remaining = piece;
  for (uint32_t index = 0; index < plane_count; ++index)
  {
    const piece_side_t side = classify_piece_against_plane(arena, remaining, *planes[index]);
    if (side == piece_side_t::Outside)
      return true;
    if (side == piece_side_t::Inside)
      continue;
    split_piece_t halves = split_piece_by_plane(arena, scratch, remaining, *planes[index], true);
    if (!halves.behind)
      return true;
    remaining = *halves.behind;
  }
  arena.pieces[remaining].live = false;
  return true;
}

uint32_t count_live_pieces(const solid_beam_arena_t& arena)
{
  uint32_t count = 0;
  for (const solid_beam_piece_t& piece : arena.pieces)
    count += piece.live ? 1u : 0u;
  return count;
}

void report_too_many_pieces_once(entity_uid_t spot, uint32_t piece_count, uint32_t volumes_left)
{
  static entity_uid_t last_reported = null_entity_uid;
  if (last_reported == spot)
    return;
  last_reported = spot;
  log_error("solid beam of spot {} is cut into {} pieces, past the {} it may have: {} shadow volumes in it are "
            "not subtracted and the beam stays solid behind their casters",
            spot, piece_count, MAX_SOLID_BEAM_PIECES, volumes_left);
}

bool same_planes(const Plane* a, const Plane* b, uint32_t count)
{
  return std::memcmp(a, b, count * sizeof(Plane)) == 0;
}

bool same_shadow_volume_cut(const shadow_volume_t& a, const shadow_volume_t& b)
{
  return a.side_plane_count == b.side_plane_count && a.front_plane_count == b.front_plane_count &&
         same_planes(&a.side_planes[0], &b.side_planes[0], a.side_plane_count) &&
         same_planes(&a.front_planes[0], &b.front_planes[0], a.front_plane_count);
}

bool same_beam_shape(const solid_beam_cache_entry_t& entry, const path_pose_t& pose, float range, float half_angle)
{
  return entry.range == range && entry.half_angle == half_angle &&
         std::memcmp(&entry.pose.position, &pose.position, sizeof(pose.position)) == 0 &&
         std::memcmp(&entry.pose.orientation, &pose.orientation, sizeof(pose.orientation)) == 0;
}

bool same_carve_inputs(const solid_beam_cache_entry_t& entry, const path_pose_t& pose, float range, float half_angle,
                       const std::vector<const shadow_volume_t*>& own_volumes)
{
  if (!same_beam_shape(entry, pose, range, half_angle) || entry.volumes.size() != own_volumes.size())
    return false;
  for (size_t index = 0; index < own_volumes.size(); ++index)
    if (!same_shadow_volume_cut(entry.volumes[index], *own_volumes[index]))
      return false;
  return true;
}

// The spot's entry carved from these inputs, or the one a miss overwrites: a free slot while the spot has
// fewer than SOLID_BEAM_CACHE_ENTRIES_PER_SPOT, else its oldest. `hit` says which.
solid_beam_cache_entry_t& find_cache_entry(solid_beam_cache_t& cache, entity_uid_t spot, const path_pose_t& pose,
                                           float range, float half_angle,
                                           const std::vector<const shadow_volume_t*>& own_volumes, bool& hit)
{
  uint32_t                  spot_entries = 0;
  solid_beam_cache_entry_t* oldest       = nullptr;
  for (solid_beam_cache_entry_t& entry : cache.entries)
  {
    if (entry.spot != spot)
      continue;
    if (same_carve_inputs(entry, pose, range, half_angle, own_volumes))
    {
      hit = true;
      return entry;
    }
    ++spot_entries;
    if (oldest == nullptr || entry.stamp < oldest->stamp)
      oldest = &entry;
  }
  hit = false;
  if (spot_entries >= SOLID_BEAM_CACHE_ENTRIES_PER_SPOT)
    return *oldest;
  cache.entries.push_back({.spot = spot});
  return cache.entries.back();
}

void write_entry_from_arena(solid_beam_cache_entry_t& entry, const solid_beam_arena_t& arena)
{
  entry.pieces.clear();
  entry.drawn_cut_planes.clear();
  entry.drawn_cut_first.clear();
  entry.drawn_cut_first.push_back(0);
  for (const solid_beam_piece_t& piece : arena.pieces)
  {
    if (!piece.live)
      continue;
    collision_piece_t out;
    out.bounds = piece.bounds;
    out.planes.reserve(piece.face_count);
    out.face_polygons.reserve(piece.face_count);
    for (uint32_t face = piece.face_first; face < piece.face_first + piece.face_count; ++face)
    {
      const solid_beam_face_t& polygon = arena.faces[face];
      out.planes.push_back(polygon.plane);
      out.face_polygons.emplace_back(arena.corners.begin() + polygon.corner_first,
                                     arena.corners.begin() + polygon.corner_first + polygon.corner_count);
    }
    entry.pieces.push_back(std::move(out));
    entry.drawn_cut_planes.insert(entry.drawn_cut_planes.end(), arena.cut_planes.begin() + piece.cut_first,
                                  arena.cut_planes.begin() + piece.cut_first + piece.cut_count);
    entry.drawn_cut_first.push_back(static_cast<uint32_t>(entry.drawn_cut_planes.size()));
  }
}

} // namespace

void collect_solid_beams(const Entity_System& system, const mover_rests_t& rests,
                         Span<const shadow_volume_t> shadow_volumes, beam_carve_scope_t scope,
                         solid_beam_cache_t& cache, std::vector<mover_t>& out)
{
  for (const entities::Spot_Light_Entity& spot : system.entities_of_type<entities::Spot_Light_Entity>())
  {
    const bool carved = spot.solid_beam || (scope == beam_carve_scope_t::Every_Beam && spot.beam);
    if (!carved || !light_is_switched_on(spot) || spot.range <= 0.f)
      continue;

    const path_pose_t placed = get_placed_pose_for_entity(spot);
    const entities::Rides* rides  = entities::get_rides(&spot);
    const path_pose_t      pose   = rides != nullptr ? ridden_pose_in_cut(out, rests, placed, *rides) : placed;

    const float half_angle = std::clamp(spot.outer_degrees, 1.f, MAX_SOLID_BEAM_HALF_ANGLE_DEGREES);

    // Nearest caster first: what it shadows is gone before a farther caster's volume is asked, so most of
    // the later volumes touch no piece that is left.
    std::vector<const shadow_volume_t*> own_volumes;
    for (const shadow_volume_t& volume : shadow_volumes)
      if (volume.light == spot.entity_id)
        own_volumes.push_back(&volume);
    std::stable_sort(own_volumes.begin(), own_volumes.end(), [&](const shadow_volume_t* a, const shadow_volume_t* b)
                     {
                       return linalg::length(get_aabb_center(a->caster_bounds) - pose.position) <
                              linalg::length(get_aabb_center(b->caster_bounds) - pose.position);
                     });

    bool                      hit   = false;
    solid_beam_cache_entry_t& entry = find_cache_entry(cache, spot.entity_id, pose, spot.range, half_angle, own_volumes, hit);
    if (!hit)
    {
      const solid_beam_arena_t&   pyramid = find_or_build_canonical_pyramid(cache, spot.range, half_angle);
      solid_beam_carve_scratch_t& scratch = cache.scratch;
      solid_beam_arena_t&         arena   = scratch.arena;
      clear_arena(arena);
      append_pyramid_at_pose(arena, pyramid, pose.position,
                             linalg::normalize(linalg::basis_from(pose.orientation).forward));
      for (uint32_t index = 0; index < own_volumes.size(); ++index)
      {
        const shadow_volume_t& volume     = *own_volumes[index];
        const uint32_t         live_count = count_live_pieces(arena);
        if (live_count > MAX_SOLID_BEAM_PIECES)
        {
          report_too_many_pieces_once(spot.entity_id, live_count, static_cast<uint32_t>(own_volumes.size() - index));
          break;
        }
        scratch.live_pieces.clear();
        for (uint32_t piece = 0; piece < arena.pieces.size(); ++piece)
          if (arena.pieces[piece].live && shadow_volume_touches_box(volume, arena.pieces[piece].bounds))
            scratch.live_pieces.push_back(piece);
        for (const uint32_t piece : scratch.live_pieces)
          try_carve_piece_by_volume(arena, scratch, piece, volume);
      }
      entry.pose       = pose;
      entry.range      = spot.range;
      entry.half_angle = half_angle;
      entry.volumes.clear();
      for (const shadow_volume_t* volume : own_volumes)
        entry.volumes.push_back(*volume);
      write_entry_from_arena(entry, arena);
    }
    entry.stamp = cache.next_stamp++;

    if (!spot.solid_beam || entry.pieces.empty())
      continue;
    mover_t cut;
    cut.uid                = spot.entity_id;
    cut.pose_at_tick_start = pose;
    cut.pose_at_tick_end   = pose;
    cut.crushes            = false;
    cut.pieces             = entry.pieces;
    cut.swept_bounds       = cut.pieces.front().bounds;
    for (const collision_piece_t& piece : cut.pieces)
      cut.swept_bounds = union_aabb(cut.swept_bounds, piece.bounds);
    out.push_back(std::move(cut));
  }
}

std::optional<beam_carve_t> try_find_beam_carve(const solid_beam_cache_t& cache, entity_uid_t spot)
{
  const solid_beam_cache_entry_t* newest = nullptr;
  for (const solid_beam_cache_entry_t& entry : cache.entries)
    if (entry.spot == spot && entry.stamp != 0 && (newest == nullptr || entry.stamp > newest->stamp))
      newest = &entry;
  if (newest == nullptr || newest->pieces.empty())
    return std::nullopt;
  return beam_carve_t{.cut_planes  = Span<const Plane>(newest->drawn_cut_planes.data(),
                                                            static_cast<uint32_t>(newest->drawn_cut_planes.size())),
                      .piece_first = Span<const uint32_t>(newest->drawn_cut_first.data(),
                                                          static_cast<uint32_t>(newest->drawn_cut_first.size()))};
}

} // namespace shared
