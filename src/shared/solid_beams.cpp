#include "solid_beams.hpp"

#include "convex_decomposition.hpp"
#include "entities/generated/entities/spot_light_entity_generated.hpp"
#include "entity_system.hpp"
#include "lighting.hpp"
#include "log.hpp"
#include "map_geometry.hpp"
#include "mover_path.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace shared
{

namespace
{

// Thinner than this is a sliver, not a piece: a corner this close to a cut is on it. The wire rounds to 1/32.
constexpr float ON_PLANE_TOLERANCE = 0.5f;

collision_piece_t piece_from_beam(const linalg::vec3f& apex, const linalg::quatf& orientation, float range,
                                  float half_angle_degrees)
{
  const linalg::basis_t basis  = linalg::basis_from(orientation);
  const linalg::vec3f   axis   = linalg::normalize(basis.forward);
  const linalg::vec3f   right  = linalg::normalize(basis.right);
  const linalg::vec3f   up     = linalg::normalize(basis.up);
  const float           radius = range * std::tan(linalg::to_radians(half_angle_degrees));
  const linalg::vec3f   cap    = apex + axis * range;
  const linalg::vec3f   inside = apex + axis * (range * 0.5f);

  std::vector<linalg::vec3> ring(SOLID_BEAM_SIDE_COUNT);
  for (uint32_t side = 0; side < SOLID_BEAM_SIDE_COUNT; ++side)
  {
    const float turn = 2.f * std::numbers::pi_v<float> * static_cast<float>(side) /
                       static_cast<float>(SOLID_BEAM_SIDE_COUNT);
    ring[side] = cap + right * (std::cos(turn) * radius) + up * (std::sin(turn) * radius);
  }

  collision_piece_t piece;
  piece.bounds = {apex, apex};
  expand_aabb_to_include_point(piece.bounds, cap);
  for (const linalg::vec3& corner : ring)
    expand_aabb_to_include_point(piece.bounds, corner);

  for (uint32_t side = 0; side < SOLID_BEAM_SIDE_COUNT; ++side)
  {
    const linalg::vec3 a = ring[side];
    const linalg::vec3 b = ring[(side + 1) % SOLID_BEAM_SIDE_COUNT];
    linalg::vec3       normal = linalg::normalize(linalg::cross(a - apex, b - apex));
    std::vector<linalg::vec3> polygon = {apex, a, b};
    if (linalg::dot(inside - apex, normal) > 0.f)
    {
      normal = normal * -1.f;
      polygon = {apex, b, a};
    }
    piece.planes.push_back({.point = apex, .normal = normal});
    piece.face_polygons.push_back(std::move(polygon));
  }

  piece.planes.push_back({.point = cap, .normal = axis});
  std::vector<linalg::vec3> cap_polygon = ring;
  if (linalg::dot(linalg::cross(ring[0] - cap, ring[1] - cap), axis) < 0.f)
    std::reverse(cap_polygon.begin(), cap_polygon.end());
  piece.face_polygons.push_back(std::move(cap_polygon));
  return piece;
}

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
piece_side_t classify_piece_against_plane(const collision_piece_t& piece, const Plane& plane)
{
  bool any_inside  = false;
  bool any_outside = false;
  for (const std::vector<linalg::vec3>& polygon : piece.face_polygons)
    for (const linalg::vec3& corner : polygon)
    {
      const float height = linalg::dot(corner - plane.point, plane.normal);
      any_inside  = any_inside || height < -ON_PLANE_TOLERANCE;
      any_outside = any_outside || height > ON_PLANE_TOLERANCE;
    }
  if (any_inside && any_outside)
    return piece_side_t::Split;
  return any_outside ? piece_side_t::Outside : piece_side_t::Inside;
}

// The cap a cut leaves: every clipped corner on the plane, once, wound about their centre.
std::vector<linalg::vec3> cap_polygon_on_plane(const std::vector<std::vector<linalg::vec3>>& clipped_faces,
                                               const Plane&                                  plane)
{
  std::vector<linalg::vec3> points;
  for (const std::vector<linalg::vec3>& polygon : clipped_faces)
    for (const linalg::vec3& corner : polygon)
    {
      if (std::fabs(linalg::dot(corner - plane.point, plane.normal)) > ON_PLANE_TOLERANCE)
        continue;
      bool known = false;
      for (const linalg::vec3& point : points)
        known = known || linalg::length(point - corner) < ON_PLANE_TOLERANCE;
      if (!known)
        points.push_back(corner);
    }
  if (points.size() < 3)
    return {};

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
  return points;
}

struct split_piece_t
{
  std::optional<collision_piece_t> behind;
  std::optional<collision_piece_t> in_front;
};

std::optional<collision_piece_t> finish_half(std::vector<Plane>&& planes, std::vector<std::vector<linalg::vec3>>&& faces,
                                             const Plane& cut)
{
  std::vector<linalg::vec3> cap = cap_polygon_on_plane(faces, cut);
  if (!cap.empty())
  {
    planes.push_back(cut);
    faces.push_back(std::move(cap));
  }
  if (faces.size() < 4)
    return std::nullopt;
  collision_piece_t piece;
  piece.planes        = std::move(planes);
  piece.face_polygons = std::move(faces);
  piece.bounds        = {piece.face_polygons[0][0], piece.face_polygons[0][0]};
  for (const std::vector<linalg::vec3>& polygon : piece.face_polygons)
    for (const linalg::vec3& corner : polygon)
      expand_aabb_to_include_point(piece.bounds, corner);
  return piece;
}

// A convex piece cut by one plane into the convex halves behind it and in front of it, either empty when the
// plane misses it. Each face polygon is clipped to its half; the cut itself becomes one new face per half. No
// hull is rebuilt: the halves' planes are the faces that survived plus the cut.
split_piece_t split_piece_by_plane(const collision_piece_t& piece, const Plane& plane)
{
  std::vector<Plane>                     behind_planes;
  std::vector<std::vector<linalg::vec3>> behind_faces;
  std::vector<Plane>                     front_planes;
  std::vector<std::vector<linalg::vec3>> front_faces;
  const Plane                            front_side = flipped(plane);
  for (size_t face = 0; face < piece.planes.size(); ++face)
  {
    std::vector<linalg::vec3> behind = clip_polygon_behind(piece.face_polygons[face], plane, 0.f);
    if (behind.size() >= 3)
    {
      behind_planes.push_back(piece.planes[face]);
      behind_faces.push_back(std::move(behind));
    }
    std::vector<linalg::vec3> in_front = clip_polygon_behind(piece.face_polygons[face], front_side, 0.f);
    if (in_front.size() >= 3)
    {
      front_planes.push_back(piece.planes[face]);
      front_faces.push_back(std::move(in_front));
    }
  }
  return {.behind   = finish_half(std::move(behind_planes), std::move(behind_faces), plane),
          .in_front = finish_half(std::move(front_planes), std::move(front_faces), front_side)};
}

// `piece` less `volume`, as disjoint convex pieces appended to `out`: piece k is inside the volume's first k - 1
// side planes and outside the k-th; the last is inside every side plane and in front of every back plane, which
// is the part of the pyramid between the light and the caster. Together they tile what is not in shadow.
// Returns false, appending nothing, when the volume misses the piece: the caller keeps the piece whole. Only a
// plane that SPLITS what is left earns a cut; one the remainder is wholly inside of constrains nothing.
bool try_carve_piece_by_volume(const collision_piece_t& piece, const shadow_volume_t& volume,
                               std::vector<collision_piece_t>& out)
{
  for (uint32_t side = 0; side < volume.side_plane_count; ++side)
    if (classify_piece_against_plane(piece, volume.side_planes[side]) == piece_side_t::Outside)
      return false;
  bool wholly_in_front_of_caster = true;
  for (uint32_t back = 0; back < volume.back_plane_count; ++back)
    wholly_in_front_of_caster =
        wholly_in_front_of_caster && classify_piece_against_plane(piece, volume.back_planes[back]) == piece_side_t::Inside;
  if (wholly_in_front_of_caster)
    return false;

  collision_piece_t remaining = piece;
  for (uint32_t side = 0; side < volume.side_plane_count; ++side)
  {
    if (classify_piece_against_plane(remaining, volume.side_planes[side]) != piece_side_t::Split)
      continue;
    split_piece_t halves = split_piece_by_plane(remaining, volume.side_planes[side]);
    if (halves.in_front)
      out.push_back(std::move(*halves.in_front));
    if (!halves.behind)
      return true;
    remaining = std::move(*halves.behind);
  }
  for (uint32_t back = 0; back < volume.back_plane_count; ++back)
  {
    const piece_side_t side = classify_piece_against_plane(remaining, volume.back_planes[back]);
    if (side == piece_side_t::Outside)
      return true;
    if (side == piece_side_t::Inside)
      continue;
    split_piece_t halves = split_piece_by_plane(remaining, volume.back_planes[back]);
    if (!halves.behind)
      return true;
    remaining = std::move(*halves.behind);
  }
  out.push_back(std::move(remaining));
  return true;
}

void report_too_many_pieces_once(entity_uid_t spot, size_t piece_count, uint32_t volumes_left)
{
  static entity_uid_t last_reported = null_entity_uid;
  if (last_reported == spot)
    return;
  last_reported = spot;
  log_error("solid beam of spot {} is cut into {} pieces, past the {} it may have: {} shadow volumes in it are "
            "not subtracted and the beam stays solid behind their casters",
            spot, piece_count, MAX_SOLID_BEAM_PIECES, volumes_left);
}

} // namespace

void collect_solid_beams(const Entity_System& system, const mover_rests_t& rests,
                         Span<const shadow_volume_t> shadow_volumes, std::vector<mover_t>& out)
{
  for (const entities::Spot_Light_Entity& spot : system.entities_of_type<entities::Spot_Light_Entity>())
  {
    if (!spot.solid_beam || !light_is_switched_on(spot) || spot.range <= 0.f)
      continue;

    const path_pose_t      placed = get_placed_pose_for_entity(spot);
    const entities::Rides* rides  = entities::get_rides(&spot);
    const path_pose_t      pose   = rides != nullptr ? ridden_pose_in_cut(out, rests, placed, *rides) : placed;

    const float half_angle = std::clamp(spot.outer_degrees, 1.f, MAX_SOLID_BEAM_HALF_ANGLE_DEGREES);

    mover_t cut;
    cut.uid                = spot.entity_id;
    cut.pose_at_tick_start = pose;
    cut.pose_at_tick_end   = pose;
    cut.crushes            = false;
    cut.pieces.push_back(piece_from_beam(pose.position, pose.orientation, spot.range, half_angle));
    cut.swept_bounds = cut.pieces.back().bounds;

    // Nearest caster first: what it shadows is gone before a farther caster's volume is asked, so most of
    // the later volumes touch no piece that is left.
    std::vector<const shadow_volume_t*> own_volumes;
    for (const shadow_volume_t& volume : shadow_volumes)
      if (volume.light == spot.entity_id)
        own_volumes.push_back(&volume);
    std::sort(own_volumes.begin(), own_volumes.end(), [&](const shadow_volume_t* a, const shadow_volume_t* b)
              {
                return linalg::length(get_aabb_center(a->caster_bounds) - pose.position) <
                       linalg::length(get_aabb_center(b->caster_bounds) - pose.position);
              });

    std::vector<collision_piece_t> carved;
    for (uint32_t index = 0; index < own_volumes.size(); ++index)
    {
      const shadow_volume_t& volume = *own_volumes[index];
      if (cut.pieces.size() > MAX_SOLID_BEAM_PIECES)
      {
        report_too_many_pieces_once(spot.entity_id, cut.pieces.size(),
                                    static_cast<uint32_t>(own_volumes.size() - index));
        break;
      }
      carved.clear();
      for (const collision_piece_t& piece : cut.pieces)
        if (!shadow_volume_touches_box(volume, piece.bounds) || !try_carve_piece_by_volume(piece, volume, carved))
          carved.push_back(piece);
      cut.pieces.swap(carved);
    }

    if (cut.pieces.empty())
      continue;
    cut.swept_bounds = cut.pieces.front().bounds;
    for (const collision_piece_t& piece : cut.pieces)
      cut.swept_bounds = union_aabb(cut.swept_bounds, piece.bounds);
    out.push_back(std::move(cut));
  }
}

} // namespace shared
