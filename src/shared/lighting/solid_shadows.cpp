#include "solid_shadows.hpp"

#include "lighting.hpp"

#include "entities/entity_reflection.hpp"
#include "entities/generated/entities/player_entity_generated.hpp"
#include "entity_system.hpp"
#include "geometry/convex_decomposition.hpp"
#include "geometry/convex_hull.hpp"
#include "log.hpp"

#include <algorithm>

namespace shared
{

std::optional<collision_piece_t> try_build_solid_shadow_piece(const shadow_volume_t& volume)
{
  const bool from_the_lens =
      volume.solid_shadows == entities::Solid_Shadows::Solid_From_Lens_To_Shadow && !volume.light_directional;
  std::vector<Plane> planes;
  planes.reserve(volume.side_plane_count + volume.light_facing_plane_count + 7);
  for (uint32_t side = 0; side < volume.side_plane_count; ++side)
    planes.push_back(volume.side_planes[side]);
  aabb_bounds_t reach_around = volume.caster_bounds;
  if (from_the_lens)
  {
    const linalg::vec3f axis = linalg::normalize(get_aabb_center(volume.caster_bounds) - volume.light_apex);
    planes.push_back({.point = volume.light_apex + axis * SHADOW_NEAR_PLANE, .normal = axis * -1.f});
    reach_around = union_aabb(reach_around, {volume.light_apex, volume.light_apex});
  }
  else
    for (uint32_t front = 0; front < volume.light_facing_plane_count; ++front)
      planes.push_back(volume.light_facing_planes[front]);

  const linalg::vec3f centre = get_aabb_center(reach_around);
  const linalg::vec3f half   = (reach_around.max - reach_around.min) * 0.5f +
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

namespace
{

std::optional<collision_piece_t> solid_shadow_piece_for_volume(const shadow_memo_t& memo, const shadow_volume_t& volume)
{
  if (volume.static_index == SHADOW_VOLUME_NOT_STATIC)
  {
    for (const shadow_moving_memo_t& entry : memo.moving)
      if (entry.light == volume.light && entry.owner == volume.caster && entry.piece_index == volume.caster_piece_index)
        return entry.solid_piece;
    fatal_error("[solid shadows] caster {} piece {} under light {} has no moving memo: the volumes are not this memo's "
                "computation",
                volume.caster, volume.caster_piece_index, volume.light);
  }
  for (const shadow_light_memo_t& entry : memo.lights)
    if (entry.light == volume.light)
    {
      if (volume.static_index >= entry.solid_pieces.size())
        fatal_error("[solid shadows] light {} memo holds {} solid pieces, volume asks for {}: the volumes are not this "
                    "memo's cut",
                    volume.light, entry.solid_pieces.size(), volume.static_index);
      return entry.solid_pieces[volume.static_index];
    }
  fatal_error("[solid shadows] light {} has no memo: the volumes are not this memo's cut", volume.light);
}

} // namespace

void compute_solid_shadows(const Entity_System& system, Span<const shadow_volume_t> shadow_volumes,
                           const shadow_memo_t& memo, std::vector<mover_t>& out)
{
  const size_t first_appended = out.size();
  for (const shadow_volume_t& volume : shadow_volumes)
  {
    const entities::Light* light = entities::get_component<entities::Light>(system.try_find(volume.light));
    if (light == nullptr || light->solid_shadows == entities::Solid_Shadows::None)
      continue;
    std::optional<collision_piece_t> piece = solid_shadow_piece_for_volume(memo, volume);
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
}

} // namespace shared
