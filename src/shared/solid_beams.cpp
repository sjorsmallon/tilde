#include "solid_beams.hpp"

#include "entities/generated/entities/spot_light_entity_generated.hpp"
#include "entity_system.hpp"
#include "lighting.hpp"
#include "map_geometry.hpp"
#include "mover_path.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace shared
{

namespace
{

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

} // namespace

void collect_solid_beams(const Entity_System& system, const mover_rests_t& rests, std::vector<mover_t>& out)
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
    out.push_back(std::move(cut));
  }
}

} // namespace shared
