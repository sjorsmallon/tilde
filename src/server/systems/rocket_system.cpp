#include "rocket_system.hpp"

#include "../../shared/entities/physics_body_entity.hpp"
#include "../../shared/entities/player_entity.hpp"
#include "../../shared/entities/rocket_entity.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/log.hpp"

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace server
{

static network::Player_Entity *
find_player_by_uid(shared::game_session_t &session, shared::entity_uid_t uid)
{
  auto *players = session.entity_system.get_entities<network::Player_Entity>(
      entity_type::PLAYER);
  if (!players) return nullptr;
  for (auto &p : *players)
    if (static_cast<shared::entity_uid_t>(p.entity_id) == uid) return &p;
  return nullptr;
}

static network::Physics_Body_Entity *
find_physics_body_by_uid(shared::game_session_t &session, shared::entity_uid_t uid)
{
  auto *pool = session.entity_system.get_entities<network::Physics_Body_Entity>(
      entity_type::PHYSICS_BODY);
  if (!pool) return nullptr;
  for (auto &b : *pool)
    if (static_cast<shared::entity_uid_t>(b.entity_id) == uid) return &b;
  return nullptr;
}

// Splash query at the detonation point. Linear falloff (1 - dist/radius).
// Self-damage is NOT filtered: standing in your own blast radius hurts and
// launches you. Direct-hit owner filtering happens at cast_sphere() time.
//
// Dispatch rule:
//   - Player_Entity bodies are kinematic in Jolt — Jolt impulses are no-ops on
//     them, and even AddLinearVelocity gets clobbered by next set_kinematic_pose().
//     So we update player->velocity directly. player_move() reads it next tick.
//   - Physics_Body_Entity (dynamic) gets add_linear_velocity. knockback_force
//     is a velocity delta (matches the pre-Jolt code's convention), so we use
//     AddLinearVelocity not AddImpulse (which would scale by mass).
static void detonate(const network::Rocket_Entity &rocket,
                     physics_state_t &physics,
                     shared::game_session_t &session)
{
  if (rocket.damage_radius <= 0.f) return;

  std::vector<hit_result_t> hits;
  overlap_sphere(physics, rocket.position, rocket.damage_radius, hits);

  // One body may surface multiple contact points; only apply damage/impulse once.
  std::unordered_set<shared::entity_uid_t> already_applied;

  for (const auto &h : hits)
  {
    if (h.entity_id == 0) continue;
    if (!already_applied.insert(h.entity_id).second) continue;

    // Resolve the entity center from network state — h.position is a surface
    // contact point which sits near the explosion origin for direct hits and
    // gives a degenerate direction.
    vec3f entity_center;
    network::Player_Entity *player = find_player_by_uid(session, h.entity_id);
    network::Physics_Body_Entity *body = nullptr;
    if (player)
    {
      entity_center = player->position + vec3f{0.f, 38.f, 0.f};
    }
    else
    {
      body = find_physics_body_by_uid(session, h.entity_id);
      if (!body) continue; // unknown entity type — skip (could be a future entity class)
      entity_center = body->position;
    }

    vec3f to_target = entity_center - rocket.position;
    float distance  = linalg::length(to_target);
    if (distance > rocket.damage_radius) continue;

    float falloff = 1.f - (distance / rocket.damage_radius);
    vec3f direction = (distance > 1e-4f) ? (to_target * (1.f / distance))
                                         : vec3f{0.f, 1.f, 0.f};

    float damage         = rocket.damage_amount * falloff;
    vec3f velocity_delta = direction * (rocket.knockback_force * falloff);

    log_terminal("Rocket detonated: entity_id={}, kind={}, dist={:.1f}, dmg={:.1f}, dv=({:.1f},{:.1f},{:.1f})",
                 h.entity_id, player ? "player" : "body",
                 distance, damage,
                 velocity_delta.x, velocity_delta.y, velocity_delta.z);

    if (player)
    {
      player->health  -= static_cast<int32_t>(damage);
      player->velocity = player->velocity + velocity_delta;
    }
    else
    {
      add_linear_velocity(physics, h.entity_id, velocity_delta);
    }
  }
}

void update_rockets(shared::game_session_t &session,
                    physics_state_t &physics,
                    float dt)
{
  auto *rockets = session.entity_system.get_entities<network::Rocket_Entity>(
      entity_type::ROCKET);
  if (!rockets || rockets->empty())
    return;

  std::vector<int> to_remove;

  for (int i = 0; i < static_cast<int>(rockets->size()); ++i)
  {
    auto &rocket = (*rockets)[i];

    rocket.lifetime -= dt;
    if (rocket.lifetime <= 0.f)
    {
      detonate(rocket, physics, session);
      to_remove.push_back(i);
      continue;
    }

    vec3f next_pos = rocket.position + rocket.velocity * dt;

    hit_result_t hit;
    shared::entity_uid_t ignore_uid =
        (rocket.owner_id > 0) ? static_cast<shared::entity_uid_t>(rocket.owner_id) : 0;

    if (cast_sphere(physics, rocket.position, next_pos,
                    rocket.hitbox.size.x, ignore_uid, hit))
    {
      rocket.position = hit.position;
      detonate(rocket, physics, session);
      to_remove.push_back(i);
    }
    else
    {
      rocket.position = next_pos;
    }
  }

  // Sort descending so swap-removes don't invalidate earlier indices.
  std::sort(to_remove.rbegin(), to_remove.rend());
  to_remove.erase(std::unique(to_remove.begin(), to_remove.end()),
                  to_remove.end());

  for (int idx : to_remove)
    session.entity_system.destroy(entity_type::ROCKET, &(*rockets)[idx]);
}

} // namespace server
