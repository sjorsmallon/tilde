#include "solid_shadows.hpp"

#include "entities/entity_reflection.hpp"
#include "entity_system.hpp"
#include "geometry/convex_decomposition.hpp"
#include "geometry/convex_hull.hpp"

namespace shared
{

std::optional<collision_piece_t> try_build_solid_shadow_piece(const shadow_volume_t& volume)
{
  std::vector<Plane> planes;
  planes.reserve(volume.side_plane_count + volume.front_plane_count + 6);
  for (uint32_t side = 0; side < volume.side_plane_count; ++side)
    planes.push_back(volume.side_planes[side]);
  for (uint32_t front = 0; front < volume.front_plane_count; ++front)
    planes.push_back(volume.front_planes[front]);

  const linalg::vec3f centre = get_aabb_center(volume.caster_bounds);
  const linalg::vec3f half   = (volume.caster_bounds.max - volume.caster_bounds.min) * 0.5f +
                               linalg::vec3f{SOLID_SHADOW_REACH, SOLID_SHADOW_REACH, SOLID_SHADOW_REACH};
  planes.push_back({.point = centre + linalg::vec3f{half.x, 0.f, 0.f}, .normal = {1.f, 0.f, 0.f}});
  planes.push_back({.point = centre - linalg::vec3f{half.x, 0.f, 0.f}, .normal = {-1.f, 0.f, 0.f}});
  planes.push_back({.point = centre + linalg::vec3f{0.f, half.y, 0.f}, .normal = {0.f, 1.f, 0.f}});
  planes.push_back({.point = centre - linalg::vec3f{0.f, half.y, 0.f}, .normal = {0.f, -1.f, 0.f}});
  planes.push_back({.point = centre + linalg::vec3f{0.f, 0.f, half.z}, .normal = {0.f, 0.f, 1.f}});
  planes.push_back({.point = centre - linalg::vec3f{0.f, 0.f, half.z}, .normal = {0.f, 0.f, -1.f}});

  const std::optional<convex_polyhedron_t> body =
      try_build_convex_from_planes(planes, centre, linalg::length(half) + 1.f);
  if (!body)
    return std::nullopt;
  return collision_piece_t{.bounds        = compute_point_bounds(body->vertices),
                           .planes        = compute_polyhedron_planes(*body),
                           .face_polygons = compute_polyhedron_face_polygons(*body)};
}

void compute_solid_shadows(const Entity_System& system, Span<const shadow_volume_t> shadow_volumes,
                           std::vector<mover_t>& out)
{
  const size_t first_appended = out.size();
  for (const shadow_volume_t& volume : shadow_volumes)
  {
    const entities::Light* light = entities::get_component<entities::Light>(system.try_find(volume.light));
    if (light == nullptr || !light->shadows_are_solid)
      continue;
    std::optional<collision_piece_t> piece = try_build_solid_shadow_piece(volume);
    if (!piece)
      continue;

    mover_t* mover = nullptr;
    for (size_t index = first_appended; index < out.size(); ++index)
      if (out[index].uid == volume.light)
        mover = &out[index];
    if (mover == nullptr)
    {
      mover_t dark;
      dark.uid                         = volume.light;
      dark.pose_at_tick_start.position = volume.light_apex;
      dark.pose_at_tick_end            = dark.pose_at_tick_start;
      dark.crushes                     = false;
      dark.swept_bounds                = piece->bounds;
      out.push_back(std::move(dark));
      mover = &out.back();
    }
    mover->swept_bounds = union_aabb(mover->swept_bounds, piece->bounds);
    mover->pieces.push_back(std::move(*piece));
  }
}

} // namespace shared
