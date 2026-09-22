#include "ricochet_system.hpp"

#include "../../shared/linalg.hpp"
#include "../../shared/log.hpp"
#include "../../shared/movement_kernel.hpp"
#include "../../shared/physics.hpp"
#include "../../shared/player_constants.hpp"
#include "../entity_lifecycle.hpp"
#include "projectile_flight.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace server
{

static constexpr float LANDING_CLEARANCE         = 1.f;
static constexpr int   LANDING_PUSH_OUT_ATTEMPTS = 4;

// How far the hull's centre sits from a face it rests against along `normal`.
static float hull_support_along(const vec3f& normal)
{
  return shared::player_half_width * std::fabs(normal.x) +
         shared::player_half_height * std::fabs(normal.y) +
         shared::player_half_width * std::fabs(normal.z);
}

// Feet for a hull resting on the struck surface, pushed out along the normal until it overlaps
// nothing. Empty when the pushes run out, which is a crevice no hull fits.
[[nodiscard]] static std::optional<vec3f> try_find_landing_feet(
    const Bounding_Volume_Hierarchy& bvh, const shared::predicted_world_t& world,
    const vec3f& surface_point, const vec3f& normal)
{
  vec3f center = surface_point + normal * (hull_support_along(normal) + LANDING_CLEARANCE);

  std::vector<shared::collision_candidate_t> candidates;
  for (int attempt = 0; attempt <= LANDING_PUSH_OUT_ATTEMPTS; ++attempt)
  {
    candidates.clear();
    shared::collect_collision_candidates(
        bvh, world,
        shared::hull_aabb(center, shared::player_half_width, shared::player_half_height),
        candidates);

    float deepest = 0.f;
    for (const shared::collision_candidate_t& candidate : candidates)
      deepest = std::max(deepest, shared::hull_penetration_depth(
                                      *candidate.collision_planes, center,
                                      shared::player_half_width, shared::player_half_height));

    if (deepest <= 0.f)
      return center - vec3f{0.f, shared::player_half_height, 0.f};

    center = center + normal * (deepest + LANDING_CLEARANCE);
  }
  return std::nullopt;
}

static void send_owner_to(server_context_t& context,
                          const shared::predicted_world_storage_t& world,
                          const entities::Ricochet_Entity& ricochet, const hit_result_t& hit)
{
  entities::Player_Entity* owner =
      context.world.session.entity_system.get<entities::Player_Entity>(
          ricochet.projectile.owner_uid);
  if (owner == nullptr)
  {
    log_terminal("ricochet uid {} landed, but its owner uid {} is not a player: nobody is sent",
                 ricochet.entity_id, ricochet.projectile.owner_uid);
    return;
  }
  if (owner->health.current_health <= 0)
    return;

  // Jolt's penetration axis can degenerate on a grazing contact; up is the one direction that
  // always has a hull's worth of room above a surface you could reach.
  const vec3f normal = linalg::length(hit.normal) > 0.5f ? linalg::normalize(hit.normal)
                                                         : vec3f{0.f, 1.f, 0.f};
  // hit.position is the swept sphere's CENTRE at contact, a radius off the surface.
  const vec3f surface_point = hit.position - normal * ricochet.collision_radius;

  const std::optional<vec3f> feet = try_find_landing_feet(
      context.world.session.bvh, shared::predicted_world_of(world, owner->team_allegiance),
      surface_point, normal);
  if (!feet)
  {
    log_warning("ricochet uid {} landed at ({:.0f}, {:.0f}, {:.0f}), but no hull fits there: "
                "owner uid {} stays put",
                ricochet.entity_id, surface_point.x, surface_point.y, surface_point.z,
                owner->entity_id);
    return;
  }

  // Velocity is kept, the remnant teleport's rule: a run or a fall carries across.
  owner->position = *feet;
  set_kinematic_pose(*context.world.physics, owner->entity_id,
                     owner->position + vec3f{0.f, shared::player_capsule_center_offset, 0.f},
                     owner->velocity);
}

void update_ricochets(server_context_t& context, const shared::predicted_world_storage_t& world,
                      float dt)
{
  shared::Entity_System& entity_system = context.world.session.entity_system;

  std::vector<shared::entity_uid_t> spent;

  for (entities::Ricochet_Entity& ricochet :
       entity_system.entities_of<entities::Ricochet_Entity>())
  {
    ricochet.lifetime -= dt;
    if (ricochet.lifetime <= 0.f)
    {
      // Touched nothing in its whole life: it left the map, and nobody is sent after it.
      spent.push_back(ricochet.entity_id);
      continue;
    }

    const std::optional<hit_result_t> hit =
        fly_projectile(context, ricochet, ricochet.projectile, ricochet.collision_radius, dt);
    if (!hit)
      continue;

    spent.push_back(ricochet.entity_id);
    send_owner_to(context, world, ricochet, *hit);
  }

  for (const shared::entity_uid_t uid : spent)
    destroy_entity(context, uid);
}

} // namespace server
