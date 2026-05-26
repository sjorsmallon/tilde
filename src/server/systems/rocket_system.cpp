#include "rocket_system.hpp"

#include "../../shared/cosmetic_events.hpp"
#include "../../shared/entities/physics_body_entity.hpp"
#include "../../shared/entities/player_entity.hpp"
#include "../../shared/entities/rocket_entity.hpp"
#include "../../shared/game_events.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/log.hpp"
#include "../cosmetic_events.hpp"
#include "../damage.hpp"
#include "../game_events.hpp"
#include "../server_api.hpp"

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
    if (p.entity_id == uid) return &p;
  return nullptr;
}

static network::Physics_Body_Entity *
find_physics_body_by_uid(shared::game_session_t &session, shared::entity_uid_t uid)
{
  auto *pool = session.entity_system.get_entities<network::Physics_Body_Entity>(
      entity_type::PHYSICS_BODY);
  if (!pool) return nullptr;
  for (auto &b : *pool)
    if (b.entity_id == uid) return &b;
  return nullptr;
}

// Splash query at the detonation point. Linear falloff (1 - dist/radius).
// Self-damage is NOT filtered: standing in your own blast radius hurts and
// launches you. Direct-hit owner filtering happens at cast_sphere() time.
//
// Per-victim damage application (HP subtract, knockback write, PLAYER_DIED
// crossing detection, respawn schedule) is delegated to inflict_damage in
// src/server/damage.cpp — every damage source goes through that single
// choke point so adding a new one (hitscan, fall, void volume) cannot
// silently forget the death-event + respawn bookkeeping.
//
// `direct_hit_uid` is the entity uid the rocket's swept collision actually
// contacted this tick, or 0 if the detonation came from lifetime expiry. It
// becomes the gameplay event's `victim_id` only when the hit entity is a
// Player_Entity — physics-body or world-geometry hits leave victim_id at 0.
// `impact_normal` is the surface normal from the swept cast at the moment of
// contact, or {0,0,0} for an airburst (lifetime expiry, no surface). The
// client handler uses this to place a decal against the visible surface
// instead of guessing direction from the origin alone.
static void detonate(const network::Rocket_Entity &rocket,
                     server_context_t &context,
                     shared::entity_uid_t direct_hit_uid,
                     vec3f impact_normal)
{
  physics_state_t &physics = *context.physics;
  shared::game_session_t &session = context.session;

  if (rocket.damage_radius <= 0.f) return;

  std::vector<hit_result_t> hits = find_all_bodies_overlapping_sphere(physics, rocket.position, rocket.damage_radius);

  // One body may surface multiple contact points; only apply damage/impulse once.
  std::unordered_set<shared::entity_uid_t> already_applied;

  const shared::entity_uid_t attacker_uid = rocket.owner_id;
  const shared::entity_uid_t inflictor_uid = rocket.entity_id;

  for (const auto &h : hits)
  {
    if (h.entity_id == 0) continue;
    if (!already_applied.insert(h.entity_id).second) continue;

    // Resolve the entity center from network state — h.position is a surface
    // contact point which sits near the explosion origin for direct hits and
    // gives a degenerate direction.
    vec3f entity_center;
    if (network::Player_Entity *player = find_player_by_uid(session, h.entity_id))
    {
      entity_center = player->position + vec3f{0.f, 38.f, 0.f};
    }
    else if (network::Physics_Body_Entity *body = find_physics_body_by_uid(session, h.entity_id))
    {
      entity_center = body->position;
    }
    else
    {
      continue; // unknown entity type — inflict_damage would log_error
    }

    vec3f to_target = entity_center - rocket.position;
    float distance  = linalg::length(to_target);
    if (distance > rocket.damage_radius) continue;

    float falloff = 1.f - (distance / rocket.damage_radius);

    damage_info_t info{};
    info.victim_uid      = h.entity_id;
    info.attacker_uid    = attacker_uid;
    info.inflictor_uid   = inflictor_uid;
    info.weapon_id       = 0; // no per-weapon ids yet
    info.amount          = rocket.damage_amount * falloff;
    info.source_position = rocket.position;
    info.knockback_force = rocket.knockback_force * falloff;
    info.type            = damage_type_t::GENERIC;
    info.was_headshot    = false;
    inflict_damage(context, info);
  }

  // Cosmetic explosion: announce the detonation through the cosmetic-events
  // channel. The server reports the world-space origin; the client handler
  // does its own cast_sphere_static against its local static geometry to
  // resolve a surface contact for the decal — see plan §"Server emits, client
  // traces locally."
  shared::effect_data_t fx{};
  fx.origin           = rocket.position;
  fx.normal           = impact_normal; // {0,0,0} = airburst, no surface decal
  fx.color            = {1.f, 1.f, 1.f};
  fx.scale            = rocket.damage_radius;
  fx.attached_entity  = 0;
  fx.surface_material = 0;
  dispatch_effect(context, shared::effect_type_t::ROCKET_EXPLOSION, fx);

  // Reliable gameplay event for HUD/score/kill-feed consumers. Victim is the
  // direct-hit player only — splash kills get reported via a future
  // PLAYER_DIED event (see plan §"Phase 4"), not back-derived from this one.
  shared::entity_uid_t victim_id = 0;
  if (direct_hit_uid != 0 && find_player_by_uid(session, direct_hit_uid))
    victim_id = direct_hit_uid;

  shared::game_event_t event{};
  event.kind = shared::game_event_kind_t::ROCKET_DETONATED;
  event.rocket_detonated.attacker_id = rocket.owner_id;
  event.rocket_detonated.victim_id   = victim_id;
  event.rocket_detonated.weapon_id   = 0; // rocket carries no weapon id yet
  fire_game_event(context, event);
}

void update_rockets(server_context_t &context, float dt)
{
  shared::game_session_t &session = context.session;
  physics_state_t        &physics = *context.physics;

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
      detonate(rocket, context, /* direct_hit_uid */ 0,
               /* impact_normal */ {0.f, 0.f, 0.f});
      to_remove.push_back(i);
      continue;
    }

    vec3f next_pos = rocket.position + rocket.velocity * dt;

    hit_result_t hit;
    shared::entity_uid_t ignore_uid = rocket.owner_id;

    if (cast_sphere(physics, rocket.position, next_pos,
                    rocket.hitbox.size.x, ignore_uid, hit))
    {
      rocket.position = hit.position;
      detonate(rocket, context, hit.entity_id, hit.normal);
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
