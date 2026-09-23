#include "../../shared/bounce_body.hpp"
#include "../../shared/entities/entity_reflection.hpp"
#include "rocket_system.hpp"
#include "projectile_flight.hpp"

#include "../../shared/linalg.hpp"
#include "../../shared/log.hpp"
#include "../../shared/weapons.hpp"
#include "../entity_lifecycle.hpp"
#include "../server_api.hpp"

#include <vector>

namespace server
{

//@FIXME(SJM): the way this works is not ideal and does not feel good.

static void detonate(const entities::Rocket_Entity &rocket,
                     server_context_t &context,
                     shared::entity_uid_t direct_hit_uid,
                     vec3f impact_normal)
{
  shared::game_session_t &session = context.world.session;

  if (rocket.damage_radius <= 0.f) return;

  // Everything a projectile can land on is everything a blast can push, measured
  // to each box's CENTER: a player's position is at the feet, and a contact
  // point on a direct hit sits on the origin and gives a degenerate direction.
  std::vector<shared::projectile_target_t> targets;
  shared::collect_projectile_targets(session.entity_system, targets);

  for (const shared::projectile_target_t &target : targets)
  {
    const vec3f entity_center = (target.bounds.min + target.bounds.max) * 0.5f;
    const vec3f to_target     = entity_center - rocket.position;
    const float distance      = linalg::length(to_target);
    if (distance > rocket.damage_radius) continue;

    // Straight up when the center coincides with the origin: a zero direction
    // would spend the blast on nothing instead of launching the victim.
    const vec3f direction = (distance > 1e-4f) ? to_target * (1.f / distance)
                                               : vec3f{0.f, 1.f, 0.f};
    const float falloff   = 1.f - (distance / rocket.damage_radius);
    const vec3f push      = direction * (rocket.knockback_force * falloff);

    // A player's push is a game-state write; a crate's or a dropped weapon's
    // wakes its bounce body. A damageable is static and takes none.
    if (entities::Player_Entity *player =
            session.entity_system.get<entities::Player_Entity>(target.uid))
    {
      shared::apply_impulse(shared::movement_settings_from(*context.cvars), player->velocity,
                            player->movement,
                            {.horizontal = shared::impulse_mode_t::Add,
                             .vertical   = shared::impulse_mode_t::Add,
                             .velocity   = push});
    }
    else if (entities::Physics_Body_Entity *body =
                 session.entity_system.get<entities::Physics_Body_Entity>(target.uid))
    {
      shared::wake_bounce_body(body->bounce, push);
    }
    else if (entities::Weapon_Entity *weapon =
                 session.entity_system.get<entities::Weapon_Entity>(target.uid))
    {
      shared::wake_bounce_body(weapon->bounce, push);
    }
  }

  // Cosmetic explosion: announce the detonation through the cosmetic-events
  // channel. The server reports the world-space origin; the client handler
  // probes its own BVH along the normal to resolve a surface contact for the
  // decal -- see plan §"Server emits, client traces locally."
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

void update_rockets(server_context_t &context, const shared::predicted_world_storage_t& world,
                    float dt)
{
  shared::game_session_t &session = context.world.session;
  Span<entities::Rocket_Entity> rockets =
      session.entity_system.entities_of<entities::Rocket_Entity>();
  if (rockets.empty())
    return;

  // Collected as uids, not slot indices. Removal is swap-and-pop, so a slot
  // index recorded during the walk names a DIFFERENT rocket after the first
  // removal -- which is why this used to have to sort descending and dedupe. A
  // uid names the same entity no matter what moved.
  std::vector<shared::entity_uid_t> uids_to_remove;

  std::vector<shared::projectile_target_t> targets;
  shared::collect_projectile_targets(session.entity_system, targets);

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

    if (const std::optional<shared::projectile_hit_t> hit = fly_projectile(
            context, world, targets, rocket, rocket.projectile, rocket.collision_radius, dt))
    {
      detonate(rocket, context, hit->entity_uid, hit->normal);
      uids_to_remove.push_back(rocket.entity_id);
    }
  }

  // `rockets` (and every reference taken from it above) is dead from here on --
  // each destroy swap-and-pops the pool it points into.
  for (shared::entity_uid_t uid : uids_to_remove)
    destroy_entity(context, uid);
}

} // namespace server
