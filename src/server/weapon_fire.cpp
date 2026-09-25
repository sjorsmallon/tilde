#include "weapon_fire.hpp"

#include "../shared/hitscan.hpp"
#include "../shared/lag_compensation.hpp"
#include "../shared/log.hpp"
#include "../shared/network/snapshot_history.hpp"
#include "../shared/player_animator.hpp"
#include "../shared/player_constants.hpp"
#include "../shared/player_rig.hpp"
#include "../shared/projectile_sweep.hpp"
#include "../shared/remnant.hpp"
#include "entities/remnant_entity.hpp"
#include "send_protobuf_message.hpp"
#include "server_api.hpp"
#include "server_messages.hpp"
#include "systems/inventory_system.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <vector>

namespace server
{

//@NOTE(SJM what the  fuck does this mean
static void append_static_targets(const shared::posed_players_t &present,
                                  shared::posed_players_t       &rewound)
{
  for (size_t index = present.poses.size(); index < present.targets.size(); ++index)
    rewound.targets.push_back(present.targets[index]);
}

// what was the situation at the client's location (what view were they aiming through.) and was it valid? not cheated?

static shared::bracket_verdict_t get_interpolation_bracket_for_input(
    server_context_t &context, int32_t client_slot,
    const game::C2S_ClientInput &input)
{
  client_slot_t &client = context.clients[client_slot];

  const shared::interpolation_bracket_t requested{
      .from_tick    = input.interpolated_from_tick(),
      .towards_tick = input.interpolated_towards_tick(),
      .fraction     = input.interpolation_fraction()};

  // check if the requested brakcet is not accidentally outside of the ring bucket already.
  const int32_t configured_max_rewind_ticks = context.cvars->sv_max_rewind_ticks;
  const uint32_t max_rewind =
      std::min<uint32_t>(configured_max_rewind_ticks < 0 ? 0u : (uint32_t)configured_max_rewind_ticks,
                         network::Snapshot_History<network::snapshot_frame_t>::CAPACITY - 1);

  // check validity.
  const shared::bracket_verdict_t verdict = shared::classify_bracket(
      requested, client.held_snapshot_tick, context.tick_number, max_rewind);

  switch (verdict.status)
  {
    case shared::bracket_status_t::Absent:
      return verdict;

    case shared::bracket_status_t::Unheld:
      log_warning("slot {}: move names a blend towards tick {}, but that slot has "
                  "only acked up to {} (server is at {}) — rewind refused",
                  client_slot, requested.towards_tick, client.held_snapshot_tick,
                  context.tick_number);
      return verdict;

    case shared::bracket_status_t::Malformed:
      log_warning("slot {}: malformed interpolation bracket {} -> {} at {:.3f} — "
                  "rewind refused",
                  client_slot, requested.from_tick, requested.towards_tick,
                  requested.fraction);
      return verdict;

    case shared::bracket_status_t::Ok:
    case shared::bracket_status_t::Clamped:
      break;
  }

  const float milliseconds_per_tick = 1000.f * static_cast<float>(get_tick_interval());

  // how far back in time did we actually go? 
  const uint32_t bracket_span_ticks = verdict.bracket.towards_tick - verdict.bracket.from_tick;
  const float rewind_tick_count =
      static_cast<float>(context.tick_number - verdict.bracket.towards_tick) +
      (1.f - verdict.bracket.fraction) * static_cast<float>(bracket_span_ticks);

  // debug
  {
    if (context.cvars->sv_lag_compensation_debug)
        log_terminal("[rewind] slot {}: asked {} -> {} at {:.3f}, using {} -> {}, reaching "
                     "back {:.2f} ticks ({:.1f} ms)",
                     client_slot, requested.from_tick, requested.towards_tick, requested.fraction,
                     verdict.bracket.from_tick, verdict.bracket.towards_tick, rewind_tick_count,
                     rewind_tick_count * milliseconds_per_tick);

  }
  
  //@NOTE(SJM): what does clamped mean actually -> clamped means that you could start from a thing that's already out of range. you can then clamp to the oldest thing that the server has. Now i'm not sure if that is even valid or just stupid but it's what we are doing.
  if (verdict.status == shared::bracket_status_t::Clamped)
  {
    
    const uint32_t warning_interval_ticks =
        std::max(1u, static_cast<uint32_t>(context.cvars->sv_tickrate));
    if (client.last_rewind_warning_tick == 0 ||
        context.tick_number - client.last_rewind_warning_tick >= warning_interval_ticks)
    {
      client.last_rewind_warning_tick = context.tick_number;
      log_warning("slot {}: rewind clamped by sv_max_rewind_ticks ({}) — asked for "
                  "{} -> {}, judged through {} -> {}; that shot reaches back {:.2f} "
                  "ticks ({:.1f} ms)",
                  client_slot, max_rewind, requested.from_tick, requested.towards_tick,
                  verdict.bracket.from_tick, verdict.bracket.towards_tick, rewind_tick_count,
                  rewind_tick_count * milliseconds_per_tick);
    }
  }

  return verdict;
}

// helper to do some debug drawing for if you shoot near a target. otherwise only registered hits would show shots. it's nice to see why something missed too.
static float distance_to_nearest_target(const vec3f& eye, const vec3f& direction,
                                        Span<const shared::hitscan_target_t> targets,
                                        shared::entity_uid_t shooter_uid)
{
  float nearest = std::numeric_limits<float>::infinity();
  for (const shared::hitscan_target_t& target : targets)
  {
    // ignore the client.
    if (target.uid == shooter_uid) continue;

    // clamp at 0 so anyone behind you does not show up as the closest.
    const vec3f to_center     = target.bounds.center - eye;
    const float along_ray     = std::max(0.f, linalg::dot(to_center, direction));
    const vec3f closest_point = eye + direction * along_ray;

    nearest = std::min(nearest, linalg::length(target.bounds.center - closest_point) -
                                    target.bounds.radius);
  }
  return nearest;
}

static void fill_shot_debug_vector(game::Vec3* out, const vec3f& value)
{
  out->set_x(value.x);
  out->set_y(value.y);
  out->set_z(value.z);
}


static void send_shot_debug(
  server_context_t &context,
  int32_t client_slot,
  const game::C2S_ClientInput &input,
  uint32_t fire_subtick_slot,
  const vec3f& eye,
  const vec3f& direction,
  const shared::bracket_verdict_t &verdict,
  bool used_rewind,
  const shared::posed_players_t &tested,
  const Span<const shared::hitscan_target_t> targets,
  const shared::hitscan_result_t &hit,
  shared::entity_uid_t shooter_uid)
{
  game::S2C_ShotDebug message;
  message.set_input_number(input.input_number());
  message.set_server_tick(context.tick_number);
  message.set_fire_subtick_slot(fire_subtick_slot);
  fill_shot_debug_vector(message.mutable_eye(), eye);
  fill_shot_debug_vector(message.mutable_direction(), direction);

  message.set_bracket_status(static_cast<uint32_t>(verdict.status));
  message.set_used_rewind(used_rewind);
  message.set_requested_from_tick(input.interpolated_from_tick());
  message.set_requested_towards_tick(input.interpolated_towards_tick());
  message.set_requested_fraction(input.interpolation_fraction());
  message.set_used_from_tick(verdict.bracket.from_tick);
  message.set_used_towards_tick(verdict.bracket.towards_tick);
  message.set_used_fraction(verdict.bracket.fraction);

  // `poses` is filled in lockstep with `targets` by both builders, so index i
  // describes the same player in each. Guarded anyway: a builder that ever
  // stopped filling one of them would otherwise walk off the end here.
  const size_t target_count = std::min(tested.targets.size(), tested.poses.size());
  for (size_t index = 0; index < target_count; ++index)
  {
    game::ShotDebugTarget *entry = message.add_targets();
    entry->set_player_uid(tested.targets[index].uid);
    fill_shot_debug_vector(entry->mutable_feet_position(), tested.poses[index].feet_position);
    entry->set_body_yaw(tested.poses[index].body_yaw);
    entry->set_view_yaw(tested.poses[index].view_yaw);
    entry->set_view_pitch(tested.poses[index].view_pitch);
  }

  message.set_hit_uid(hit.hit_uid);
  message.set_hit_region(static_cast<uint32_t>(hit.region));
  if (hit.hit_uid != shared::null_entity_uid)
    fill_shot_debug_vector(message.mutable_impact_point(), hit.impact_point);
  else
    message.set_nearest_miss_distance(
        distance_to_nearest_target(eye, direction, targets, shooter_uid));
  
  const network::Address &client_address =
      context.transport_layer.clients[client_slot].address;

  ::send_protobuf_message(context, client_address, message);
}

[[nodiscard]] static bool try_begin_shot(server_context_t &context, int32_t client_slot,
                                         entities::Player_Entity &player,
                                         entities::Weapon_Entity &active_weapon,
                                         const shared::weapon_definition_t &weapon,
                                         shared::subtick_time_t fire_time)
{
  const float seconds_per_subtick_slot =
      static_cast<float>(get_tick_interval()) / static_cast<float>(shared::SUBTICK_SLOT_COUNT);

  // can't fire because it's not time yet.
  if (fire_time < active_weapon.next_fire_time)
  {
    log_terminal("slot {}: {} trigger refused, fire interval has {:.3f}s left", client_slot,
                 weapon.display_name,
                 static_cast<float>(active_weapon.next_fire_time - fire_time) * seconds_per_subtick_slot);
    return false;
  }

  // can't fire because weapon is still deploying.
  if (fire_time < player.inventory.deploy_complete_time)
  {
    log_terminal("slot {}: {} trigger refused, still deploying for {:.3f}s", client_slot,
                 weapon.display_name,
                 static_cast<float>(player.inventory.deploy_complete_time - fire_time) *
                     seconds_per_subtick_slot);
    return false;
  }

  // we're reloading.
  if (is_reloading(player))
  {
    // can't fire because we're not done reloading.
    if (fire_time < player.reload_complete_time) return false;


    finish_reload(context.world.session, player);
  }

  // can't fire because we're out of ammo.
  if (!shared::ammo_allows_a_shot(active_weapon.ammo))
  {
    const uint32_t warning_interval =
        std::max(1u, static_cast<uint32_t>(context.cvars->sv_tickrate));
    if (context.tick_number - player.last_empty_fire_warning_tick >= warning_interval)
    {
      player.last_empty_fire_warning_tick = context.tick_number;
      log_warning("slot {}: trigger pulled on an empty {} — no shot resolved",
                  client_slot, weapon.display_name);
    }
    return false;
  }

  if (active_weapon.ammo > 0)
    --active_weapon.ammo;

  active_weapon.next_fire_time = shared::subtick_time_after(
      fire_time, weapon.fire_interval_seconds, static_cast<float>(get_tick_interval()));
  return true;
}


void mark_shot_fired(const server_context_t &context, entities::Player_Entity &player,
                            entities::Weapon weapon_id)
{
  player.last_fire_tick   = context.tick_number;
  player.last_fire_weapon = weapon_id;
}

std::optional<shared::subtick_time_t>
try_find_held_fire_time(shared::game_session_t& session, const entities::Player_Entity& player,
                        entities::Fire_Trigger trigger, shared::subtick_time_t step_start,
                        shared::subtick_time_t step_end)
{
  const entities::Weapon_Entity* active_weapon = try_find_active_weapon(session, player);
  if (active_weapon == nullptr)
    return std::nullopt;

  const shared::weapon_definition_t& weapon = shared::get_weapon_definition(active_weapon->weapon_id);
  return shared::try_find_held_fire_time(shared::fire_of(weapon, trigger),
                                         active_weapon->next_fire_time,
                                         player.inventory.deploy_complete_time, step_start, step_end);
}

// The volumes and targets a shot at the shooter's own remnants is tested against. The targets
// point into the volumes, so the volumes are filled first and never grow after.
struct remnant_targets_t
{
  std::vector<assets::posed_hitbox_t>   volumes;
  std::vector<shared::hitscan_target_t> targets;
};

// What a delivery may land on, from the contact's declaration and nothing else
// (contact_effect_plan.md D3). Bodies is the tick's posed set, which the caller may rewind.
// Own_Remnants is the shooter's remnants: a remnant is a marker, not a body, so it never
// soaks a bullet, nobody else's can be aimed at, and it does not move, so there is nothing
// to rewind; the volume is the one the hitbox overlay draws.
static Span<const shared::hitscan_target_t> hitscan_targets_of(server_context_t& context,
                                                               const shared::contact_t& contact,
                                                               const entities::Player_Entity& shooter,
                                                               remnant_targets_t& remnants)
{
  switch (contact.targets)
  {
  case shared::contact_targets_t::Bodies:
    return Span<const shared::hitscan_target_t>{context.posed_players.targets};

  case shared::contact_targets_t::Own_Remnants:
  {
    std::vector<shared::entity_uid_t> uids;
    for (const entities::Remnant_Entity& remnant :
         context.world.session.entity_system.entities_of<entities::Remnant_Entity>())
    {
      if (remnant.owner_uid != shooter.entity_id)
        continue;
      uids.push_back(remnant.entity_id);
      remnants.volumes.push_back(shared::remnant_hit_volume(remnant));
    }
    remnants.targets.reserve(uids.size());
    for (size_t index = 0; index < uids.size(); ++index)
      remnants.targets.push_back(shared::make_hitscan_target(
          uids[index], Span<const assets::posed_hitbox_t>{remnants.volumes.data() + index, 1}));
    return Span<const shared::hitscan_target_t>{remnants.targets};
  }
  }
  return {};
}

void resolve_player_shot(
  server_context_t &context, int32_t client_slot,
  const game::C2S_ClientInput &input,
  const shared::predicted_world_t& world,
  entities::Player_Entity* player,
  float yaw, float pitch,
  shared::subtick_time_t fire_time,
  entities::Fire_Trigger trigger)
{

  // this tripped me up 15 different times, so here we go again.
  // POST-move eye, against start-of-tick or rewound victims. The asymmetry
  // is deliberate and it is what the client's screen looks like: prediction
  // runs this same move before the frame is drawn, so the camera sits at the
  // post-move position (play_state.cpp, prediction.player_position), while
  // remote players are drawn interpolated in the PAST. Sampling the shooter
  // pre-move would reconstruct a view nobody saw.

  const vec3f direction = linalg::direction_from_angles(yaw, pitch);
  const vec3f eye = player->position + vec3f{0.f, shared::player_eye_height, 0.f};

  entities::Weapon_Entity* active_weapon = try_find_active_weapon(context.world.session, *player);

  if (active_weapon == nullptr) return;

  const shared::weapon_definition_t& weapon = shared::get_weapon_definition(active_weapon->weapon_id);
  const shared::weapon_fire_t&       fire   = shared::fire_of(weapon, trigger);

  switch (fire.resolution)
  {
    // Nothing on this button, or the client's scope: no shot and no fire mark. A canopy is a
    // held LEVEL that canopy_system reads off the slot's last input, so the press is nothing too.
    case entities::Fire_Resolution::None:
    case entities::Fire_Resolution::Zoom:
    case entities::Fire_Resolution::Canopy:
      return;

    case entities::Fire_Resolution::Hitscan:
    {
      if (!try_begin_shot(context, client_slot, *player, *active_weapon, weapon, fire_time))
        return;

      // The world test is the one question every flying thing asks, at radius zero: the
      // shooter's team view of the map AND the movers, so a raised platform stops a round
      // and a lift takes the impact. The world hit clips the range; nothing past it can be hit.
      const float range = fire.hitscan.range;
      const std::optional<shared::projectile_hit_t> world_hit = shared::sweep_projectile(
          context.world.session.bvh, world, eye, eye + direction * range, 0.f);
      const float world_distance = world_hit ? world_hit->t * range : range;

      if (context.posed_players.built_for_tick != context.tick_number)
        fatal_error("hit volumes were posed for tick {} but this is tick {}; "
                    "pose_all_targets must run before the input loop",
                    context.posed_players.built_for_tick, context.tick_number);

      // A frozen player is a mover the sweep stops at AND a body the hitboxes describe. Their
      // box clips a shot at anyone behind them; their own hitboxes are read past it, so a
      // statue still takes a headshot.
      const bool surface_is_a_body =
          world_hit && fire.contact.targets == shared::contact_targets_t::Bodies &&
          context.world.session.entity_system.get<entities::Player_Entity>(world_hit->entity_uid) !=
              nullptr;
      const float body_range = surface_is_a_body ? range : world_distance;

      remnant_targets_t remnants;
      Span<const shared::hitscan_target_t> targets =
          hitscan_targets_of(context, fire.contact, *player, remnants);
      auto verdict     = shared::bracket_verdict_t{};
      bool used_rewind = false;

      // --- Lag compensation ---
      // rewind the targets to the position that the shooter saw when they fired.
      // The present-tick set is the fallback.
      // spectator, a client's first shots before it holds two snapshots, a
      // refused bracket, or an endpoint that has aged out of the ring all
      // land there.
      if (context.cvars->sv_lag_compensation &&
          fire.contact.targets == shared::contact_targets_t::Bodies)
      {

        verdict = get_interpolation_bracket_for_input(context, client_slot, input);

        const bool bracket_is_usable =
            verdict.status == shared::bracket_status_t::Ok ||
            verdict.status == shared::bracket_status_t::Clamped;

        if (bracket_is_usable &&
            shared::try_pose_players_across_bracket(
                context.replication.snapshot_history, shared::player_rig(),
                aim_settings_from(*context.cvars), verdict.bracket, context.rewind_scratch))
        {
          // The rewound set is PLAYERS ONLY, so the static targets are appended
          // rather than lost. A rewind exists because a target moved between
          // the tick the shooter saw and the tick we are on; a damageable did
          // not move, so its present-tick pose is not an approximation of what
          // the shooter saw, it IS what the shooter saw.
          //
          // The spans these carry point into context.posed_players.volumes,
          // which pose_all_targets sized in step 2 of the tick and nothing
          // touches again until the next one -- so copying the target values is safe
          // for exactly as long as this shot needs them.
          append_static_targets(context.posed_players, context.rewind_scratch);

          targets     = Span<const shared::hitscan_target_t>{context.rewind_scratch.targets};
          used_rewind = true;
        }
      }

      const shared::hitscan_result_t hit = shared::resolve_hitscan(
          eye, direction, body_range, targets, player->entity_id);

      if (context.cvars->sv_shot_debug)
        send_shot_debug(context, client_slot, input, shared::subtick_slot_of(fire_time), eye,
                        direction, verdict,
                        used_rewind,
                        used_rewind ? context.rewind_scratch : context.posed_players, targets,
                        hit, player->entity_id);

      // A body inside the clipped range, or the frozen player whose own box clipped it.
      const bool hit_a_body =
          hit.hit_uid != shared::null_entity_uid &&
          (hit.distance <= world_distance || (world_hit && hit.hit_uid == world_hit->entity_uid));

      // What arrived, and where. Tested here, acted on in update_contacts; the arm there
      // asks what the target is. A shot into the void arrives nowhere and pushes nothing.
      pending_contact_t contact{.shooter_uid = player->entity_id,
                                .weapon      = active_weapon->weapon_id,
                                .trigger     = trigger,
                                .damage_type = active_weapon->damage_type};
      if (hit_a_body)
      {
        contact.target_uid = hit.hit_uid;
        contact.point      = hit.impact_point;
        contact.normal     = hit.impact_normal;
        contact.region     = hit.region;
      }
      else if (world_hit)
      {
        contact.target_uid = world_hit->entity_uid;
        contact.point      = world_hit->position;
        contact.normal     = world_hit->normal;
      }
      else
        break;

      context.outgoing.pending_contacts.push_back(contact);
      break;
    }
    case entities::Fire_Resolution::Projectile:
    {
      log_terminal("trying to fire a projectile.");

      if (!try_begin_shot(context, client_slot, *player, *active_weapon, weapon, fire_time))
        return;

      spawn_projectile(context, player->entity_id, weapon, eye, direction, trigger);
      break;
    }
    case entities::Fire_Resolution::Place:
    {
      if (!try_begin_shot(context, client_slot, *player, *active_weapon, weapon, fire_time))
        return;

      const shared::entity_uid_t placed_uid =
          spawn_placed_entity(context, weapon, player->position, yaw, trigger);
      if (context.world.session.entity_system.get<entities::Remnant_Entity>(placed_uid) != nullptr)
        claim_remnant(context, placed_uid, player->entity_id);
      break;
    }
    case entities::Fire_Resolution::Self_Impulse:
    {
      if (!shared::try_apply_self_impulse(shared::movement_settings_from(*context.cvars), weapon,
                                          trigger, direction, player->movement,
                                          player->velocity))
        return;
      break;
    }
  }

  // set some fields in player to indicate that we fired a shot.
  mark_shot_fired(context, *player, active_weapon->weapon_id);
}

} // namespace server
