#include "solid_shadows.hpp"

#include "entities/entity_reflection.hpp"
#include "entities/generated/entities/player_entity_generated.hpp"
#include "entity_system.hpp"
#include "geometry/convex_decomposition.hpp"
#include "geometry/convex_hull.hpp"

#include <algorithm>

namespace shared
{

namespace
{

bool same_plane(const Plane& a, const Plane& b)
{
  return a.point.x == b.point.x && a.point.y == b.point.y && a.point.z == b.point.z && a.normal.x == b.normal.x &&
         a.normal.y == b.normal.y && a.normal.z == b.normal.z;
}

// Everything try_build_solid_shadow_piece reads: the two plane lists and the caster's bounds the reach box is around.
bool built_from_same_planes(const shadow_volume_t& built_from, const shadow_volume_t& volume)
{
  if (built_from.side_plane_count != volume.side_plane_count ||
      built_from.light_facing_plane_count != volume.light_facing_plane_count)
    return false;
  if (!same_plane({built_from.caster_bounds.min, built_from.caster_bounds.max},
                  {volume.caster_bounds.min, volume.caster_bounds.max}))
    return false;
  for (uint32_t side = 0; side < volume.side_plane_count; ++side)
    if (!same_plane(built_from.side_planes[side], volume.side_planes[side]))
      return false;
  for (uint32_t front = 0; front < volume.light_facing_plane_count; ++front)
    if (!same_plane(built_from.light_facing_planes[front], volume.light_facing_planes[front]))
      return false;
  return true;
}

} // namespace

std::optional<collision_piece_t> try_build_solid_shadow_piece(const shadow_volume_t& volume)
{
  std::vector<Plane> planes;
  planes.reserve(volume.side_plane_count + volume.light_facing_plane_count + 6);
  for (uint32_t side = 0; side < volume.side_plane_count; ++side)
    planes.push_back(volume.side_planes[side]);
  for (uint32_t front = 0; front < volume.light_facing_plane_count; ++front)
    planes.push_back(volume.light_facing_planes[front]);

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

std::optional<collision_piece_t> try_get_solid_shadow_piece_for_volume(solid_shadow_cache_t& cache,
                                                                      const shadow_volume_t& volume)
{
  solid_shadow_cache_entry_t* entry = nullptr;
  for (solid_shadow_cache_entry_t& candidate : cache.entries)
    if (candidate.light == volume.light && candidate.caster_piece == volume.caster_piece)
      entry = &candidate;

  if (entry != nullptr && built_from_same_planes(entry->built_from, volume))
  {
    entry->last_used = cache.cut;
    return entry->piece;
  }

  if (entry == nullptr)
  {
    cache.entries.push_back({.light = volume.light, .caster_piece = volume.caster_piece});
    entry = &cache.entries.back();
  }
  entry->last_used  = cache.cut;
  entry->built_from = volume;
  entry->piece      = try_build_solid_shadow_piece(volume);
  ++cache.builds;
  return entry->piece;
}

void compute_solid_shadows(const Entity_System& system, Span<const shadow_volume_t> shadow_volumes,
                           solid_shadow_cache_t& cache, std::vector<mover_t>& out)
{
  ++cache.cut;
  const size_t first_appended = out.size();
  for (const shadow_volume_t& volume : shadow_volumes)
  {
    const entities::Light* light = entities::get_component<entities::Light>(system.try_find(volume.light));
    if (light == nullptr || !light->shadows_are_solid)
      continue;
    std::optional<collision_piece_t> piece = try_get_solid_shadow_piece_for_volume(cache, volume);
    if (!piece)
      continue;

    const bool         cast_by_a_player = entities::entity_as<entities::Player_Entity>(system.try_find(volume.caster)) != nullptr;
    const entity_uid_t mover_uid        = cast_by_a_player ? volume.caster : volume.light;
    mover_t*           mover            = nullptr;
    for (size_t index = first_appended; index < out.size(); ++index)
      if (out[index].uid == mover_uid)
        mover = &out[index];
    if (mover == nullptr)
    {
      mover_t dark;
      dark.uid                         = mover_uid;
      dark.pose_at_tick_start.position = cast_by_a_player ? get_aabb_center(volume.caster_bounds) : volume.light_apex;
      dark.pose_at_tick_end            = dark.pose_at_tick_start;
      dark.crushes                     = false;
      dark.swept_bounds                = piece->bounds;
      out.push_back(std::move(dark));
      mover = &out.back();
    }
    mover->swept_bounds = union_aabb(mover->swept_bounds, piece->bounds);
    mover->pieces.push_back(std::move(*piece));
  }

  std::erase_if(cache.entries, [&](const solid_shadow_cache_entry_t& entry)
                { return entry.last_used + SOLID_SHADOW_CACHE_KEEP_CUTS < cache.cut; });
}

} // namespace shared
