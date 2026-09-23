#pragma once
// The ONE question a flying thing asks: what does a sphere swept from here to
// there stop at? It takes the BVH (the map's SHAPE) and a predicted_world_t
// (the tick's STATE: the disabled set, the movers) so a projectile collides
// with exactly what a player's hull collides with -- a switched-off gate is
// not there, a lift is where it is at the end of the tick. collision_world_plan.md.
//
// The TARGETS are the other half: the entities a projectile can land on, as
// the box each one physically is (a player's hull, a damageable's volume, a
// body's extent), cut once per tick from the CURRENT entities -- projectiles
// fly after every player has moved, so what they hit is where everyone is now.

#include "aabb.hpp"
#include "collision_detection.hpp"
#include "entity_uid.hpp"
#include "linalg.hpp"
#include "movers.hpp"
#include "predicted_world.hpp"
#include "span.hpp"

#include <optional>
#include <vector>

namespace shared
{

struct Entity_System;

struct projectile_hit_t
{
  // The sphere's CENTER when it stopped, from + (to - from) * t.
  linalg::vec3f position;
  linalg::vec3f normal;
  float         t;
  // The mover or the target hit, or null for the map.
  entity_uid_t  entity_uid = null_entity_uid;
};

struct projectile_target_t
{
  entity_uid_t  uid;
  aabb_bounds_t bounds;
};

// Every mover's pieces at its END pose, the pose player_move collides with.
[[nodiscard]] std::optional<projectile_hit_t>
sweep_sphere_against_movers(Span<const mover_t> movers, const linalg::vec3f& from,
                            const linalg::vec3f& to, float radius);

// The map through the world's disabled set, then the movers; the nearer wins.
[[nodiscard]] std::optional<projectile_hit_t>
sweep_projectile(const Bounding_Volume_Hierarchy& bvh, const predicted_world_t& world,
                 const linalg::vec3f& from, const linalg::vec3f& to, float radius);

// A living player by its hull, a living damageable by its volume, a physics
// body by its size, a weapon lying in the world by its volume.
void collect_projectile_targets(const Entity_System& system, std::vector<projectile_target_t>& out);

// Each target's box inflated by the radius, nearest entry wins; `ignore_uid` is
// the shooter, who is the same kind of box as everyone else.
[[nodiscard]] std::optional<projectile_hit_t>
sweep_sphere_against_targets(Span<const projectile_target_t> targets, const linalg::vec3f& from,
                             const linalg::vec3f& to, float radius, entity_uid_t ignore_uid);

} // namespace shared
