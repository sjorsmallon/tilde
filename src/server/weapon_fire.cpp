#include "weapon_fire.hpp"

#include "../shared/collision_detection.hpp"
#include "../shared/hitscan.hpp"
#include "../shared/lag_compensation.hpp"
#include "../shared/log.hpp"
#include "../shared/network/snapshot_history.hpp"
#include "../shared/player_animator.hpp"
#include "../shared/player_constants.hpp"
#include "../shared/player_rig.hpp"
#include "../shared/remnant.hpp"
#include "damage.hpp"
#include "send_protobuf_message.hpp"
#include "server_api.hpp"
#include "server_messages.hpp"
#include "systems/inventory_system.hpp"

#include <algorithm>
#include <format>
#include <limits>

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

void resolve_player_shot(
  server_context_t &context, int32_t client_slot,
  const game::C2S_ClientInput &input,
  Span<const uint8_t> disabled_geometry,
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

      float range = fire.hitscan.range;
      auto world_hit = ray_hit_result_t{};
      
      const bool shot_collided_with_static_geometry =
          bvh_intersect_ray(context.world.session.bvh, eye, direction, world_hit,
                            disabled_geometry) &&
          world_hit.hit;

      // clip the max range, since players outside of this range can't possibly be hit.
      if (shot_collided_with_static_geometry)
        range = std::min(range, world_hit.t);

      if (context.posed_players.built_for_tick != context.tick_number)
        fatal_error("hit volumes were posed for tick {} but this is tick {}; "
                    "pose_all_targets must run before the input loop",
                    context.posed_players.built_for_tick, context.tick_number);

      // --- Lag compensation ---
      // rewind the targets to the position that the shooter saw when they fired.
      // The present-tick set is the fallback.
      // spectator, a client's first shots before it holds two snapshots, a
      // refused bracket, or an endpoint that has aged out of the ring all
      // land there.
      Span<const shared::hitscan_target_t> targets{context.posed_players.targets};
      auto verdict = shared::bracket_verdict_t{};
      bool used_rewind = false;

      // A teleport shot is aimed at the shooter's OWN remnants and at nothing
      // else: a remnant is a marker, not a body, so it never soaks a bullet and
      // nobody else's can be aimed at. It does not move, so there is nothing to
      // rewind; the volume is the one the hitbox overlay draws.
      std::vector<assets::posed_hitbox_t>   remnant_volumes;
      std::vector<shared::hitscan_target_t> remnant_targets;
      const bool aims_at_remnants = fire.hitscan.hit_effect == shared::hit_effect_t::Teleport;
      if (aims_at_remnants)
      {
        std::vector<shared::entity_uid_t> remnant_uids;
        for (const entities::Remnant_Entity& remnant :
             context.world.session.entity_system.entities_of<entities::Remnant_Entity>())
        {
          if (remnant.owner_uid != player->entity_id)
            continue;
          remnant_uids.push_back(remnant.entity_id);
          remnant_volumes.push_back(shared::remnant_hit_volume(remnant));
        }
        remnant_targets.reserve(remnant_uids.size());
        for (size_t index = 0; index < remnant_uids.size(); ++index)
          remnant_targets.push_back(shared::make_hitscan_target(
              remnant_uids[index],
              Span<const assets::posed_hitbox_t>{remnant_volumes.data() + index, 1}));
        targets = Span<const shared::hitscan_target_t>{remnant_targets};
      }

      if (context.cvars->sv_lag_compensation && !aims_at_remnants)
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
          eye, direction, range, targets, player->entity_id);


      if (context.cvars->sv_shot_debug)
        send_shot_debug(context, client_slot, input, shared::subtick_slot_of(fire_time), eye,
                        direction, verdict,
                        used_rewind,
                        used_rewind ? context.rewind_scratch : context.posed_players, targets,
                        hit, player->entity_id);

      if (hit.hit_uid != shared::null_entity_uid)
      {
        switch (fire.hitscan.hit_effect)
        {
          case shared::hit_effect_t::Damage:
          {
            // debug
            {
              broadcast_server_text_message(
                context, std::format("Player {} hit player {} in the {}",
                client_slot, hit.hit_uid,
                to_string(hit.region)));

            }
            const bool was_headshot = (hit.region == shared::hit_region_t::Head);

            auto damage_info = damage_info_t{};
            damage_info.victim_uid = hit.hit_uid;
            damage_info.attacker_uid = player->entity_id;
            damage_info.inflictor_uid = player->entity_id;
            damage_info.weapon_id = static_cast<uint16_t>(active_weapon->weapon_id);

            int32_t damage_amount = fire.hitscan.damage * (was_headshot ? fire.hitscan.headshot_multiplier : 1.f);
            damage_info.amount = damage_amount;
            damage_info.source_position = eye;
            damage_info.was_headshot = was_headshot;
            damage_info.type = active_weapon->damage_type;

            // we store all damage events and then before resolving, we can check majority contribution (if two people shoot at the same time, or whatever.)
            context.outgoing.pending_hits.push_back(
                {damage_info, hit.impact_point, hit.impact_normal, hit.region});
            break;
          }
          case shared::hit_effect_t::Swap:
          {
            if (context.world.session.entity_system.get<entities::Player_Entity>(hit.hit_uid) != nullptr)
              context.outgoing.pending_swaps.push_back({player->entity_id, hit.hit_uid});
            break;
          }
          case shared::hit_effect_t::Magnet:
          {
            context.outgoing.pending_magnets.push_back(
                {player->entity_id, hit.hit_uid, fire.hitscan.magnet_speed});
            break;
          }
          case shared::hit_effect_t::Tether:
          {
            context.outgoing.pending_tethers.push_back(
                {player->entity_id, hit.hit_uid, fire.hitscan.tether_speed,
                 fire.hitscan.tether_seconds});
            break;
          }
          case shared::hit_effect_t::Teleport:
          {
            context.outgoing.pending_teleports.push_back({player->entity_id, hit.hit_uid});
            break;
          }
          case shared::hit_effect_t::Stasis:
          case shared::hit_effect_t::Statue:
          {
            const entities::Movement_Override kind =
                fire.hitscan.hit_effect == shared::hit_effect_t::Stasis
                    ? entities::Movement_Override::Stasis
                    : entities::Movement_Override::Statue;
            context.outgoing.pending_freezes.push_back(
                {player->entity_id, hit.hit_uid, kind, fire.hitscan.freeze_seconds});
            break;
          }
        }
      }
      else if (shot_collided_with_static_geometry && world_hit.t <= fire.hitscan.range)
      {
        auto shot_impact_fx = shared::Shot_Impact{};
        shot_impact_fx.origin = eye + direction * world_hit.t;
        shot_impact_fx.normal = world_hit.normal;
        shot_impact_fx.weapon = static_cast<uint16_t>(active_weapon->weapon_id);
        shot_impact_fx.trigger = static_cast<uint8_t>(trigger);
        shared::fire_shot_impact(context.outgoing.effects, shot_impact_fx);
      }
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

      spawn_placed_entity(context, player->entity_id, weapon, player->position, yaw, trigger);
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
