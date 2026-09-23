#include "projectile_sweep.hpp"

#include "entities/generated/entities_generated.hpp"
#include "entity_system.hpp"
#include "player_constants.hpp"
#include "shapes.hpp"

#include <algorithm>

namespace shared
{

namespace
{

aabb_bounds_t segment_bounds(const linalg::vec3f& from, const linalg::vec3f& to, float radius)
{
  aabb_bounds_t bounds;
  for (int axis = 0; axis < 3; ++axis)
  {
    bounds.min[axis] = std::min(from[axis], to[axis]) - radius;
    bounds.max[axis] = std::max(from[axis], to[axis]) + radius;
  }
  return bounds;
}

} // namespace

std::optional<projectile_hit_t> sweep_sphere_against_movers(Span<const mover_t> movers,
                                                            const linalg::vec3f& from,
                                                            const linalg::vec3f& to, float radius)
{
  const linalg::vec3f dir = to - from;
  if (linalg::dot(dir, dir) == 0.f)
    return std::nullopt;

  const aabb_bounds_t swept = segment_bounds(from, to, radius);
  std::optional<projectile_hit_t> nearest;

  for (const mover_t& mover : movers)
  {
    if (mover.pieces.empty() || !aabbs_intersect(swept, mover.swept_bounds))
      continue;

    for (const collision_piece_t& piece : mover.pieces)
    {
      if (!aabbs_intersect(swept, piece.bounds))
        continue;

      float         t_piece;
      float         t_exit;
      linalg::vec3f normal;
      if (!intersect_sphere_sweep_convex_hull(piece.planes, from, dir, radius, t_piece, t_exit,
                                              normal))
        continue;

      const float t = std::max(t_piece, 0.f);
      if (t > 1.f || (nearest && t >= nearest->t))
        continue;

      nearest = projectile_hit_t{.position   = from + dir * t,
                                 .normal     = normal,
                                 .t          = t,
                                 .entity_uid = mover.uid};
    }
  }

  return nearest;
}

std::optional<projectile_hit_t> sweep_projectile(const Bounding_Volume_Hierarchy& bvh,
                                                 const predicted_world_t& world,
                                                 const linalg::vec3f& from,
                                                 const linalg::vec3f& to, float radius)
{
  std::optional<projectile_hit_t> nearest =
      sweep_sphere_against_movers(world.movers, from, to, radius);

  const std::optional<sweep_hit_t> map_hit =
      bvh_sweep_sphere(bvh, from, to, radius, world.disabled_geometry);
  if (map_hit && (!nearest || map_hit->t < nearest->t))
    nearest = projectile_hit_t{.position   = from + (to - from) * map_hit->t,
                               .normal     = map_hit->normal,
                               .t          = map_hit->t,
                               .entity_uid = null_entity_uid};

  return nearest;
}

void collect_projectile_targets(const Entity_System& system, std::vector<projectile_target_t>& out)
{
  out.clear();

  for (const entities::Player_Entity& player : system.entities_of<entities::Player_Entity>())
  {
    if (player.health.current_health <= 0)
      continue;
    out.push_back({player.entity_id, player_hull_bounds(player.position)});
  }

  for (const entities::Damageable_Entity& damageable :
       system.entities_of<entities::Damageable_Entity>())
  {
    if (damageable.health.current_health <= 0)
      continue;
    out.push_back({damageable.entity_id, get_bounds(damageable.volume, damageable.position)});
  }

  for (const entities::Physics_Body_Entity& body :
       system.entities_of<entities::Physics_Body_Entity>())
  {
    const linalg::vec3f half_extents = body.size * 0.5f;
    out.push_back({body.entity_id, {body.position - half_extents, body.position + half_extents}});
  }

  for (const entities::Weapon_Entity& weapon : system.entities_of<entities::Weapon_Entity>())
  {
    if (weapon.owner_uid != null_entity_uid)
      continue;
    out.push_back({weapon.entity_id, get_bounds(weapon.volume, weapon.position)});
  }
}

std::optional<projectile_hit_t> sweep_sphere_against_targets(Span<const projectile_target_t> targets,
                                                             const linalg::vec3f& from,
                                                             const linalg::vec3f& to, float radius,
                                                             entity_uid_t ignore_uid)
{
  const linalg::vec3f dir = to - from;
  if (linalg::dot(dir, dir) == 0.f)
    return std::nullopt;

  const linalg::vec3f inflation{radius, radius, radius};
  std::optional<projectile_hit_t> nearest;

  for (const projectile_target_t& target : targets)
  {
    if (target.uid == ignore_uid)
      continue;

    float         t_target;
    float         t_exit;
    linalg::vec3f normal;
    if (!linalg::intersect_ray_aabb(from, dir, target.bounds.min - inflation,
                                    target.bounds.max + inflation, t_target, t_exit, normal))
      continue;

    const float t = std::max(t_target, 0.f);
    if (t > 1.f || (nearest && t >= nearest->t))
      continue;

    nearest = projectile_hit_t{.position   = from + dir * t,
                               .normal     = normal,
                               .t          = t,
                               .entity_uid = target.uid};
  }

  return nearest;
}

} // namespace shared
