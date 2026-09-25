#include "systems/contact_system.hpp"

#include "../shared/bounce_body.hpp"
#include "../shared/effects/generated/effects_generated.hpp"
#include "../shared/events/generated/events_generated.hpp"
#include "../shared/log.hpp"
#include "../shared/movement_kernel.hpp"
#include "../shared/movement_override.hpp"
#include "../shared/player_constants.hpp"
#include "../shared/player_move.hpp"
#include "../shared/projectile_sweep.hpp"
#include "../shared/subtick.hpp"
#include "../shared/weapons.hpp"
#include "damage.hpp"
#include "entity_lifecycle.hpp"
#include "server_api.hpp"
#include "server_context.hpp"
#include "server_messages.hpp"
#include "systems/game_rules_system.hpp"
#include "systems/inventory_system.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <utility>
#include <vector>

namespace server
{

static constexpr float SHORTEST_PULL_SECONDS     = 0.3f;
static constexpr float LONGEST_PULL_SECONDS      = 1.0f;
static constexpr float LANDING_CLEARANCE         = 1.f;
static constexpr int   LANDING_PUSH_OUT_ATTEMPTS = 4;

static const shared::contact_t& contact_rule_of(const pending_contact_t& contact)
{
  return shared::fire_of(shared::get_weapon_definition(contact.weapon), contact.trigger).contact;
}

// The kinds a body effect can act on. Everything else that stops a shot -- the map, a lift, a
// landed platform, a canopy -- is a surface.
static bool is_body(shared::Entity_System& entity_system, shared::entity_uid_t uid)
{
  if (uid == shared::null_entity_uid)
    return false;
  return entity_system.get<entities::Player_Entity>(uid) != nullptr ||
         entity_system.get<entities::Damageable_Entity>(uid) != nullptr ||
         entity_system.get<entities::Remnant_Entity>(uid) != nullptr ||
         entity_system.get<entities::Physics_Body_Entity>(uid) != nullptr ||
         entity_system.get<entities::Weapon_Entity>(uid) != nullptr;
}

static const char* type_name_of(shared::Entity_System& entity_system, shared::entity_uid_t uid)
{
  const entities::Entity* entity = entity_system.try_find(uid);
  return entity != nullptr ? entities::entity_info(entity->type).display_name : "nothing";
}

// The ONE site that fires Shot_Impact: a bullet in a wall, a bullet in a body. `attached` is
// the body struck, or null for a surface -- a mover's brush is world geometry to the effect.
static void fire_shot_impact_at(server_context_t& context, const pending_contact_t& contact,
                                shared::entity_uid_t attached)
{
  shared::Shot_Impact impact{};
  impact.origin          = contact.point;
  impact.normal          = contact.normal;
  impact.attached_entity = attached;
  impact.region          = static_cast<uint16_t>(contact.region);
  impact.weapon          = static_cast<uint16_t>(contact.weapon);
  impact.trigger         = static_cast<uint8_t>(contact.trigger);
  shared::fire_shot_impact(context.outgoing.effects, impact);
}

// A body effect that found no body of its kind: on a surface that is a bullet in a wall, not a
// failure; on a body of another kind it is refused by name. Never silent.
static void impact_or_refuse(server_context_t& context, const pending_contact_t& contact,
                             const char* acts_on)
{
  shared::Entity_System& entity_system = context.world.session.entity_system;
  if (!is_body(entity_system, contact.target_uid))
  {
    fire_shot_impact_at(context, contact, shared::null_entity_uid);
    return;
  }
  log_terminal("{} contact from uid {} dropped: it acts on {}, and uid {} is a {}",
               to_string(contact_rule_of(contact).effect), contact.shooter_uid, acts_on,
               contact.target_uid, type_name_of(entity_system, contact.target_uid));
}

// player_move works on the hull CENTRE, so a reel aims at the anchor's, not at their feet.
static vec3f reel_anchor_of(const entities::Player_Entity& anchor)
{
  return anchor.position + vec3f{0.f, shared::player_half_height, 0.f};
}

// An override's numbers are written AT ATTACH and are replicated from there, which is what lets
// the reeled player predict their own reel and is why player_move reads no hook cvar.
static void attach_reel(server_context_t& context, entities::Player_Entity& reeled,
                        const entities::Player_Entity& anchor, float seconds)
{
  reeled.movement.active_override            = entities::Movement_Override::Reel;
  reeled.movement.override_target_uid        = anchor.entity_id;
  reeled.movement.override_target_position   = reel_anchor_of(anchor);
  reeled.movement.override_seconds_remaining = seconds;
  reeled.movement.override_speed             = context.cvars->sv_hook_pull_speed;
  reeled.movement.override_arrive_radius     = context.cvars->sv_hook_arrive_radius;
}

// The arc that lands `thrown` on `destination` after the flight time, gravity included.
static void throw_toward(server_context_t& context, entities::Player_Entity& thrown,
                         const entities::Player_Entity& destination)
{
  const vec3f to_destination = destination.position - thrown.position;
  const float flight_seconds =
      std::clamp(linalg::length(to_destination) / context.cvars->sv_hook_pull_speed,
                 SHORTEST_PULL_SECONDS, LONGEST_PULL_SECONDS);

  shared::apply_impulse(shared::movement_settings_from(*context.cvars), thrown.velocity,
                        thrown.movement,
                        {.velocity = to_destination * (1.f / flight_seconds) +
                                     vec3f{0.f, 0.5f * context.cvars->g_gravity * flight_seconds,
                                           0.f}});
}

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

// ---------------------------------------------------------------------------
// The arms
// ---------------------------------------------------------------------------

static void apply_damage_contact(server_context_t& context, const pending_contact_t& contact,
                                 const shared::contact_damage_t& rule,
                                 std::vector<pending_hit_t>& damage)
{
  shared::Entity_System& entity_system = context.world.session.entity_system;

  const bool acts = entity_system.get<entities::Player_Entity>(contact.target_uid) != nullptr ||
                    entity_system.get<entities::Damageable_Entity>(contact.target_uid) != nullptr ||
                    entity_system.get<entities::Physics_Body_Entity>(contact.target_uid) != nullptr;
  if (!acts)
  {
    impact_or_refuse(context, contact, "a player, a damageable or a physics body");
    return;
  }

  if (entity_system.get<entities::Player_Entity>(contact.target_uid) != nullptr)
    broadcast_server_text_message(
        context, std::format("Player {} hit player {} in the {}", contact.shooter_uid,
                             contact.target_uid, to_string(contact.region)));

  const entities::Player_Entity* shooter =
      entity_system.get<entities::Player_Entity>(contact.shooter_uid);
  const bool was_headshot = contact.region == shared::hit_region_t::Head;

  pending_hit_t hit{};
  hit.info.victim_uid      = contact.target_uid;
  hit.info.attacker_uid    = contact.shooter_uid;
  hit.info.inflictor_uid   = contact.shooter_uid;
  hit.info.weapon_id       = static_cast<uint16_t>(contact.weapon);
  hit.info.amount          = rule.amount * (was_headshot ? rule.headshot_multiplier : 1.f);
  hit.info.source_position = shooter != nullptr
                                 ? shooter->position + vec3f{0.f, shared::player_eye_height, 0.f}
                                 : contact.point;
  hit.info.type            = contact.damage_type;
  hit.info.was_headshot    = was_headshot;
  hit.impact_point         = contact.point;
  hit.impact_normal        = contact.normal;
  hit.region               = contact.region;
  damage.push_back(hit);
}

static void apply_swap(server_context_t& context, const pending_contact_t& contact)
{
  shared::Entity_System& entity_system = context.world.session.entity_system;

  entities::Player_Entity* target = entity_system.get<entities::Player_Entity>(contact.target_uid);
  if (target == nullptr)
  {
    impact_or_refuse(context, contact, "a player");
    return;
  }
  entities::Player_Entity* shooter = entity_system.get<entities::Player_Entity>(contact.shooter_uid);
  if (shooter == nullptr)
  {
    log_error("swap from uid {} to uid {} dropped: the shooter is no longer a player",
              contact.shooter_uid, contact.target_uid);
    return;
  }
  if (target->health.current_health <= 0)
    return;

  std::swap(shooter->position, target->position);
  std::swap(shooter->velocity, target->velocity);
}

// The remnant is SPENT: a teleport is a return to where you stood, and standing there again is
// the next set. Velocity is kept, so a fall or a run carries.
static void apply_teleport(server_context_t& context, const pending_contact_t& contact)
{
  shared::Entity_System& entity_system = context.world.session.entity_system;

  const entities::Remnant_Entity* remnant =
      entity_system.get<entities::Remnant_Entity>(contact.target_uid);
  if (remnant == nullptr)
  {
    impact_or_refuse(context, contact, "one of the shooter's own remnants");
    return;
  }
  if (remnant->owner_uid != contact.shooter_uid)
  {
    log_error("teleport of uid {} to remnant uid {} dropped: that remnant belongs to uid {}",
              contact.shooter_uid, contact.target_uid, remnant->owner_uid);
    return;
  }
  entities::Player_Entity* shooter = entity_system.get<entities::Player_Entity>(contact.shooter_uid);
  if (shooter == nullptr)
  {
    log_error("teleport of uid {} to remnant uid {} dropped: the shooter is no longer a player",
              contact.shooter_uid, contact.target_uid);
    return;
  }
  if (shooter->health.current_health <= 0)
    return;

  shooter->position = remnant->position;
  destroy_entity(context, contact.target_uid);
}

static void apply_magnet(server_context_t& context, const pending_contact_t& contact,
                         const shared::contact_magnet_t& rule)
{
  shared::game_session_t& session       = context.world.session;
  shared::Entity_System&  entity_system = session.entity_system;

  entities::Player_Entity* target = entity_system.get<entities::Player_Entity>(contact.target_uid);
  if (target == nullptr)
  {
    impact_or_refuse(context, contact, "a player");
    return;
  }
  entities::Player_Entity* shooter = entity_system.get<entities::Player_Entity>(contact.shooter_uid);
  if (shooter == nullptr)
    return;
  if (shooter->health.current_health <= 0 || target->health.current_health <= 0)
    return;
  if (!carries_weapon(session, *target, entities::Weapon::Magnet))
    return;

  const vec3f to_target = target->position - shooter->position;
  const float distance  = linalg::length(to_target);
  if (distance < 1.f)
    return;

  const vec3f toward_target = to_target * (rule.speed / distance);
  const shared::movement_settings_t settings = shared::movement_settings_from(*context.cvars);

  shared::apply_impulse(settings, shooter->velocity, shooter->movement,
                        {.horizontal = shared::impulse_mode_t::Add,
                         .vertical   = shared::impulse_mode_t::Add,
                         .velocity   = toward_target});
  shared::apply_impulse(settings, target->velocity, target->movement,
                        {.horizontal = shared::impulse_mode_t::Add,
                         .vertical   = shared::impulse_mode_t::Add,
                         .velocity   = toward_target * -1.f});
}

// Reel and Throw share their cast: the shooter and the player hit, both alive, and the
// subject says which of the two moves.
struct reel_cast_t
{
  entities::Player_Entity* moved;
  entities::Player_Entity* anchor;
};

[[nodiscard]] static std::optional<reel_cast_t> try_cast_of(server_context_t& context,
                                                            const pending_contact_t& contact,
                                                            shared::contact_subject_t subject,
                                                            const char* effect_name)
{
  shared::Entity_System& entity_system = context.world.session.entity_system;

  entities::Player_Entity* target = entity_system.get<entities::Player_Entity>(contact.target_uid);
  if (target == nullptr)
  {
    impact_or_refuse(context, contact, "a player");
    return std::nullopt;
  }
  entities::Player_Entity* shooter = entity_system.get<entities::Player_Entity>(contact.shooter_uid);
  if (shooter == nullptr)
  {
    log_error("{} from uid {} onto uid {} dropped: the shooter is no longer a player", effect_name,
              contact.shooter_uid, contact.target_uid);
    return std::nullopt;
  }
  if (shooter->health.current_health <= 0 || target->health.current_health <= 0)
    return std::nullopt;

  switch (subject)
  {
  case shared::contact_subject_t::Target:  return reel_cast_t{.moved = target, .anchor = shooter};
  case shared::contact_subject_t::Shooter: return reel_cast_t{.moved = shooter, .anchor = target};
  }
  return std::nullopt;
}

// A hit RENEWS a reel already running rather than pushing: the pull itself is player_move's, so
// the reeled player predicts it.
static void apply_reel(server_context_t& context, const pending_contact_t& contact,
                       const shared::contact_reel_t& rule)
{
  if (const std::optional<reel_cast_t> cast = try_cast_of(context, contact, rule.subject, "Reel"))
    attach_reel(context, *cast->moved, *cast->anchor, rule.seconds);
}

static void apply_throw(server_context_t& context, const pending_contact_t& contact,
                        const shared::contact_throw_t& rule)
{
  if (const std::optional<reel_cast_t> cast = try_cast_of(context, contact, rule.subject, "Throw"))
    throw_toward(context, *cast->moved, *cast->anchor);
}

// A hit on a player already frozen RELEASES them: the clock is spent and the next step thaws
// through the one exit door, with the velocity the freeze holds. A Statue is zeroed at attach;
// the step then falls it (movement_override.hpp).
static void apply_freeze(server_context_t& context, const pending_contact_t& contact,
                         const shared::contact_freeze_t& rule)
{
  entities::Player_Entity* target =
      context.world.session.entity_system.get<entities::Player_Entity>(contact.target_uid);
  if (target == nullptr)
  {
    impact_or_refuse(context, contact, "a player");
    return;
  }
  if (target->health.current_health <= 0)
    return;

  if (shared::override_freezes(target->movement.active_override))
  {
    target->movement.override_seconds_remaining = 0.f;
    return;
  }

  target->movement.active_override            = rule.kind;
  target->movement.override_target_uid        = contact.shooter_uid;
  target->movement.override_seconds_remaining = rule.seconds;

  if (rule.kind == entities::Movement_Override::Statue)
    shared::apply_impulse(shared::movement_settings_from(*context.cvars), target->velocity,
                          target->movement,
                          {.horizontal = shared::impulse_mode_t::Set,
                           .vertical   = shared::impulse_mode_t::Set,
                           .velocity   = {}});
}

// Positional: the splash pushes everything a projectile can land on within the radius, measured
// to each box's CENTER, and does not care what stopped the shot. A zero normal is an airburst.
static void apply_explode(server_context_t& context, const pending_contact_t& contact,
                          const shared::contact_explode_t& rule)
{
  shared::Entity_System& entity_system = context.world.session.entity_system;

  std::vector<shared::projectile_target_t> targets;
  shared::collect_projectile_targets(entity_system, targets);

  for (const shared::projectile_target_t& target : targets)
  {
    const vec3f entity_center = (target.bounds.min + target.bounds.max) * 0.5f;
    const vec3f to_target     = entity_center - contact.point;
    const float distance      = linalg::length(to_target);
    if (distance > rule.radius)
      continue;

    // Straight up when the center coincides with the origin: a zero direction would spend the
    // blast on nothing instead of launching the victim.
    const vec3f direction = distance > 1e-4f ? to_target * (1.f / distance) : vec3f{0.f, 1.f, 0.f};
    const float falloff   = 1.f - distance / rule.radius;
    const vec3f push      = direction * (rule.knockback * falloff);

    if (entities::Player_Entity* player = entity_system.get<entities::Player_Entity>(target.uid))
      shared::apply_impulse(shared::movement_settings_from(*context.cvars), player->velocity,
                            player->movement,
                            {.horizontal = shared::impulse_mode_t::Add,
                             .vertical   = shared::impulse_mode_t::Add,
                             .velocity   = push});
    else if (entities::Physics_Body_Entity* body =
                 entity_system.get<entities::Physics_Body_Entity>(target.uid))
      shared::wake_bounce_body(body->bounce, push);
    else if (entities::Weapon_Entity* weapon = entity_system.get<entities::Weapon_Entity>(target.uid))
      shared::wake_bounce_body(weapon->bounce, push);
  }

  shared::Rocket_Explosion explosion{};
  explosion.origin           = contact.point;
  explosion.normal           = contact.normal;
  explosion.color            = {1.f, 1.f, 1.f};
  explosion.scale            = rule.radius;
  explosion.attached_entity  = 0;
  explosion.surface_material = 0;
  shared::fire_rocket_explosion(context.outgoing.effects, explosion);

  shared::Rocket_Detonated detonated{};
  detonated.attacker_id = contact.shooter_uid;
  detonated.victim_id   = entity_system.get<entities::Player_Entity>(contact.target_uid) != nullptr
                              ? contact.target_uid
                              : shared::null_entity_uid;
  detonated.weapon_id   = static_cast<uint16_t>(contact.weapon);
  shared::fire_rocket_detonated(context.outgoing.events, detonated);
}

// The shooter is sent to a hull resting on whatever was struck. Velocity is kept, the teleport's rule.
static void apply_land(server_context_t& context, const shared::predicted_world_storage_t& world,
                       const pending_contact_t& contact)
{
  entities::Player_Entity* owner =
      context.world.session.entity_system.get<entities::Player_Entity>(contact.shooter_uid);
  if (owner == nullptr)
  {
    log_terminal("Land contact dropped: its owner uid {} is not a player, nobody is sent",
                 contact.shooter_uid);
    return;
  }
  if (owner->health.current_health <= 0)
    return;

  // Every sweep normal is unit; up is the fallback for one that somehow is not.
  const vec3f normal = linalg::length(contact.normal) > 0.5f ? linalg::normalize(contact.normal)
                                                             : vec3f{0.f, 1.f, 0.f};

  const std::optional<vec3f> feet = try_find_landing_feet(
      context.world.session.bvh, shared::predicted_world_of(world, owner->team_allegiance),
      contact.point, normal);
  if (!feet)
  {
    log_warning("Land contact at ({:.0f}, {:.0f}, {:.0f}) found no hull that fits there: owner "
                "uid {} stays put",
                contact.point.x, contact.point.y, contact.point.z, owner->entity_id);
    return;
  }

  owner->position = *feet;
}

// The zone at construction, centred on the contact; update_timed_movement_modifiers stamps it this same tick.
static void apply_leave_zone(server_context_t& context, const pending_contact_t& contact)
{
  shared::Entity_System& entity_system = context.world.session.entity_system;

  const shared::entity_uid_t zone_uid =
      entity_system.spawn(entities::entity_type::Timed_Movement_Modifier_Entity);
  entities::Timed_Movement_Modifier_Entity* zone =
      entity_system.get<entities::Timed_Movement_Modifier_Entity>(zone_uid);
  if (zone == nullptr)
  {
    log_error("Leave_Zone contact from uid {} dropped: no room to spawn the zone",
              contact.shooter_uid);
    return;
  }

  zone->position             = contact.point;
  zone->projectile.owner_uid = contact.shooter_uid;
  zone->projectile.weapon_id = contact.weapon;
  zone->projectile.trigger   = contact.trigger;
}

static void apply_contact(server_context_t& context, const shared::predicted_world_storage_t& world,
                          const pending_contact_t& contact, std::vector<pending_hit_t>& damage)
{
  const shared::contact_t& rule = contact_rule_of(contact);
  switch (rule.effect)
  {
  case shared::contact_effect_t::None:
    log_error("contact from uid {} dropped: {}'s {} button carries no contact effect, so nothing "
              "should have pushed one",
              contact.shooter_uid, shared::get_weapon_definition(contact.weapon).display_name,
              to_string(contact.trigger));
    return;
  case shared::contact_effect_t::Damage:     apply_damage_contact(context, contact, rule.damage, damage); return;
  case shared::contact_effect_t::Swap:       apply_swap(context, contact); return;
  case shared::contact_effect_t::Magnet:     apply_magnet(context, contact, rule.magnet); return;
  case shared::contact_effect_t::Reel:       apply_reel(context, contact, rule.reel); return;
  case shared::contact_effect_t::Throw:      apply_throw(context, contact, rule.throw_); return;
  case shared::contact_effect_t::Teleport:   apply_teleport(context, contact); return;
  case shared::contact_effect_t::Freeze:     apply_freeze(context, contact, rule.freeze); return;
  case shared::contact_effect_t::Explode:    apply_explode(context, contact, rule.explode); return;
  case shared::contact_effect_t::Land:       apply_land(context, world, contact); return;
  case shared::contact_effect_t::Leave_Zone: apply_leave_zone(context, contact); return;
  }
}

// Damage LAST, as one batch, so a swap-then-kill in one tick resolves where the swap put them
// and two shooters on one victim both count (inflict_damage_batch).
static void apply_damage(server_context_t& context, Span<const pending_hit_t> hits)
{
  for (const pending_hit_t& hit : hits)
  {
    pending_contact_t landed{};
    landed.target_uid = hit.info.victim_uid;
    landed.point      = hit.impact_point;
    landed.normal     = hit.impact_normal;
    landed.region     = hit.region;
    landed.weapon     = static_cast<entities::Weapon>(hit.info.weapon_id);
    fire_shot_impact_at(context, landed, hit.info.victim_uid);

    // The hitmarker, for the shooter only, as replicated state. Their own client plays it off
    // this stamp advancing -- see Player_Entity::last_hit_tick in entities.def for why it is not
    // an effect. Every contributor gets one, not just the one credited with the kill.
    //
    // Gated on the same query the health write is: a hitmarker is a claim that damage landed,
    // so outside the round it would be feedback for a hit that did nothing. The Shot_Impact
    // above is NOT gated -- it says where the bullet went, which is true either way.
    entities::Player_Entity* attacker =
        can_take_damage(context)
            ? context.world.session.entity_system.get<entities::Player_Entity>(hit.info.attacker_uid)
            : nullptr;
    if (attacker != nullptr)
    {
      attacker->last_hit_tick         = context.tick_number;
      attacker->last_hit_was_headshot = hit.info.was_headshot;
    }
  }

  inflict_damage_batch(context, hits);
}

static void finish_reloads_that_came_due(server_context_t& context)
{
  const shared::subtick_time_t end_of_tick =
      shared::subtick_time(context.tick_number + 1, 0);
  for (entities::Player_Entity& player :
       context.world.session.entity_system.entities_of<entities::Player_Entity>())
  {
    if (is_reloading(player) && player.reload_complete_time <= end_of_tick)
      finish_reload(context.world.session, player);
  }
}

// The reel's other half: player_move is pure and cannot resolve a uid, so the anchor travels as
// a position and this is the ONE place it is written. An anchor who left, died or dropped the
// hook detaches here rather than leaving the reeled player pulling toward a stale point --
// through the door, so the exit is the one every override exit is.
static void refresh_reel_anchors(server_context_t& context)
{
  shared::Entity_System& entity_system = context.world.session.entity_system;

  for (entities::Player_Entity& reeled : entity_system.entities_of<entities::Player_Entity>())
  {
    if (reeled.movement.active_override != entities::Movement_Override::Reel)
      continue;

    const entities::Player_Entity* anchor =
        entity_system.get<entities::Player_Entity>(reeled.movement.override_target_uid);

    if (anchor == nullptr || anchor->health.current_health <= 0)
    {
      reeled.movement.active_override            = entities::Movement_Override::None;
      reeled.movement.override_target_uid        = shared::null_entity_uid;
      reeled.movement.override_seconds_remaining = 0.f;
      shared::apply_impulse(shared::movement_settings_from(*context.cvars), reeled.velocity,
                            reeled.movement, {.velocity = reeled.velocity});
      continue;
    }

    reeled.movement.override_target_position = reel_anchor_of(*anchor);
  }
}

void update_contacts(server_context_t& context, const shared::predicted_world_storage_t& world)
{
  std::vector<pending_hit_t> damage;
  for (const pending_contact_t& contact : context.outgoing.pending_contacts)
    apply_contact(context, world, contact, damage);
  apply_damage(context, Span<const pending_hit_t>{damage});
  context.outgoing.pending_contacts.clear();

  finish_reloads_that_came_due(context);
  refresh_reel_anchors(context);
}

} // namespace server
