#include "support_planks.hpp"

#include "collision/collision_detection.hpp"
#include "movement/player_constants.hpp"

#include <cmath>

namespace shared
{

namespace
{

const shadow_volume_t* try_find_solid_shadow_volume_for_mover(Span<const shadow_volume_t> shadow_volumes,
                                                              entity_uid_t                mover)
{
  for (const shadow_volume_t& volume : shadow_volumes)
  {
    if (volume.solid_shadows == entities::Solid_Shadows::None)
      continue;
    if (volume.light == mover || volume.caster == mover)
      return &volume;
  }
  return nullptr;
}

linalg::vec3f across_direction_for_ray(const linalg::vec3f& ray)
{
  linalg::vec3f across = linalg::cross(ray, linalg::vec3f{0.f, 1.f, 0.f});
  if (linalg::length(across) < 1e-3f)
    across = linalg::cross(ray, linalg::vec3f{1.f, 0.f, 0.f});
  return linalg::normalize(across);
}

} // namespace

void compute_support_planks(Span<const supported_player_t> players, Span<const shadow_volume_t> shadow_volumes,
                            const Bounding_Volume_Hierarchy& bvh, std::vector<support_plank_t>& out)
{
  for (const supported_player_t& player : players)
  {
    if (player.ground_mover == null_entity_uid)
      continue;
    const shadow_volume_t* volume = try_find_solid_shadow_volume_for_mover(shadow_volumes, player.ground_mover);
    if (volume == nullptr)
      continue;

    const linalg::vec3f toward_shadow = volume->light_directional
                                            ? linalg::normalize(volume->light_direction)
                                            : linalg::normalize(player.feet - volume->light_apex);
    ray_hit_result_t hit{};
    if (!bvh_intersect_ray(bvh, player.feet, toward_shadow, hit) || hit.t > SHADOW_VOLUME_DRAWN_REACH)
      continue;

    const linalg::vec3f landing = player.feet + toward_shadow * hit.t;
    const linalg::vec3f across  = across_direction_for_ray(toward_shadow) * player_half_width;
    support_plank_t     plank;
    plank.player     = player.player;
    plank.light      = volume->light;
    plank.corners[0] = player.feet - across;
    plank.corners[1] = player.feet + across;
    plank.corners[2] = landing + across;
    plank.corners[3] = landing - across;
    out.push_back(plank);
  }
}

} // namespace shared
