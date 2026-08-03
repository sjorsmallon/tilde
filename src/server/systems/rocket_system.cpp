#include "../../shared/entities/entity_reflection.hpp"
#include "rocket_system.hpp"

#include "../../shared/cosmetic_events.hpp"
#include "../../shared/game_events.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/log.hpp"
#include "../cosmetic_events.hpp"
#include "../damage.hpp"
#include "../entity_lifecycle.hpp"
#include "../game_events.hpp"
#include "../server_api.hpp"

#include <unordered_set>
#include <vector>

namespace server
{

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
static void detonate(const entities::Rocket_Entity &rocket,
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
    //
    // get<T>(uid) is a uid-index lookup and answers nullptr for "no such
    // entity" and "wrong type" alike, which is exactly the two-way test this
    // needs — so the type dispatch below IS the lookup.
    vec3f entity_center;
    if (entities::Player_Entity *player =
            session.entity_system.get<entities::Player_Entity>(h.entity_id))
    {
      entity_center = player->position + vec3f{0.f, 38.f, 0.f};
    }
    else if (entities::Physics_Body_Entity *body =
                 session.entity_system.get<entities::Physics_Body_Entity>(h.entity_id))
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
  // does its own Static_Only cast_sphere against its local static geometry to
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
  if (direct_hit_uid != 0 &&
      session.entity_system.get<entities::Player_Entity>(direct_hit_uid) != nullptr)
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

  Span<entities::Rocket_Entity> rockets =
      session.entity_system.entities_of<entities::Rocket_Entity>();
  if (rockets.empty())
    return;

  // Collected as uids, not slot indices. Removal is swap-and-pop, so a slot
  // index recorded during the walk names a DIFFERENT rocket after the first
  // removal -- which is why this used to have to sort descending and dedupe. A
  // uid names the same entity no matter what moved.
  std::vector<shared::entity_uid_t> uids_to_remove;

  for (uint32_t i = 0; i < rockets.size(); ++i)
  {
    entities::Rocket_Entity &rocket = rockets[i];

    rocket.lifetime -= dt;
    if (rocket.lifetime <= 0.f)
    {
      detonate(rocket, context, /* direct_hit_uid */ 0,
               /* impact_normal */ {0.f, 0.f, 0.f});
      uids_to_remove.push_back(rocket.entity_id);
      continue;
    }

    vec3f next_pos = rocket.position + rocket.velocity * dt;

    hit_result_t hit;
    // Everything is a valid target except the player who fired: a rocket that
    // clips its own owner's capsule on the first tick would detonate in their
    // face. Back faces collide so a rocket spawned barely inside geometry
    // still stops rather than sailing through it.
    const query_filter_t filter{.layers     = query_layers_t::All,
                                .ignore_uid = rocket.owner_id,
                                .back_faces = back_face_mode_t::Collide};

    if (cast_sphere(physics, rocket.position, next_pos,
                    rocket.hitbox.size.x, filter, hit))
    {
      rocket.position = hit.position;
      detonate(rocket, context, hit.entity_id, hit.normal);
      uids_to_remove.push_back(rocket.entity_id);
    }
    else
    {
      rocket.position = next_pos;
    }
  }

  // `rockets` (and every reference taken from it above) is dead from here on --
  // each destroy swap-and-pops the pool it points into.
  for (shared::entity_uid_t uid : uids_to_remove)
    destroy_entity(context, uid);
}

} // namespace server
