#include "../../shared/entities/entity_reflection.hpp"
#include "rocket_system.hpp"

#include "../../shared/linalg.hpp"
#include "../../shared/log.hpp"
#include "../../shared/weapons.hpp"
#include "../entity_lifecycle.hpp"
#include "../server_api.hpp"

#include <unordered_set>
#include <vector>

namespace server
{

//@FIXME(SJM): the way this works is not ideal and does not feel good.

static void detonate(const entities::Rocket_Entity &rocket,
                     server_context_t &context,
                     shared::entity_uid_t direct_hit_uid,
                     vec3f impact_normal)
{
  physics_state_t &physics = *context.world.physics;
  shared::game_session_t &session = context.world.session;

  if (rocket.damage_radius <= 0.f) return;

  std::vector<hit_result_t> hits = find_all_bodies_overlapping_sphere(physics, rocket.position, rocket.damage_radius);

  // One body may surface multiple contact points; only push it once.
  std::unordered_set<shared::entity_uid_t> already_pushed;

  for (const auto &h : hits)
  {
    if (h.entity_id == 0) continue;
    if (!already_pushed.insert(h.entity_id).second) continue;

    // The type is asked ONE question -- "is this a player?" -- because that is
    // the only thing the push branches on: a player is a kinematic capsule, so
    // Jolt impulses are no-ops on it and an added velocity is clobbered by the
    // next set_kinematic_pose, which makes its push a game-state write. Every
    // other uid the overlap surfaced has a Jolt body and takes the delta
    // directly, whatever it is -- a crate, a thrown weapon, a damageable.
    entities::Player_Entity *player =
        session.entity_system.get<entities::Player_Entity>(h.entity_id);

    // Measured to the entity center, never to h.position: a surface contact
    // point sits near the origin on a direct hit and gives a degenerate
    // direction. A player's position is at the feet, so it carries the capsule
    // offset; everything else is positioned at its own center.
    vec3f entity_center;
    if (player)
    {
      entity_center = player->position + vec3f{0.f, 38.f, 0.f};
    }
    else if (entities::Entity *entity = session.entity_system.try_find(h.entity_id))
    {
      entity_center = entity->position;
    }
    else
    {
      continue; // a body whose entity is already gone
    }

    const vec3f to_target = entity_center - rocket.position;
    const float distance  = linalg::length(to_target);
    if (distance > rocket.damage_radius) continue;

    // Straight up when the center coincides with the origin: a zero direction
    // would spend the blast on nothing instead of launching the victim.
    const vec3f direction = (distance > 1e-4f) ? to_target * (1.f / distance)
                                               : vec3f{0.f, 1.f, 0.f};
    const float falloff   = 1.f - (distance / rocket.damage_radius);
    const vec3f push      = direction * (rocket.knockback_force * falloff);

    if (player)
      player->velocity = player->velocity + push;
    else
      add_linear_velocity(physics, h.entity_id, push);
  }

  // Cosmetic explosion: announce the detonation through the cosmetic-events
  // channel. The server reports the world-space origin; the client handler
  // does its own Static_Only cast_sphere against its local static geometry to
  // resolve a surface contact for the decal — see plan §"Server emits, client
  // traces locally."
  shared::Rocket_Explosion fx{};
  fx.origin           = rocket.position;
  fx.normal           = impact_normal; // {0,0,0} = airburst, no surface decal
  fx.color            = {1.f, 1.f, 1.f};
  fx.scale            = rocket.damage_radius;
  fx.attached_entity  = 0;
  fx.surface_material = 0;
  shared::fire_rocket_explosion(context.outgoing.effects, fx);

  // Reliable gameplay event for HUD/score/kill-feed consumers. Victim is the
  // direct-hit player only — splash kills get reported via a future
  // PLAYER_DIED event (see plan §"Phase 4"), not back-derived from this one.
  shared::entity_uid_t victim_id = 0;
  if (direct_hit_uid != 0 &&
      session.entity_system.get<entities::Player_Entity>(direct_hit_uid) != nullptr)
    victim_id = direct_hit_uid;

  // Encoded straight into the outgoing stream: no value survives the call, so
  // a kind can never disagree with its payload.
  shared::Rocket_Detonated detonated{};
  detonated.attacker_id = rocket.projectile.owner_uid;
  detonated.victim_id   = victim_id;
  detonated.weapon_id   = static_cast<uint16_t>(rocket.projectile.weapon_id);
  shared::fire_rocket_detonated(context.outgoing.events, detonated);
}

void update_rockets(server_context_t &context, float dt)
{
  shared::game_session_t &session = context.world.session;
  physics_state_t        &physics = *context.world.physics;

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

    const shared::projectile_t& projectile =
        shared::get_weapon_definition(rocket.projectile.weapon_id).projectile;
    const shared::projectile_step_t step = shared::advance_projectile(
        projectile, context.cvars->g_gravity, rocket.position, rocket.projectile.velocity, dt);
    const vec3f next_pos = step.position;
    rocket.projectile.velocity      = step.velocity;

    hit_result_t hit;
    // Everything is a valid target except the player who fired: a rocket that
    // clips its own owner's capsule on the first tick would detonate in their
    // face. Back faces collide so a rocket spawned barely inside geometry
    // still stops rather than sailing through it.
    const query_filter_t filter{.layers     = query_layers_t::All,
                                .ignore_uid = rocket.projectile.owner_uid,
                                .back_faces = back_face_mode_t::Collide};

    if (cast_sphere(physics, rocket.position, next_pos,
                    rocket.collision_radius, filter, hit))
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
