#include "shadow_volume.hpp"

#include "brush.hpp"
#include "collision_detection.hpp"
#include "convex_decomposition.hpp"
#include "entities/generated/entities/directional_light_entity_generated.hpp"
#include "entities/generated/entities/geometry_owner_entity_generated.hpp"
#include "entities/generated/entities/point_light_entity_generated.hpp"
#include "entities/generated/entities/spot_light_entity_generated.hpp"
#include "entity_system.hpp"
#include "lighting.hpp"
#include "log.hpp"
#include "reveal_light.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <format>

namespace shared
{

std::optional<shadow_light_t> try_shadow_light_from_entity(const entities::Entity& entity, const path_pose_t& pose)
{
  if (const entities::Point_Light_Entity* point = entities::entity_as<entities::Point_Light_Entity>(&entity))
  {
    if (!point->light.cuts_geometry || !light_is_switched_on(entity))
      return std::nullopt;
    return shadow_light_t{.uid = point->entity_id, .apex = pose.position, .range = point->range};
  }
  if (const entities::Spot_Light_Entity* spot = entities::entity_as<entities::Spot_Light_Entity>(&entity))
  {
    if ((!spot->light.cuts_geometry && !spot->beam && !spot->solid_beam) || !light_is_switched_on(entity))
      return std::nullopt;
    return shadow_light_t{.uid                   = spot->entity_id,
                          .apex                  = pose.position,
                          .direction             = linalg::normalize(linalg::basis_from(pose.orientation).forward),
                          .range                 = spot->range,
                          .cosine_of_outer_angle = std::cos(linalg::to_radians(spot->outer_degrees)),
                          .cuts_geometry         = spot->light.cuts_geometry,
                          .keeps_every_volume    = spot->beam || spot->solid_beam};
  }
  if (const entities::Directional_Light_Entity* directional =
          entities::entity_as<entities::Directional_Light_Entity>(&entity))
  {
    if (!directional->light.cuts_geometry || !light_is_switched_on(entity))
      return std::nullopt;
    return shadow_light_t{.uid         = directional->entity_id,
                          .directional = true,
                          .direction   = linalg::normalize(linalg::basis_from(pose.orientation).forward)};
  }
  return std::nullopt;
}

std::optional<Plane> try_shadow_light_far_cap(const shadow_light_t& light, const linalg::vec3f& center)
{
  if (light.directional || light.range <= 0.f)
    return std::nullopt;
  const bool          spot = light.cosine_of_outer_angle > -1.f;
  const linalg::vec3f axis = linalg::normalize(spot ? light.direction : center - light.apex);
  return Plane{.point = light.apex + axis * light.range, .normal = axis};
}

namespace
{

Plane plane_outward_from(const linalg::vec3f& point, linalg::vec3f normal, const linalg::vec3f& inside)
{
  normal = linalg::normalize(normal);
  if (linalg::dot(inside - point, normal) > 0.f)
    normal = normal * -1.f;
  return {.point = point, .normal = normal};
}

shadow_cast_t refused(shadow_cast_refusal_t refusal)
{
  return {.refusal = refusal};
}

// A light within this of a face's plane sits ON that face (a fixture mounted on a wall, placed to the wire's
// 1/32) and shines away from it: the face counts as toward the light, not as the light inside the piece.
constexpr float LIGHT_ON_FACE_TOLERANCE = 0.25f;

bool plane_faces_away_from_light(const shadow_light_t& light, const Plane& plane)
{
  return light.directional ? linalg::dot(plane.normal, light.direction) > 0.f
                           : linalg::dot(plane.normal, plane.point - light.apex) > LIGHT_ON_FACE_TOLERANCE;
}

} // namespace

shadow_cast_t cast_shadow_volume(const shadow_light_t& light, Span<const Plane> piece_planes,
                                 Span<const std::vector<linalg::vec3>> corner_polygons, entity_uid_t caster)
{
  if (piece_planes.size() != corner_polygons.size())
    fatal_error("cast_shadow_volume: {} planes but {} face polygons; face_polygons[i] is the polygon of planes[i]",
                piece_planes.size(), corner_polygons.size());

  linalg::vec3f center{0.f, 0.f, 0.f};
  uint32_t      corner_count = 0;
  aabb_bounds_t piece_bounds = {};
  for (const std::vector<linalg::vec3>& polygon : corner_polygons)
    for (const linalg::vec3& corner : polygon)
    {
      center = center + corner;
      piece_bounds = corner_count == 0 ? aabb_bounds_t{corner, corner} : piece_bounds;
      expand_aabb_to_include_point(piece_bounds, corner);
      ++corner_count;
    }
  if (corner_count < 3)
    return refused(shadow_cast_refusal_t::Silhouette_Open);
  std::optional<reveal_cone_planes_t> beam;
  if (light.cosine_of_outer_angle > -1.f)
  {
    beam = planes_of_reveal_cone({.apex                 = light.apex,
                                  .axis                 = light.direction,
                                  .range                = light.range > 0.f ? light.range : FLT_MAX,
                                  .cosine_of_half_angle = light.cosine_of_outer_angle});
    if (!reveal_cone_touches_box(*beam, piece_bounds))
      return refused(shadow_cast_refusal_t::Outside_Spot_Beam);
  }
  center = center * (1.f / static_cast<float>(corner_count));

  constexpr float MIN_DEPTH = 1e-3f;

  // The ray through a corner.
  const auto ray_through = [&](const linalg::vec3f& corner) -> linalg::vec3f
  { return light.directional ? light.direction : linalg::normalize(corner - light.apex); };

  shadow_cast_t    cast;
  shadow_volume_t& volume = cast.volume;
  volume.caster           = caster;
  volume.light            = light.uid;
  volume.cuts_geometry    = light.cuts_geometry;
  volume.caster_bounds    = piece_bounds;
  volume.light_apex       = light.apex;
  volume.light_direction  = light.direction;
  volume.light_directional = light.directional;

  // A face is FRONT when it faces the light: where a ray from the light enters the piece, and the shadow
  // begins. Every face toward the light, or none, is the light inside the piece.
  std::vector<bool> faces_away(piece_planes.size(), false);
  for (uint32_t index = 0; index < piece_planes.size(); ++index)
  {
    const Plane& plane = piece_planes[index];
    faces_away[index]  = plane_faces_away_from_light(light, plane);
    if (faces_away[index])
      continue;
    if (volume.front_plane_count == MAX_SHADOW_VOLUME_FRONT_PLANES)
      return refused(shadow_cast_refusal_t::Too_Many_Planes);
    volume.front_planes[volume.front_plane_count++] = plane;
  }
  if (volume.front_plane_count == 0 || volume.front_plane_count == piece_planes.size())
    return refused(shadow_cast_refusal_t::Light_Inside_Caster);

  // The silhouette: every edge a back face shares with a front face, wound as the back face winds it.
  constexpr float EDGE_EPSILON = 1e-2f;
  const auto      same_point   = [](const linalg::vec3f& a, const linalg::vec3f& b)
  { return linalg::length(a - b) <= EDGE_EPSILON; };
  struct silhouette_edge_t
  {
    linalg::vec3f a;
    linalg::vec3f b;
  };
  std::vector<silhouette_edge_t> edges;
  for (uint32_t back = 0; back < piece_planes.size(); ++back)
  {
    if (!faces_away[back])
      continue;
    const std::vector<linalg::vec3>& polygon = corner_polygons[back];
    for (size_t k = 0; k < polygon.size(); ++k)
    {
      const linalg::vec3f a = polygon[k];
      const linalg::vec3f b = polygon[(k + 1) % polygon.size()];
      bool                shared_with_front = false;
      for (uint32_t front = 0; front < piece_planes.size() && !shared_with_front; ++front)
      {
        if (faces_away[front])
          continue;
        const std::vector<linalg::vec3>& other = corner_polygons[front];
        for (size_t m = 0; m < other.size() && !shared_with_front; ++m)
        {
          const linalg::vec3f& c = other[m];
          const linalg::vec3f& d = other[(m + 1) % other.size()];
          shared_with_front = (same_point(c, b) && same_point(d, a)) || (same_point(c, a) && same_point(d, b));
        }
      }
      if (shared_with_front)
        edges.push_back({a, b});
    }
  }
  if (edges.size() < 3)
    return refused(shadow_cast_refusal_t::Silhouette_Open);
  if (edges.size() > MAX_SHADOW_VOLUME_SIDES)
    return refused(shadow_cast_refusal_t::Too_Many_Planes);

  for (const silhouette_edge_t& edge : edges)
  {
    if (!light.directional && linalg::length(edge.a - light.apex) < MIN_DEPTH)
      return refused(shadow_cast_refusal_t::Light_On_Silhouette);
    const linalg::vec3f normal = linalg::cross(edge.b - edge.a, ray_through(edge.a));
    if (linalg::length(normal) < 1e-6f)
      return refused(shadow_cast_refusal_t::Light_On_Silhouette);
    volume.side_planes[volume.side_plane_count++] = plane_outward_from(edge.a, normal, center);
  }
  volume.ring_plane_count = volume.side_plane_count;

  if (const std::optional<Plane> far_cap = try_shadow_light_far_cap(light, center))
  {
    volume.side_planes[volume.side_plane_count++] = *far_cap;
    volume.has_far_cap                            = true;
  }

  // The beam clips the pyramid, last: these planes are the beam's own surface, and the beam pass leaves them
  // out of its margins (a chord ending on the beam's surface is not at a shadow's edge). Both pass through
  // the apex, so a cone plane every silhouette corner is inside of has every ray inside of it too and is
  // left out; one some corner is outside of is a side.
  if (beam)
  {
    for (const Plane& cone_side : beam->sides)
    {
      bool cuts = false;
      for (const silhouette_edge_t& edge : edges)
        if (linalg::dot(edge.a - cone_side.point, cone_side.normal) > 0.f)
        {
          cuts = true;
          break;
        }
      if (!cuts)
        continue;
      if (volume.side_plane_count == MAX_SHADOW_VOLUME_SIDES)
        return refused(shadow_cast_refusal_t::Too_Many_Planes);
      volume.side_planes[volume.side_plane_count++] = cone_side;
    }
  }

  // The rings: the silhouette chained into its loop (face windings need not agree, so an edge is taken
  // either way round), and each corner down its ray. They bound the drawn body; the debug draw reads the
  // planes instead (collect_shadow_volume_edges).
  std::vector<bool> used(edges.size(), false);
  silhouette_edge_t edge = edges[0];
  used[0]                = true;
  volume.side_count      = 0;
  for (size_t step = 0; step < edges.size(); ++step)
  {
    const float far_distance = !light.directional && light.range > 0.f
                                   ? std::max(light.range - linalg::length(edge.a - light.apex), 0.f)
                                   : SHADOW_VOLUME_DRAWN_REACH;
    volume.near_ring[volume.side_count] = edge.a;
    volume.far_ring[volume.side_count]  = edge.a + ray_through(edge.a) * far_distance;
    ++volume.side_count;

    bool found_next = false;
    for (size_t candidate = 0; candidate < edges.size() && !found_next; ++candidate)
    {
      if (used[candidate])
        continue;
      if (same_point(edges[candidate].a, edge.b))
        edge = edges[candidate];
      else if (same_point(edges[candidate].b, edge.b))
        edge = {edges[candidate].b, edges[candidate].a};
      else
        continue;
      used[candidate] = true;
      found_next      = true;
    }
    if (!found_next)
      break;
  }
  if (volume.side_count != edges.size())
    volume.side_count = 0;

  return cast;
}

shadow_cast_refusal_t cast_shadow_volumes_behind_caster(const shadow_light_t& light, Span<const Plane> piece_planes,
                                                        Span<const std::vector<linalg::vec3>> corner_polygons,
                                                        entity_uid_t caster, std::vector<shadow_volume_t>& out)
{
  const shadow_cast_t cast = cast_shadow_volume(light, piece_planes, corner_polygons, caster);
  if (cast.refusal != shadow_cast_refusal_t::None)
    return cast.refusal;
  for (const Plane& plane : piece_planes)
  {
    if (!plane_faces_away_from_light(light, plane))
      continue;
    shadow_volume_t beyond   = cast.volume;
    beyond.front_planes[0]   = {.point = plane.point, .normal = plane.normal * -1.f};
    beyond.front_plane_count = 1;
    out.push_back(beyond);
  }
  return shadow_cast_refusal_t::None;
}

void collect_shadow_volume_edges(const shadow_volume_t& volume, float reach, std::vector<shadow_volume_edge_t>& out)
{
  std::vector<Plane> planes;
  for (uint32_t side = 0; side < volume.side_plane_count; ++side)
    planes.push_back(volume.side_planes[side]);
  const linalg::vec3f centre = get_aabb_center(volume.caster_bounds);
  const linalg::vec3f half   = (volume.caster_bounds.max - volume.caster_bounds.min) * 0.5f +
                               linalg::vec3f{reach, reach, reach};
  planes.push_back({.point = centre + linalg::vec3f{half.x, 0.f, 0.f}, .normal = {1.f, 0.f, 0.f}});
  planes.push_back({.point = centre - linalg::vec3f{half.x, 0.f, 0.f}, .normal = {-1.f, 0.f, 0.f}});
  planes.push_back({.point = centre + linalg::vec3f{0.f, half.y, 0.f}, .normal = {0.f, 1.f, 0.f}});
  planes.push_back({.point = centre - linalg::vec3f{0.f, half.y, 0.f}, .normal = {0.f, -1.f, 0.f}});
  planes.push_back({.point = centre + linalg::vec3f{0.f, 0.f, half.z}, .normal = {0.f, 0.f, 1.f}});
  planes.push_back({.point = centre - linalg::vec3f{0.f, 0.f, half.z}, .normal = {0.f, 0.f, -1.f}});

  constexpr float PARALLEL = 1e-6f;
  constexpr float ON_PLANE = 1e-3f;
  for (size_t i = 0; i < planes.size(); ++i)
    for (size_t j = i + 1; j < planes.size(); ++j)
    {
      const linalg::vec3f direction = linalg::cross(planes[i].normal, planes[j].normal);
      if (linalg::length(direction) < PARALLEL)
        continue;
      const linalg::vec3f n1 = planes[i].normal;
      const linalg::vec3f n2 = planes[j].normal;
      const linalg::vec3f n3 = linalg::normalize(direction);
      const float         d1 = linalg::dot(n1, planes[i].point);
      const float         d2 = linalg::dot(n2, planes[j].point);
      const float         d3 = linalg::dot(n3, centre);
      const float         determinant = linalg::dot(n1, linalg::cross(n2, n3));
      if (std::fabs(determinant) < PARALLEL)
        continue;
      const linalg::vec3f point =
          (linalg::cross(n2, n3) * d1 + linalg::cross(n3, n1) * d2 + linalg::cross(n1, n2) * d3) * (1.f / determinant);

      float from  = -4.f * reach;
      float to    = 4.f * reach;
      bool  empty = false;
      for (size_t k = 0; k < planes.size() && !empty; ++k)
      {
        if (k == i || k == j)
          continue;
        const float along  = linalg::dot(planes[k].normal, n3);
        const float height = linalg::dot(planes[k].normal, point - planes[k].point);
        if (std::fabs(along) < PARALLEL)
        {
          empty = height > ON_PLANE;
          continue;
        }
        const float t = -height / along;
        if (along > 0.f)
          to = std::min(to, t);
        else
          from = std::max(from, t);
      }
      if (empty || to <= from + ON_PLANE)
        continue;
      out.push_back({.a = point + n3 * from, .b = point + n3 * to});
    }
}

std::optional<shadow_occluder_t> try_cast_shadow_occluder(const shadow_light_t& light, Span<const Plane> piece_planes,
                                                          Span<const std::vector<linalg::vec3>> corner_polygons,
                                                          uint32_t volume_bits, bool receives)
{
  if ((volume_bits & SHADOW_OCCLUDER_RECEIVES_BIT) != 0)
    fatal_error("try_cast_shadow_occluder: volume bit 31 is the receives bit");
  shadow_light_t unbounded = light;
  unbounded.range          = 0.f;

  std::vector<Plane>                     cut_planes;
  std::vector<std::vector<linalg::vec3>> cut_polygons;
  const bool                             spot = light.cosine_of_outer_angle > -1.f;
  if (spot && light.range > 0.f)
  {
    linalg::vec3f center{0.f, 0.f, 0.f};
    float         radius       = 0.f;
    uint32_t      corner_count = 0;
    for (const std::vector<linalg::vec3>& polygon : corner_polygons)
      for (const linalg::vec3& corner : polygon)
      {
        center = center + corner;
        ++corner_count;
      }
    if (corner_count == 0)
      return std::nullopt;
    center = center * (1.f / static_cast<float>(corner_count));
    for (const std::vector<linalg::vec3>& polygon : corner_polygons)
      for (const linalg::vec3& corner : polygon)
        radius = std::max(radius, linalg::length(corner - center));
    cut_planes.assign(piece_planes.begin(), piece_planes.end());
    cut_planes.push_back(*try_shadow_light_far_cap(light, center));
    const std::optional<brush_polyhedron_t> cut = try_build_convex_from_planes(cut_planes, center, radius + 1.f);
    if (!cut)
      return std::nullopt;
    cut_planes.clear();
    for (const brush_face_t& face : cut->faces)
    {
      cut_planes.push_back(face.plane);
      std::vector<linalg::vec3>& polygon = cut_polygons.emplace_back();
      for (const uint32_t index : face.vertex_indices)
        polygon.push_back(cut->vertices[index]);
    }
    piece_planes    = cut_planes;
    corner_polygons = cut_polygons;
  }

  const shadow_cast_t cast = cast_shadow_volume(unbounded, piece_planes, corner_polygons, null_entity_uid);
  if (cast.refusal != shadow_cast_refusal_t::None)
    return std::nullopt;
  shadow_occluder_t occluder{.volume_bits  = volume_bits,
                             .receives     = receives,
                             .light        = light.uid,
                             .piece_bounds = cast.volume.caster_bounds};
  for (uint32_t index = 0; index < cast.volume.side_plane_count; ++index)
    occluder.pyramid_planes[occluder.pyramid_plane_count++] = cast.volume.side_planes[index];
  for (const Plane& plane : piece_planes)
  {
    if (plane_faces_away_from_light(light, plane))
      continue;
    if (occluder.front_plane_count == MAX_SHADOW_VOLUME_FRONT_PLANES)
      return std::nullopt;
    occluder.front_planes[occluder.front_plane_count++] = plane;
  }
  return occluder;
}

std::optional<aabb_bounds_t> try_compute_drawn_shadow_body_bounds(const shadow_volume_t&        volume,
                                                                  Span<const shadow_occluder_t> occluders,
                                                                  uint32_t                      volume_bit)
{
  aabb_bounds_t box   = volume.caster_bounds;
  bool          lands = false;
  for (const shadow_occluder_t& occluder : occluders)
  {
    if (!occluder.receives || (occluder.volume_bits & volume_bit) == 0)
      continue;
    box   = union_aabb(box, occluder.piece_bounds);
    lands = true;
  }
  if (!lands)
    return std::nullopt;

  // The silhouette's pyramid with its far cap, which holds the spot-clipped volume and so its body, cut to
  // the box: every vertex of that cut and nothing else. Either polytope's corners inside the other, the
  // pyramid's edges (the ray to each far corner, the cap's rim) through the box's faces, the box's edges
  // through the pyramid's planes. The bounds of those are the cut's bounds, with nothing allocated.
  if (volume.side_count == 0)
    return box;
  constexpr float TOLERANCE   = 0.5f;
  Array<Plane, SHADOW_VOLUME_SIDE_SLOTS> planes;
  uint32_t                               plane_count = 0;
  for (uint32_t index = 0; index < volume.ring_plane_count; ++index)
    planes[plane_count++] = volume.side_planes[index];
  if (volume.has_far_cap)
    planes[plane_count++] = volume.side_planes[volume.ring_plane_count];
  const Span<const Plane> sides(planes.data, plane_count);
  bool                    any    = false;
  aabb_bounds_t           bounds = {};
  const auto consider = [&](const linalg::vec3f& point)
  {
    if (point.x < box.min.x - TOLERANCE || point.x > box.max.x + TOLERANCE || point.y < box.min.y - TOLERANCE ||
        point.y > box.max.y + TOLERANCE || point.z < box.min.z - TOLERANCE || point.z > box.max.z + TOLERANCE)
      return;
    for (const Plane& side : sides)
      if (linalg::dot(side.normal, point - side.point) > TOLERANCE)
        return;
    bounds = any ? bounds : aabb_bounds_t{point, point};
    expand_aabb_to_include_point(bounds, point);
    any = true;
  };
  const auto consider_segment_through_box_faces = [&](const linalg::vec3f& a, const linalg::vec3f& b)
  {
    consider(a);
    consider(b);
    const linalg::vec3f along = b - a;
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
      if (std::abs(along[axis]) < 1e-6f)
        continue;
      for (const float face : {box.min[axis], box.max[axis]})
      {
        const float t = (face - a[axis]) / along[axis];
        if (t >= 0.f && t <= 1.f)
          consider(a + along * t);
      }
    }
  };

  const float reach = linalg::length(box.max - box.min) + 1.f;
  for (uint32_t side = 0; side < volume.side_count; ++side)
  {
    const linalg::vec3f far_corner = volume.far_ring[side];
    consider_segment_through_box_faces(
        volume.light_directional ? volume.near_ring[side] - volume.light_direction * reach : volume.light_apex,
        far_corner);
    consider_segment_through_box_faces(far_corner, volume.far_ring[(side + 1) % volume.side_count]);
  }

  const linalg::vec3f corners[8] = {
      {box.min.x, box.min.y, box.min.z}, {box.max.x, box.min.y, box.min.z}, {box.min.x, box.max.y, box.min.z},
      {box.max.x, box.max.y, box.min.z}, {box.min.x, box.min.y, box.max.z}, {box.max.x, box.min.y, box.max.z},
      {box.min.x, box.max.y, box.max.z}, {box.max.x, box.max.y, box.max.z}};
  constexpr uint32_t edge_corners[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                            {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
  for (const linalg::vec3f& corner : corners)
    consider(corner);
  for (const uint32_t (&edge)[2] : edge_corners)
  {
    const linalg::vec3f a     = corners[edge[0]];
    const linalg::vec3f along = corners[edge[1]] - a;
    for (const Plane& side : sides)
    {
      const float per_unit = linalg::dot(side.normal, along);
      if (std::abs(per_unit) < 1e-6f)
        continue;
      const float t = -linalg::dot(side.normal, a - side.point) / per_unit;
      if (t >= 0.f && t <= 1.f)
        consider(a + along * t);
    }
  }
  return any ? std::optional<aabb_bounds_t>(bounds) : std::optional<aabb_bounds_t>(box);
}

bool shadow_occluder_pyramid_contains_point(const shadow_occluder_t& occluder, const linalg::vec3f& point)
{
  for (uint32_t index = 0; index < occluder.pyramid_plane_count; ++index)
  {
    const Plane& plane = occluder.pyramid_planes[index];
    if (linalg::dot(point - plane.point, plane.normal) > 0.f)
      return false;
  }
  return true;
}

bool shadow_occluder_is_behind_point(const shadow_occluder_t& occluder, const linalg::vec3f& point)
{
  if (!shadow_occluder_pyramid_contains_point(occluder, point))
    return false;
  for (uint32_t index = 0; index < occluder.front_plane_count; ++index)
  {
    const Plane& plane = occluder.front_planes[index];
    if (linalg::dot(point - plane.point, plane.normal) > 0.f)
      return false;
  }
  return true;
}

bool shadow_volume_contains_point(const shadow_volume_t& volume, const linalg::vec3f& point)
{
  for (uint32_t index = 0; index < volume.side_plane_count; ++index)
  {
    const Plane& plane = volume.side_planes[index];
    if (linalg::dot(point - plane.point, plane.normal) > 0.f)
      return false;
  }
  for (uint32_t index = 0; index < volume.front_plane_count; ++index)
  {
    const Plane& plane = volume.front_planes[index];
    if (linalg::dot(point - plane.point, plane.normal) > 0.f)
      return false;
  }
  return true;
}

namespace
{

float box_reach_along(const linalg::vec3f& half_extents, const linalg::vec3f& normal)
{
  return half_extents.x * std::fabs(normal.x) + half_extents.y * std::fabs(normal.y) +
         half_extents.z * std::fabs(normal.z);
}

} // namespace

bool shadow_volume_touches_box(const shadow_volume_t& volume, const aabb_bounds_t& box)
{
  const linalg::vec3f center       = get_aabb_center(box);
  const linalg::vec3f half_extents = (box.max - box.min) * 0.5f;
  for (uint32_t index = 0; index < volume.side_plane_count; ++index)
  {
    const Plane& plane = volume.side_planes[index];
    if (linalg::dot(center - plane.point, plane.normal) > box_reach_along(half_extents, plane.normal))
      return false;
  }
  for (uint32_t index = 0; index < volume.front_plane_count; ++index)
  {
    const Plane& plane = volume.front_planes[index];
    if (linalg::dot(center - plane.point, plane.normal) > box_reach_along(half_extents, plane.normal))
      return false;
  }
  return true;
}

bool any_shadow_volume_touches_box(Span<const shadow_volume_t> volumes, const aabb_bounds_t& box)
{
  for (const shadow_volume_t& volume : volumes)
    if (volume.cuts_geometry && shadow_volume_touches_box(volume, box))
      return true;
  return false;
}

bool shadow_volume_contains_box(const shadow_volume_t& volume, const aabb_bounds_t& box)
{
  for (uint32_t corner_index = 0; corner_index < 8; ++corner_index)
  {
    const linalg::vec3f corner = {(corner_index & 1) != 0 ? box.max.x : box.min.x,
                                  (corner_index & 2) != 0 ? box.max.y : box.min.y,
                                  (corner_index & 4) != 0 ? box.max.z : box.min.z};
    if (!shadow_volume_contains_point(volume, corner))
      return false;
  }
  return true;
}

bool any_shadow_volume_contains_box(Span<const shadow_volume_t> volumes, const aabb_bounds_t& box)
{
  for (const shadow_volume_t& volume : volumes)
    if (volume.cuts_geometry && shadow_volume_contains_box(volume, box))
      return true;
  return false;
}

bool geometry_owner_receives_shadow(const entities::Geometry_Owner_Entity& owner)
{
  return owner.solid_only_in_shadow || owner.erased_in_shadow;
}

std::string describe_shadow_volume_report(const shadow_volume_report_t& report, Span<const shadow_volume_t> kept)
{
  std::string text = std::format(
      "shadow volumes: {} switched-on lights cut geometry, {} receiver pieces, {} caster pieces; {} cast, {} kept "
      "({} refused: light inside the caster, {} refused: open silhouette, {} refused: light on the silhouette, "
      "{} refused: more than {} silhouette edges or {} faces toward the light, "
      "{} refused: outside the spot's beam, {} dropped: reaching no receiver)",
      report.cutting_lights, report.receiver_pieces, report.caster_pieces, report.cast, report.kept,
      report.refused_inside_caster, report.refused_open_silhouette, report.refused_on_silhouette,
      report.refused_too_many_planes, MAX_SHADOW_VOLUME_SIDES,
      MAX_SHADOW_VOLUME_FRONT_PLANES, report.refused_outside_beam, report.culled_reaching_nothing);
  if (report.cutting_lights == 0)
    text += "\n  no light has cuts_geometry set while switched on: nothing casts";
  if (report.receiver_pieces == 0)
    text += "\n  no switched-on Geometry_Owner_Entity with solid_only_in_shadow or erased_in_shadow owns a brush: nothing receives";
  for (const shadow_volume_t& volume : kept)
  {
    linalg::vec3f near_center{0.f, 0.f, 0.f};
    linalg::vec3f far_center{0.f, 0.f, 0.f};
    for (uint32_t side = 0; side < volume.side_count; ++side)
    {
      near_center = near_center + volume.near_ring[side];
      far_center  = far_center + volume.far_ring[side];
    }
    near_center = near_center * (1.f / static_cast<float>(std::max(volume.side_count, 1u)));
    far_center  = far_center * (1.f / static_cast<float>(std::max(volume.side_count, 1u)));
    text += std::format("\n  caster {} (0 is an unowned brush) from light {}: {} sides, {} front planes, silhouette about "
                        "({:.0f} {:.0f} {:.0f}), far ring about ({:.0f} {:.0f} {:.0f})",
                        volume.caster, volume.light, volume.side_count, volume.front_plane_count, near_center.x,
                        near_center.y, near_center.z, far_center.x, far_center.y, far_center.z);
  }
  for (const refused_piece_t& refused : report.refused)
    text += std::format("\n  piece {} about ({:.0f} {:.0f} {:.0f}) refused under light {}: {}", refused.piece,
                        refused.center.x, refused.center.y, refused.center.z, refused.light,
                        describe_shadow_cast_refusal(refused.reason));
  return text;
}

const char* describe_shadow_cast_refusal(shadow_cast_refusal_t refusal)
{
  switch (refusal)
  {
    case shadow_cast_refusal_t::None: return "cast";
    case shadow_cast_refusal_t::Light_Inside_Caster: return "light inside the caster";
    case shadow_cast_refusal_t::Silhouette_Open: return "open silhouette (face polygons do not meet at their corners)";
    case shadow_cast_refusal_t::Light_On_Silhouette: return "light on the silhouette";
    case shadow_cast_refusal_t::Too_Many_Planes: return "too many planes";
    case shadow_cast_refusal_t::Outside_Spot_Beam: return "outside the spot's beam";
  }
  return "cast";
}

namespace
{

std::vector<shadow_light_t> gather_shadow_lights(const Entity_System& system, Span<const mover_t> movers,
                                                 const mover_rests_t& rests)
{
  std::vector<shadow_light_t> lights;
  const auto                  gather = [&]<typename Light_T>()
  {
    for (const Light_T& entity : system.entities_of_type<Light_T>())
    {
      const path_pose_t      placed = get_placed_pose_for_entity(entity);
      const entities::Rides* rides  = entities::get_rides(&entity);
      const path_pose_t pose = rides != nullptr ? ridden_pose_in_cut(movers, rests, placed, *rides) : placed;
      if (const std::optional<shadow_light_t> light = try_shadow_light_from_entity(entity, pose))
        lights.push_back(*light);
    }
  };
  gather.template operator()<entities::Point_Light_Entity>();
  gather.template operator()<entities::Spot_Light_Entity>();
  gather.template operator()<entities::Directional_Light_Entity>();
  return lights;
}

// Erased receives and casts from behind itself; Receives (solid only in shadow) never casts.
enum class piece_role_t : uint8_t
{
  Casts,
  Receives,
  Erased,
  Nothing
};

bool piece_role_receives(piece_role_t role)
{
  return role == piece_role_t::Receives || role == piece_role_t::Erased;
}

// Owners resolved once per geometry index rather than once per primitive, since a brush is several.
std::vector<piece_role_t> resolve_piece_roles(const Entity_System& system, Span<const entity_uid_t> owner_of)
{
  std::vector<piece_role_t> role_of(owner_of.size(), piece_role_t::Casts);
  for (uint32_t index = 0; index < owner_of.size(); ++index)
  {
    if (owner_of[index] == null_entity_uid)
      continue;
    const entities::Geometry_Owner_Entity* owner =
        entities::entity_as<entities::Geometry_Owner_Entity>(system.try_find(owner_of[index]));
    if (owner == nullptr)
      continue;
    if (!owner->switch_state.value)
      role_of[index] = piece_role_t::Nothing;
    else if (owner->erased_in_shadow)
      role_of[index] = piece_role_t::Erased;
    else if (owner->solid_only_in_shadow)
      role_of[index] = piece_role_t::Receives;
  }
  return role_of;
}

struct visited_piece_t
{
  uint32_t                              key;
  piece_role_t                          role;
  entity_uid_t                          owner;
  aabb_bounds_t                         bounds;
  Span<const Plane>                     planes;
  Span<const std::vector<linalg::vec3>> polygons;
};

// Every piece in the one order both collects agree on, keyed by its place in it: the static pieces
// in the BVH's order, then every mover's pieces. A mover's pieces cast and occlude; none receives. An
// erased receiver casts from behind itself. A solid beam is a mover and is light: it casts no shadow and
// stops none, whichever collect it is in by.
template <typename Visit_T>
void for_each_shadow_piece(const Entity_System& system, const Bounding_Volume_Hierarchy& bvh,
                           Span<const entity_uid_t> owner_of, Span<const piece_role_t> role_of,
                           Span<const mover_t> movers, Visit_T&& visit)
{
  uint32_t key = 0;
  for (const BVH_Primitive& primitive : bvh.primitives)
  {
    if (primitive.id.type != Collision_Id::Type::Static_Geometry || primitive.id.index >= role_of.size())
      continue;
    visit(visited_piece_t{.key      = key++,
                          .role     = role_of[primitive.id.index],
                          .owner    = owner_of[primitive.id.index],
                          .bounds   = primitive.aabb,
                          .planes   = primitive.collision_planes,
                          .polygons = primitive.face_polygons});
  }
  for (const mover_t& mover : movers)
  {
    if (entities::entity_as<entities::Spot_Light_Entity>(system.try_find(mover.uid)) != nullptr)
      continue;
    for (const collision_piece_t& piece : mover.pieces)
      visit(visited_piece_t{.key      = key++,
                            .role     = piece_role_t::Casts,
                            .owner    = mover.uid,
                            .bounds   = piece.bounds,
                            .planes   = piece.planes,
                            .polygons = piece.face_polygons});
  }
}

} // namespace

shadow_volume_report_t collect_shadow_volumes(const Entity_System& system, const Bounding_Volume_Hierarchy& bvh,
                                              Span<const entity_uid_t> owner_of, Span<const mover_t> movers,
                                              const mover_rests_t& rests, std::vector<shadow_volume_t>& out)
{
  out.clear();
  shadow_volume_report_t report;

  const std::vector<shadow_light_t> lights  = gather_shadow_lights(system, movers, rests);
  report.cutting_lights                     = static_cast<uint32_t>(lights.size());
  const std::vector<piece_role_t>   role_of = resolve_piece_roles(system, owner_of);

  // Keyed as for_each_shadow_piece keys the static pieces, so an erased caster can leave itself out.
  struct receiver_t
  {
    uint32_t      key;
    aabb_bounds_t bounds;
  };
  std::vector<receiver_t> receivers;
  uint32_t                static_key = 0;
  for (const BVH_Primitive& primitive : bvh.primitives)
  {
    if (primitive.id.type != Collision_Id::Type::Static_Geometry || primitive.id.index >= role_of.size())
      continue;
    if (piece_role_receives(role_of[primitive.id.index]))
      receivers.push_back({.key = static_key, .bounds = primitive.aabb});
    ++static_key;
  }
  report.receiver_pieces = static_cast<uint32_t>(receivers.size());

  bool any_light_keeps_every_volume = false;
  for (const shadow_light_t& light : lights)
    any_light_keeps_every_volume = any_light_keeps_every_volume || light.keeps_every_volume;
  if (lights.empty() || (receivers.empty() && !any_light_keeps_every_volume))
    return report;

  std::vector<shadow_volume_t> behind;

  for_each_shadow_piece(system, bvh, owner_of, role_of, movers, [&](const visited_piece_t& piece)
  {
    if (piece.role != piece_role_t::Casts && piece.role != piece_role_t::Erased)
      return;
    ++report.caster_pieces;
    for (const shadow_light_t& light : lights)
    {
      shadow_cast_t cast;
      behind.clear();
      if (piece.role == piece_role_t::Erased)
        cast.refusal = cast_shadow_volumes_behind_caster(light, piece.planes, piece.polygons, piece.owner, behind);
      else
      {
        cast = cast_shadow_volume(light, piece.planes, piece.polygons, piece.owner);
        if (cast.refusal == shadow_cast_refusal_t::None)
          behind.push_back(cast.volume);
      }
      switch (cast.refusal)
      {
        case shadow_cast_refusal_t::None: break;
        case shadow_cast_refusal_t::Light_Inside_Caster: ++report.refused_inside_caster; break;
        case shadow_cast_refusal_t::Silhouette_Open: ++report.refused_open_silhouette; break;
        case shadow_cast_refusal_t::Light_On_Silhouette: ++report.refused_on_silhouette; break;
        case shadow_cast_refusal_t::Too_Many_Planes: ++report.refused_too_many_planes; break;
        case shadow_cast_refusal_t::Outside_Spot_Beam: ++report.refused_outside_beam; break;
      }
      if (cast.refusal != shadow_cast_refusal_t::None)
      {
        if (cast.refusal != shadow_cast_refusal_t::Outside_Spot_Beam)
          report.refused.push_back({.piece  = piece.key,
                                    .light  = light.uid,
                                    .center = get_aabb_center(piece.bounds),
                                    .reason = cast.refusal});
        continue;
      }
      for (shadow_volume_t& volume : behind)
      {
        ++report.cast;
        volume.caster_piece = piece.key;
        bool reaches_a_receiver = false;
        for (const receiver_t& receiver : receivers)
          if (receiver.key != piece.key && shadow_volume_touches_box(volume, receiver.bounds))
          {
            reaches_a_receiver = true;
            break;
          }
        if (!reaches_a_receiver && !light.keeps_every_volume)
        {
          ++report.culled_reaching_nothing;
          continue;
        }
        volume.reaches_receiver = reaches_a_receiver;
        out.push_back(volume);
      }
    }
  });

  // The volumes that change the floor first; the rest dealt round-robin by light, each light's nearest
  // caster first, so the renderer's slots hold a few casters of every beam before many of one.
  const std::vector<shadow_volume_t>::iterator first_drawn_only =
      std::stable_partition(out.begin(), out.end(), [](const shadow_volume_t& volume)
                            { return volume.cuts_geometry && volume.reaches_receiver; });
  const auto caster_distance = [](const shadow_volume_t& volume)
  { return linalg::length(get_aabb_center(volume.caster_bounds) - volume.light_apex); };
  std::stable_sort(first_drawn_only, out.end(), [&](const shadow_volume_t& a, const shadow_volume_t& b)
                   { return a.light != b.light ? a.light < b.light : caster_distance(a) < caster_distance(b); });
  std::vector<uint32_t> rank_within_light;
  rank_within_light.reserve(static_cast<size_t>(out.end() - first_drawn_only));
  for (std::vector<shadow_volume_t>::iterator volume = first_drawn_only; volume != out.end(); ++volume)
    rank_within_light.push_back(volume == first_drawn_only || (volume - 1)->light != volume->light
                                    ? 0u
                                    : rank_within_light.back() + 1u);
  std::vector<shadow_volume_t> dealt(first_drawn_only, out.end());
  std::vector<uint32_t>        order(dealt.size());
  for (uint32_t index = 0; index < order.size(); ++index)
    order[index] = index;
  std::stable_sort(order.begin(), order.end(),
                   [&](uint32_t a, uint32_t b) { return rank_within_light[a] < rank_within_light[b]; });
  for (uint32_t index = 0; index < order.size(); ++index)
    *(first_drawn_only + index) = dealt[order[index]];
  report.kept = static_cast<uint32_t>(out.size());
  return report;
}

uint32_t collect_shadow_occluders(const Entity_System& system, const Bounding_Volume_Hierarchy& bvh,
                                  Span<const entity_uid_t> owner_of, Span<const mover_t> movers,
                                  const mover_rests_t& rests, Span<const shadow_volume_t> drawn,
                                  std::vector<shadow_occluder_t>& out)
{
  out.clear();
  if (drawn.size() > 31)
    fatal_error("collect_shadow_occluders: {} drawn volumes, but volume_bits holds 31", drawn.size());
  if (drawn.empty())
    return 0;

  const std::vector<shadow_light_t> lights  = gather_shadow_lights(system, movers, rests);
  const std::vector<piece_role_t>   role_of = resolve_piece_roles(system, owner_of);
  uint32_t                          skipped = 0;

  for_each_shadow_piece(system, bvh, owner_of, role_of, movers, [&](const visited_piece_t& piece)
  {
    if (piece.role == piece_role_t::Nothing)
      return;
    for (const shadow_light_t& light : lights)
    {
      uint32_t bits = 0;
      for (uint32_t index = 0; index < drawn.size(); ++index)
      {
        const shadow_volume_t& volume = drawn[index];
        if (volume.light != light.uid || volume.caster_piece == piece.key)
          continue;
        if (shadow_volume_touches_box(volume, piece.bounds))
          bits |= 1u << index;
      }
      if (bits == 0)
        continue;
      if (const std::optional<shadow_occluder_t> occluder = try_cast_shadow_occluder(
              light, piece.planes, piece.polygons, bits, piece_role_receives(piece.role)))
        out.push_back(*occluder);
      else
        ++skipped;
    }
  });
  return skipped;
}

} // namespace shared
