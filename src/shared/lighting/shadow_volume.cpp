#include "shadow_volume.hpp"

#include "player_shadow_casters.hpp"

#include "collision/collision_detection.hpp"
#include "geometry/convex_decomposition.hpp"
#include "entities/generated/entities/directional_light_entity_generated.hpp"
#include "entities/generated/entities/geometry_owner_entity_generated.hpp"
#include "entities/generated/entities/point_light_entity_generated.hpp"
#include "entities/generated/entities/projected_image_entity_generated.hpp"
#include "entities/generated/entities/projector_entity_generated.hpp"
#include "entities/generated/entities/spot_light_entity_generated.hpp"
#include "entity_system.hpp"
#include "lighting.hpp"
#include "log.hpp"
#include "projected_image.hpp"
#include "projector_mask.hpp"
#include "reveal_light.hpp"
#include "solid_shadows.hpp"

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
    const bool point_is_solid = point->light.solid_shadows != entities::Solid_Shadows::None;
    if ((!point->light.cuts_geometry && !point_is_solid) || !light_is_switched_on(entity))
      return std::nullopt;
    return shadow_light_t{.uid                = point->entity_id,
                          .apex               = pose.position,
                          .range              = point->range,
                          .cuts_geometry      = point->light.cuts_geometry,
                          .keeps_every_volume = point_is_solid,
                          .solid_shadows      = point->light.solid_shadows};
  }
  if (const entities::Spot_Light_Entity* spot = entities::entity_as<entities::Spot_Light_Entity>(&entity))
  {
    const bool spot_is_solid = spot->light.solid_shadows != entities::Solid_Shadows::None;
    if ((!spot->light.cuts_geometry && !spot_is_solid && !spot->beam && !spot->solid_beam) ||
        !light_is_switched_on(entity))
      return std::nullopt;
    return shadow_light_t{.uid                   = spot->entity_id,
                          .apex                  = pose.position,
                          .direction             = linalg::normalize(linalg::basis_from(pose.orientation).forward),
                          .range                 = spot->range,
                          .cosine_of_outer_angle = std::cos(linalg::to_radians(spot->outer_degrees)),
                          .cuts_geometry         = spot->light.cuts_geometry,
                          .keeps_every_volume    = spot->beam || spot->solid_beam || spot_is_solid,
                          .solid_shadows         = spot->light.solid_shadows};
  }
  if (const entities::Projector_Entity* projector = entities::entity_as<entities::Projector_Entity>(&entity))
  {
    const bool projector_is_solid = projector->light.solid_shadows != entities::Solid_Shadows::None;
    if ((!projector->light.cuts_geometry && !projector_is_solid) || !light_is_switched_on(entity))
      return std::nullopt;
    const std::optional<scene_light_t> scene_light = try_convert_light_entity_to_scene_light(entity, pose);
    shadow_light_t light{.uid                            = projector->entity_id,
                         .apex                           = pose.position,
                         .direction                      = linalg::normalize(scene_light->forward),
                         .range                          = projector->range,
                         .frame_planes                   = compute_projector_frame_planes(*scene_light),
                         .frame_plane_count              = 4,
                         .frame_corners_at_unit_distance = compute_projector_frame_corners(*scene_light, 1.f),
                         .cuts_geometry                  = projector->light.cuts_geometry,
                         .keeps_every_volume             = projector_is_solid,
                         .solid_shadows                  = projector->light.solid_shadows};
    return light;
  }
  if (const entities::Directional_Light_Entity* directional =
          entities::entity_as<entities::Directional_Light_Entity>(&entity))
  {
    entities::Solid_Shadows directional_solid = directional->light.solid_shadows;
    if (directional_solid == entities::Solid_Shadows::Solid_From_Lens_To_Shadow)
    {
      log_warning("[shadow volumes] directional light {} asks for Solid_From_Lens_To_Shadow and has no lens: "
                  "its dark is Shadow_Volumes_Only",
                  directional->entity_id);
      directional_solid = entities::Solid_Shadows::Shadow_Volumes_Only;
    }
    const bool directional_is_solid = directional_solid != entities::Solid_Shadows::None;
    if ((!directional->light.cuts_geometry && !directional_is_solid) || !light_is_switched_on(entity))
      return std::nullopt;
    return shadow_light_t{.uid                = directional->entity_id,
                          .directional        = true,
                          .direction          = linalg::normalize(linalg::basis_from(pose.orientation).forward),
                          .cuts_geometry      = directional->light.cuts_geometry,
                          .keeps_every_volume = directional_is_solid,
                          .solid_shadows      = directional_solid};
  }
  return std::nullopt;
}

// A spot or a projector: its reach is measured along `direction`, and its sides cut what it throws.
static bool shadow_light_is_directed(const shadow_light_t& light)
{
  return light.cosine_of_outer_angle > -1.f || light.frame_plane_count > 0;
}

std::optional<Plane> try_shadow_light_far_cap(const shadow_light_t& light, const linalg::vec3f& center)
{
  if (light.directional || light.range <= 0.f)
    return std::nullopt;
  const linalg::vec3f axis =
      linalg::normalize(shadow_light_is_directed(light) ? light.direction : center - light.apex);
  return Plane{.point = light.apex + axis * light.range, .normal = axis};
}

namespace
{

// Within the frame's reach and not wholly outside any of its planes: reveal_cone_touches_box for a frame.
bool projector_frame_touches_box(const shadow_light_t& light, const aabb_bounds_t& box)
{
  const linalg::vec3f nearest = {std::clamp(light.apex.x, box.min.x, box.max.x),
                                 std::clamp(light.apex.y, box.min.y, box.max.y),
                                 std::clamp(light.apex.z, box.min.z, box.max.z)};
  if (light.range > 0.f && linalg::length(nearest - light.apex) > light.range)
    return false;
  const linalg::vec3f center       = get_aabb_center(box);
  const linalg::vec3f half_extents = (box.max - box.min) * 0.5f;
  for (uint32_t index = 0; index < light.frame_plane_count; ++index)
  {
    const Plane& side  = light.frame_planes[index];
    const float  reach = half_extents.x * std::fabs(side.normal.x) + half_extents.y * std::fabs(side.normal.y) +
                        half_extents.z * std::fabs(side.normal.z);
    if (linalg::dot(center - side.point, side.normal) > reach)
      return false;
  }
  return true;
}

} // namespace

shadow_light_clip_t compute_shadow_light_clip(const shadow_light_t& light)
{
  shadow_light_clip_t clip;
  if (light.cosine_of_outer_angle > -1.f)
  {
    clip.beam = planes_of_reveal_cone({.apex                 = light.apex,
                                       .axis                 = light.direction,
                                       .range                = light.range > 0.f ? light.range : FLT_MAX,
                                       .cosine_of_half_angle = light.cosine_of_outer_angle});
    clip.sides      = clip.beam->sides;
    clip.side_count = REVEAL_CONE_SIDE_COUNT;
  }
  else if (light.frame_plane_count > 0)
  {
    for (uint32_t index = 0; index < light.frame_plane_count; ++index)
      clip.sides[index] = light.frame_planes[index];
    clip.side_count = light.frame_plane_count;
  }
  return clip;
}

bool shadow_light_touches_box(const shadow_light_t& light, const shadow_light_clip_t& clip, const aabb_bounds_t& box)
{
  if (clip.beam)
    return reveal_cone_touches_box(*clip.beam, box);
  if (clip.side_count > 0)
    return projector_frame_touches_box(light, box);
  return true;
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
  const shadow_light_clip_t clip = compute_shadow_light_clip(light);
  if (!shadow_light_touches_box(light, clip, piece_bounds))
    return refused(shadow_cast_refusal_t::Outside_Spot_Beam);
  const Span<const Plane> clip_sides(clip.sides.data, clip.side_count);
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
  volume.solid_shadows    = light.solid_shadows;
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
    if (volume.light_facing_plane_count == MAX_SHADOW_VOLUME_LIGHT_FACING_PLANES)
      return refused(shadow_cast_refusal_t::Too_Many_Planes);
    volume.light_facing_planes[volume.light_facing_plane_count++] = plane;
  }
  if (volume.light_facing_plane_count == 0 || volume.light_facing_plane_count == piece_planes.size())
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

  // The beam or the frame clips the pyramid, last: these planes are the beam's own surface, and the beam pass
  // leaves them out of its margins (a chord ending on the beam's surface is not at a shadow's edge). Both pass
  // through the apex, so a cone plane every silhouette corner is inside of has every ray inside of it too and
  // is left out; one some corner is outside of is a side.
  {
    for (const Plane& cone_side : clip_sides)
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
    beyond.light_facing_planes[0]   = {.point = plane.point, .normal = plane.normal * -1.f};
    beyond.light_facing_plane_count = 1;
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

bool shadow_volume_contains_point(const shadow_volume_t& volume, const linalg::vec3f& point)
{
  for (uint32_t index = 0; index < volume.side_plane_count; ++index)
  {
    const Plane& plane = volume.side_planes[index];
    if (linalg::dot(point - plane.point, plane.normal) > 0.f)
      return false;
  }
  for (uint32_t index = 0; index < volume.light_facing_plane_count; ++index)
  {
    const Plane& plane = volume.light_facing_planes[index];
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
  for (uint32_t index = 0; index < volume.light_facing_plane_count; ++index)
  {
    const Plane& plane = volume.light_facing_planes[index];
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
      "shadow volumes: {} switched-on lights cut geometry, {} receiver pieces, {} caster pieces, {} picture mask "
      "pieces, {} player pieces; {} cast, {} kept "
      "({} refused: light inside the caster, {} refused: open silhouette, {} refused: light on the silhouette, "
      "{} refused: more than {} silhouette edges or {} faces toward the light, "
      "{} refused: outside the spot's beam, {} dropped: reaching no receiver)",
      report.cutting_lights, report.receiver_pieces, report.caster_pieces, report.mask_pieces, report.player_pieces,
      report.cast, report.kept,
      report.refused_inside_caster, report.refused_open_silhouette, report.refused_on_silhouette,
      report.refused_too_many_planes, MAX_SHADOW_VOLUME_SIDES,
      MAX_SHADOW_VOLUME_LIGHT_FACING_PLANES, report.refused_outside_beam, report.culled_reaching_nothing);
  if (report.cutting_lights == 0)
    text += "\n  no switched-on light has cuts_geometry or solid_shadows set, or a beam: nothing casts";
  if (report.receiver_pieces == 0)
    text += "\n  no switched-on Geometry_Owner_Entity with solid_only_in_shadow or erased_in_shadow owns geometry: nothing receives";
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
    text += std::format("\n  caster {} (0 is unowned geometry) from light {}: {} sides, {} front planes, silhouette about "
                        "({:.0f} {:.0f} {:.0f}), far ring about ({:.0f} {:.0f} {:.0f})",
                        volume.caster, volume.light, volume.side_count, volume.light_facing_plane_count, near_center.x,
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
    case shadow_cast_refusal_t::Outside_Spot_Beam: return "outside the spot's beam or the projector's frame";
  }
  return "cast";
}

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
      std::optional<shadow_light_t> light = try_shadow_light_from_entity(entity, pose);
      if (!light)
        continue;
      if constexpr (std::is_same_v<Light_T, entities::Projector_Entity>)
        if (const entities::Projected_Image_Entity* image =
                find_projected_image_for_projector(system, entity.entity_id))
          light->image = image->entity_id;
      lights.push_back(*light);
    }
  };
  gather.template operator()<entities::Point_Light_Entity>();
  gather.template operator()<entities::Spot_Light_Entity>();
  gather.template operator()<entities::Projector_Entity>();
  gather.template operator()<entities::Directional_Light_Entity>();
  return lights;
}

namespace
{

// Under a Solid_From_Lens_To_Shadow light a Casts_From_Lens piece's volume is solid from the lens, where a plain Casts
// piece (the room) is solid behind itself alone (solid_shadows.hpp).
bool piece_role_casts(piece_role_t role)
{
  return role == piece_role_t::Casts || role == piece_role_t::Casts_From_Lens;
}

bool piece_role_receives(piece_role_t role)
{
  return role == piece_role_t::Receives || role == piece_role_t::Erased;
}

bool same_vec3(const linalg::vec3f& a, const linalg::vec3f& b)
{
  return a.x == b.x && a.y == b.y && a.z == b.z;
}

// Bit for bit: what a memo was built for against the light this cut gathered.
bool same_shadow_light(const shadow_light_t& a, const shadow_light_t& b)
{
  if (a.uid != b.uid || a.directional != b.directional || !same_vec3(a.apex, b.apex) ||
      !same_vec3(a.direction, b.direction) || a.range != b.range ||
      a.cosine_of_outer_angle != b.cosine_of_outer_angle || a.frame_plane_count != b.frame_plane_count ||
      a.image != b.image || a.cuts_geometry != b.cuts_geometry || a.keeps_every_volume != b.keeps_every_volume ||
      a.solid_shadows != b.solid_shadows)
    return false;
  for (uint32_t index = 0; index < a.frame_plane_count; ++index)
    if (!same_vec3(a.frame_planes[index].point, b.frame_planes[index].point) ||
        !same_vec3(a.frame_planes[index].normal, b.frame_planes[index].normal) ||
        !same_vec3(a.frame_corners_at_unit_distance[index], b.frame_corners_at_unit_distance[index]))
      return false;
  return true;
}

bool same_planes(Span<const Plane> a, Span<const Plane> b)
{
  if (a.count != b.count)
    return false;
  for (uint32_t index = 0; index < a.count; ++index)
    if (!same_vec3(a[index].point, b[index].point) || !same_vec3(a[index].normal, b[index].normal))
      return false;
  return true;
}

// A memo's counts into this computation's: everything an entry counted when built, which is every count but the
// computation's own (its lights, its caster and player pieces, its receivers and what it kept).
void add_shadow_volume_report_counts(shadow_volume_report_t& into, const shadow_volume_report_t& still)
{
  into.mask_pieces += still.mask_pieces;
  into.cast += still.cast;
  into.refused_inside_caster += still.refused_inside_caster;
  into.refused_open_silhouette += still.refused_open_silhouette;
  into.refused_on_silhouette += still.refused_on_silhouette;
  into.refused_too_many_planes += still.refused_too_many_planes;
  into.refused_outside_beam += still.refused_outside_beam;
  into.culled_reaching_nothing += still.culled_reaching_nothing;
  into.refused.insert(into.refused.end(), still.refused.begin(), still.refused.end());
}

// Owners resolved once per geometry index rather than once per primitive, since a mesh is several pieces.
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
    else if (owner->casts_solid_from_lens)
      role_of[index] = piece_role_t::Casts_From_Lens;
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
  // A player's piece: cast into the solid_shadows lights alone.
  bool                                  casts_only_into_solid_shadows = false;
  // A moving piece's index within its mover or player: with the owner, its name in the moving memo.
  uint32_t                              index_in_owner = 0;
};

// The pieces in the one order both collects agree on, keyed by their place in it (the key is the readout's and the
// erased receiver's self-exclusion, nothing else's): the still pieces in the BVH's order, then every mover's
// pieces, then every player's. A mover's pieces cast and occlude; none receives. An erased receiver casts from
// behind itself. A mover that is a light's (a solid beam, a solid shadow) is light or dark: it casts no shadow and
// stops none, whichever collect it is in by.
template <typename Visit_T>
void for_each_still_shadow_piece(const Bounding_Volume_Hierarchy& bvh, Span<const entity_uid_t> owner_of,
                                 Span<const piece_role_t> role_of, Visit_T&& visit)
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
}

template <typename Visit_T>
void for_each_moving_shadow_piece(const Entity_System& system, Span<const mover_t> movers,
                                  Span<const player_shadow_caster_t> player_casters, uint32_t first_key,
                                  Visit_T&& visit)
{
  uint32_t key = first_key;
  for (const mover_t& mover : movers)
  {
    if (entities::get_component<entities::Light>(system.try_find(mover.uid)) != nullptr)
      continue;
    for (uint32_t index = 0; index < mover.pieces.size(); ++index)
      visit(visited_piece_t{.key            = key++,
                            .role           = piece_role_t::Casts_From_Lens,
                            .owner          = mover.uid,
                            .bounds         = mover.pieces[index].bounds,
                            .planes         = mover.pieces[index].planes,
                            .polygons       = mover.pieces[index].face_polygons,
                            .index_in_owner = index});
  }
  for (uint32_t index = 0; index < player_casters.size(); ++index)
  {
    const player_shadow_caster_t& caster = player_casters[index];
    visit(visited_piece_t{.key                           = key++,
                          .role                          = piece_role_t::Casts_From_Lens,
                          .owner                         = caster.uid,
                          .bounds                        = caster.piece.bounds,
                          .planes                        = caster.piece.planes,
                          .polygons                      = caster.piece.face_polygons,
                          .casts_only_into_solid_shadows = true,
                          .index_in_owner                = index});
  }
}

} // namespace

shadow_volume_report_t compute_shadow_volumes(const Entity_System& system, const Bounding_Volume_Hierarchy& bvh,
                                              Span<const entity_uid_t> owner_of, Span<const mover_t> movers,
                                              Span<const player_shadow_caster_t> player_casters,
                                              const mover_rests_t& rests, projector_mask_cache_t& projector_masks,
                                              shadow_memo_t& memo, std::vector<shadow_volume_t>& out)
{
  out.clear();
  shadow_volume_report_t report;
  ++memo.generation;

  const std::vector<shadow_light_t> lights  = gather_shadow_lights(system, movers, rests);
  report.cutting_lights                     = static_cast<uint32_t>(lights.size());
  std::vector<shadow_light_clip_t> clips;
  clips.reserve(lights.size());
  for (const shadow_light_t& light : lights)
    clips.push_back(compute_shadow_light_clip(light));
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
  {
    memo.lights.clear();
    return report;
  }

  std::vector<shadow_volume_t> behind;

  const auto cast_piece_into = [&](const shadow_light_t& light, const visited_piece_t& piece,
                                   std::vector<shadow_volume_t>& into, shadow_volume_report_t& counts)
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
      case shadow_cast_refusal_t::Light_Inside_Caster: ++counts.refused_inside_caster; break;
      case shadow_cast_refusal_t::Silhouette_Open: ++counts.refused_open_silhouette; break;
      case shadow_cast_refusal_t::Light_On_Silhouette: ++counts.refused_on_silhouette; break;
      case shadow_cast_refusal_t::Too_Many_Planes: ++counts.refused_too_many_planes; break;
      case shadow_cast_refusal_t::Outside_Spot_Beam: ++counts.refused_outside_beam; break;
    }
    if (cast.refusal != shadow_cast_refusal_t::None)
    {
      if (cast.refusal != shadow_cast_refusal_t::Outside_Spot_Beam)
        counts.refused.push_back({.piece  = piece.key,
                                  .light  = light.uid,
                                  .center = get_aabb_center(piece.bounds),
                                  .reason = cast.refusal});
      return;
    }
    for (shadow_volume_t& volume : behind)
    {
      ++counts.cast;
      volume.static_index   = SHADOW_VOLUME_NOT_STATIC;
      volume.caster_casts_from_lens = piece.role == piece_role_t::Casts_From_Lens;
      if (volume.solid_shadows == entities::Solid_Shadows::Solid_From_Lens_To_Shadow &&
          piece.role != piece_role_t::Casts_From_Lens)
        volume.solid_shadows = entities::Solid_Shadows::Shadow_Volumes_Only;
      bool reaches_a_receiver = false;
      for (const receiver_t& receiver : receivers)
        if (receiver.key != piece.key && shadow_volume_touches_box(volume, receiver.bounds))
        {
          reaches_a_receiver = true;
          break;
        }
      if (!reaches_a_receiver && !light.keeps_every_volume)
      {
        ++counts.culled_reaching_nothing;
        continue;
      }
      volume.reaches_receiver = reaches_a_receiver;
      into.push_back(volume);
    }
  };

  // The still half, per light from its memo. The caster count is the cut's, so it is taken once, not per light.
  uint32_t still_piece_count = 0;
  for_each_still_shadow_piece(bvh, owner_of, role_of, [&](const visited_piece_t& piece)
  {
    ++still_piece_count;
    if (piece_role_casts(piece.role) || piece.role == piece_role_t::Erased)
      ++report.caster_pieces;
  });
  for (size_t index = 0; index < lights.size(); ++index)
  {
    const shadow_light_t&   light = lights[index];
    const projector_mask_t* mask  = nullptr;
    if (light.image != null_entity_uid)
    {
      const entities::Projected_Image_Entity* image =
          entities::entity_as<entities::Projected_Image_Entity>(system.try_find(light.image));
      const entities::Projector_Entity* projector =
          entities::entity_as<entities::Projector_Entity>(system.try_find(light.uid));
      if (image == nullptr || projector == nullptr)
        fatal_error("[shadow] projector {} shows image {}, which the gather found and the system now lacks",
                    light.uid, light.image);
      mask = &find_or_compute_projector_mask_for_image(projector_masks, *image, projector->aspect);
    }
    const uint64_t picture_hash = mask != nullptr ? mask->picture_hash : 0;

    shadow_light_memo_t* entry = nullptr;
    for (shadow_light_memo_t& candidate : memo.lights)
      if (candidate.light == light.uid)
        entry = &candidate;
    if (entry == nullptr)
    {
      memo.lights.push_back({.light = light.uid});
      entry = &memo.lights.back();
    }
    entry->last_used = memo.generation;

    const bool reusable = same_shadow_light(entry->built_for, light) && entry->built_for_bvh == &bvh &&
                          entry->built_for_roles == role_of && entry->built_for_picture_hash == picture_hash;
    if (!reusable)
    {
      entry->built_for              = light;
      entry->built_for_bvh          = &bvh;
      entry->built_for_roles        = role_of;
      entry->built_for_picture_hash = picture_hash;
      entry->volumes.clear();
      entry->solid_pieces.clear();
      entry->report = {};
      ++memo.builds;

      const auto cast_still_piece = [&](const visited_piece_t& piece)
      {
        if (!shadow_light_touches_box(light, clips[index], piece.bounds))
        {
          ++entry->report.refused_outside_beam;
          return;
        }
        cast_piece_into(light, piece, entry->volumes, entry->report);
      };
      for_each_still_shadow_piece(bvh, owner_of, role_of, [&](const visited_piece_t& piece)
      {
        if (piece_role_casts(piece.role) || piece.role == piece_role_t::Erased)
          cast_still_piece(piece);
      });

      // The dark of the picture, as pieces on the glass, cast into this projector alone: a piece on one lens
      // stands in no other light's way.
      uint32_t mask_key = still_piece_count;
      if (mask != nullptr)
        for (const projector_mask_quad_t& quad : mask->quads)
        {
          const std::optional<collision_piece_t> piece = try_build_projector_mask_piece(light, quad);
          if (!piece)
            continue;
          ++entry->report.mask_pieces;
          cast_still_piece(visited_piece_t{.key      = mask_key++,
                                           .role     = piece_role_t::Casts,
                                           .owner    = light.uid,
                                           .bounds   = piece->bounds,
                                           .planes   = piece->planes,
                                           .polygons = piece->face_polygons});
        }

      for (uint32_t volume_index = 0; volume_index < entry->volumes.size(); ++volume_index)
        entry->volumes[volume_index].static_index = volume_index;
      if (light.solid_shadows != entities::Solid_Shadows::None)
        for (const shadow_volume_t& volume : entry->volumes)
          entry->solid_pieces.push_back(try_build_solid_shadow_piece(volume));
    }

    out.insert(out.end(), entry->volumes.begin(), entry->volumes.end());
    add_shadow_volume_report_counts(report, entry->report);
  }
  std::erase_if(memo.lights, [&](const shadow_light_memo_t& entry) { return entry.last_used != memo.generation; });

  // The moving half, every tick, each piece under each light from its moving memo while neither has changed.
  std::vector<shadow_volume_t> moving_cast;
  for_each_moving_shadow_piece(system, movers, player_casters, still_piece_count, [&](const visited_piece_t& piece)
  {
    if (piece.casts_only_into_solid_shadows)
      ++report.player_pieces;
    else
      ++report.caster_pieces;
    for (size_t index = 0; index < lights.size(); ++index)
    {
      const shadow_light_t& light = lights[index];
      if (light.solid_shadows == entities::Solid_Shadows::None && piece.casts_only_into_solid_shadows)
        continue;

      shadow_moving_memo_t* entry = nullptr;
      for (shadow_moving_memo_t& candidate : memo.moving)
        if (candidate.light == light.uid && candidate.owner == piece.owner && candidate.piece_index == piece.index_in_owner)
          entry = &candidate;
      if (entry == nullptr)
      {
        memo.moving.push_back({.light = light.uid, .owner = piece.owner, .piece_index = piece.index_in_owner});
        entry = &memo.moving.back();
      }
      entry->last_used = memo.generation;

      if (!same_shadow_light(entry->built_for, light) || !same_planes(entry->built_from_planes, piece.planes))
      {
        entry->built_for = light;
        entry->built_from_planes.assign(piece.planes.begin(), piece.planes.end());
        entry->volume.reset();
        entry->solid_piece.reset();
        entry->counts = {};
        ++memo.moving_builds;
        if (!shadow_light_touches_box(light, clips[index], piece.bounds))
          ++entry->counts.refused_outside_beam;
        else
        {
          moving_cast.clear();
          cast_piece_into(light, piece, moving_cast, entry->counts);
          if (moving_cast.size() > 1)
            fatal_error("[shadow] mover {} piece {} threw {} volumes under light {}; a moving piece throws one",
                        piece.owner, piece.index_in_owner, moving_cast.size(), light.uid);
          if (!moving_cast.empty())
          {
            entry->volume                     = moving_cast[0];
            entry->volume->caster_piece_index = piece.index_in_owner;
            if (light.solid_shadows != entities::Solid_Shadows::None)
              entry->solid_piece = try_build_solid_shadow_piece(*entry->volume);
          }
        }
      }
      if (entry->volume)
        out.push_back(*entry->volume);
      add_shadow_volume_report_counts(report, entry->counts);
    }
  });
  std::erase_if(memo.moving, [&](const shadow_moving_memo_t& entry) { return entry.last_used != memo.generation; });

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

} // namespace shared
