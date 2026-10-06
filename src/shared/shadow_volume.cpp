#include "shadow_volume.hpp"

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
    if (!spot->light.cuts_geometry || !light_is_switched_on(entity))
      return std::nullopt;
    return shadow_light_t{.uid                   = spot->entity_id,
                          .apex                  = pose.position,
                          .direction             = linalg::normalize(linalg::basis_from(pose.orientation).forward),
                          .range                 = spot->range,
                          .cosine_of_outer_angle = std::cos(linalg::to_radians(spot->outer_degrees))};
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
    return refused(shadow_cast_refusal_t::Light_Beside_Caster);
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

  // A face is BACK when it faces away from the light. Every face away is the light inside the piece.
  std::vector<bool> faces_away(piece_planes.size(), false);
  for (uint32_t index = 0; index < piece_planes.size(); ++index)
  {
    const Plane& plane = piece_planes[index];
    faces_away[index]  = light.directional ? linalg::dot(plane.normal, light.direction) > 0.f
                                           : linalg::dot(plane.normal, plane.point - light.apex) > 0.f;
    if (!faces_away[index])
      continue;
    if (volume.back_plane_count == MAX_SHADOW_VOLUME_BACK_PLANES)
      return refused(shadow_cast_refusal_t::Too_Many_Planes);
    volume.back_planes[volume.back_plane_count++] = plane;
  }
  if (volume.back_plane_count == 0 || volume.back_plane_count == piece_planes.size())
    return refused(shadow_cast_refusal_t::Light_Beside_Caster);

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
    return refused(shadow_cast_refusal_t::Light_Beside_Caster);
  if (edges.size() > MAX_SHADOW_VOLUME_SIDES)
    return refused(shadow_cast_refusal_t::Too_Many_Planes);

  for (const silhouette_edge_t& edge : edges)
  {
    if (!light.directional && linalg::length(edge.a - light.apex) < MIN_DEPTH)
      return refused(shadow_cast_refusal_t::Light_Beside_Caster);
    const linalg::vec3f normal = linalg::cross(edge.b - edge.a, ray_through(edge.a));
    if (linalg::length(normal) < 1e-6f)
      return refused(shadow_cast_refusal_t::Light_Beside_Caster);
    volume.side_planes[volume.side_plane_count++] = plane_outward_from(edge.a, normal, center);
  }

  // The beam clips the pyramid. Both pass through the apex, so a cone plane every silhouette corner is
  // inside of has every ray inside of it too and is left out; one some corner is outside of is a side.
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

  if (!light.directional && light.range > 0.f)
  {
    const linalg::vec3f axis = linalg::normalize(center - light.apex);
    volume.side_planes[volume.side_plane_count++] = {.point = light.apex + axis * light.range, .normal = axis};
  }

  // The rings for the debug draw: the silhouette chained into its loop (face windings need not agree,
  // so an edge is taken either way round), and each corner down its ray.
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

bool shadow_volume_contains_point(const shadow_volume_t& volume, const linalg::vec3f& point)
{
  for (uint32_t index = 0; index < volume.side_plane_count; ++index)
  {
    const Plane& plane = volume.side_planes[index];
    if (linalg::dot(point - plane.point, plane.normal) > 0.f)
      return false;
  }
  for (uint32_t index = 0; index < volume.back_plane_count; ++index)
  {
    const Plane& plane = volume.back_planes[index];
    if (linalg::dot(point - plane.point, plane.normal) >= 0.f)
      return true;
  }
  return false;
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
  for (uint32_t index = 0; index < volume.back_plane_count; ++index)
  {
    const Plane& plane = volume.back_planes[index];
    if (linalg::dot(center - plane.point, plane.normal) + box_reach_along(half_extents, plane.normal) >= 0.f)
      return true;
  }
  return false;
}

bool any_shadow_volume_touches_box(Span<const shadow_volume_t> volumes, const aabb_bounds_t& box)
{
  for (const shadow_volume_t& volume : volumes)
    if (shadow_volume_touches_box(volume, box))
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
    if (shadow_volume_contains_box(volume, box))
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
      "({} refused: light beside or inside the caster, {} refused: more than {} silhouette edges or {} back planes, "
      "{} refused: outside the spot's beam, {} dropped: reaching no receiver)",
      report.cutting_lights, report.receiver_pieces, report.caster_pieces, report.cast, report.kept,
      report.refused_beside_light, report.refused_too_many_planes, MAX_SHADOW_VOLUME_SIDES,
      MAX_SHADOW_VOLUME_BACK_PLANES, report.refused_outside_beam, report.culled_reaching_nothing);
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
    text += std::format("\n  caster {} (0 is an unowned brush) from light {}: {} sides, {} back planes, silhouette about "
                        "({:.0f} {:.0f} {:.0f}), far ring about ({:.0f} {:.0f} {:.0f})",
                        volume.caster, volume.light, volume.side_count, volume.back_plane_count, near_center.x,
                        near_center.y, near_center.z, far_center.x, far_center.y, far_center.z);
  }
  return text;
}

shadow_volume_report_t collect_shadow_volumes(const Entity_System& system, const Bounding_Volume_Hierarchy& bvh,
                                              Span<const entity_uid_t> owner_of, Span<const mover_t> movers,
                                              const mover_rests_t& rests, std::vector<shadow_volume_t>& out)
{
  out.clear();
  shadow_volume_report_t report;

  std::vector<shadow_light_t> lights;
  const auto                  gather_lights = [&]<typename Light_T>()
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
  gather_lights.template operator()<entities::Point_Light_Entity>();
  gather_lights.template operator()<entities::Spot_Light_Entity>();
  gather_lights.template operator()<entities::Directional_Light_Entity>();
  report.cutting_lights = static_cast<uint32_t>(lights.size());

  // Owners resolved once per geometry index rather than once per primitive, since a brush is several.
  enum class piece_role_t : uint8_t
  {
    Casts,
    Receives,
    Nothing
  };
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
    else if (geometry_owner_receives_shadow(*owner))
      role_of[index] = piece_role_t::Receives;
  }

  std::vector<aabb_bounds_t> receiver_bounds;
  for (const BVH_Primitive& primitive : bvh.primitives)
    if (primitive.id.type == Collision_Id::Type::Static_Geometry && primitive.id.index < role_of.size() &&
        role_of[primitive.id.index] == piece_role_t::Receives)
      receiver_bounds.push_back(primitive.aabb);
  report.receiver_pieces = static_cast<uint32_t>(receiver_bounds.size());

  if (lights.empty() || receiver_bounds.empty())
    return report;

  const auto cast_from_every_light = [&](Span<const Plane> piece_planes,
                                         Span<const std::vector<linalg::vec3>> corner_polygons,
                                         entity_uid_t                           caster)
  {
    ++report.caster_pieces;
    for (const shadow_light_t& light : lights)
    {
      const shadow_cast_t cast = cast_shadow_volume(light, piece_planes, corner_polygons, caster);
      switch (cast.refusal)
      {
        case shadow_cast_refusal_t::None: break;
        case shadow_cast_refusal_t::Light_Beside_Caster: ++report.refused_beside_light; continue;
        case shadow_cast_refusal_t::Too_Many_Planes: ++report.refused_too_many_planes; continue;
        case shadow_cast_refusal_t::Outside_Spot_Beam: ++report.refused_outside_beam; continue;
      }
      ++report.cast;

      bool reaches_a_receiver = false;
      for (const aabb_bounds_t& bounds : receiver_bounds)
        if (shadow_volume_touches_box(cast.volume, bounds))
        {
          reaches_a_receiver = true;
          break;
        }
      if (!reaches_a_receiver)
      {
        ++report.culled_reaching_nothing;
        continue;
      }
      out.push_back(cast.volume);
    }
  };

  for (const BVH_Primitive& primitive : bvh.primitives)
  {
    if (primitive.id.type != Collision_Id::Type::Static_Geometry || primitive.id.index >= role_of.size() ||
        role_of[primitive.id.index] != piece_role_t::Casts)
      continue;
    cast_from_every_light(primitive.collision_planes, primitive.face_polygons, owner_of[primitive.id.index]);
  }

  for (const mover_t& mover : movers)
    for (const collision_piece_t& piece : mover.pieces)
      cast_from_every_light(piece.planes, piece.face_polygons, mover.uid);

  report.kept = static_cast<uint32_t>(out.size());
  return report;
}

} // namespace shared
