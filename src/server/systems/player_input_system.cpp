#include "entities/generated/entities/player_entity_generated.hpp"
#include "entities/generated/entities/weapon_entity_generated.hpp"
#include "systems/player_input_system.hpp"

#include "../shared/animation/player_animator.hpp"
#include "../shared/cvars/generated/cvars_generated.hpp"
#include "../shared/effects/generated/effects_generated.hpp"
#include "../shared/lighting/lens.hpp"
#include "../shared/merge.hpp"
#include "../shared/network/subtick_codec.hpp"
#include "movement/player_constants.hpp"
#include "movement/player_move.hpp"
#include "replay/replay_recorder.hpp"
#include "../shared/round_phase_rules.hpp"
#include "systems/game_rules_system.hpp"
#include "../shared/subtick.hpp"
#include "../shared/input_step.hpp"
#include "map/surface_lookup.hpp"
#include "../shared/weapon_instance.hpp"
#include "../shared/weapons.hpp"
#include "log.hpp"
#include "move_budget.hpp"
#include "server_api.hpp"
#include "server_context.hpp"
#include "server_messages.hpp"
#include "systems/bubble_system.hpp"
#include "systems/inventory_system.hpp"
#include "systems/ping_system.hpp"
#include "weapon_fire.hpp"

#include <cmath>
#include <format>

namespace server
{

// The empty-magazine warning is rate limited per player; the clock refusals are terminal noise.
static void log_refused_shot(server_context_t& context, int32_t client_slot,
                             entities::Player_Entity& player, shared::shot_refusal_t refusal)
{
  const shared::weapon_definition_t* held =
      shared::try_find_held_weapon_definition(context.world.session.entity_system, player);
  const char* weapon_name = held != nullptr ? held->display_name : "an empty hand";

  if (refusal != shared::shot_refusal_t::Empty)
  {
    log_terminal("slot {}: {} trigger refused, {}", client_slot, weapon_name,
                 shared::to_string(refusal));
    return;
  }

  const uint32_t warning_interval =
      std::max(1u, context.cvars->sv_tickrate);
  if (context.tick_number - player.last_empty_fire_warning_tick >= warning_interval)
  {
    player.last_empty_fire_warning_tick = context.tick_number;
    log_warning("slot {}: trigger pulled on an empty {} — no shot resolved", client_slot,
                weapon_name);
  }
}

void update_player_inputs(server_context_t& context, const shared::predicted_world_storage_t& world_storage)
{
  for (const auto &[client_slot, input] : context.incoming.client_inputs)
  {
    if (!is_valid_client_slot(client_slot))
    {
      log_error("Tick: a move arrived tagged with slot {}, which is out of range "
                "— dropped",
                client_slot);
      continue;
    }

    client_slot_t& client = context.clients[client_slot];

    // valid during this tick.
    entities::Player_Entity* player =
        context.world.session.entity_system.get<entities::Player_Entity>(
            client.player_uid);

    // latest processed is the high watermark so anything earlier can be discarded.
    if (input.input_number() <= client.latest_processed_input_number)
      continue;

    // A spectator has no body to move, but the command was still received and
    // consumed: the pass above drained its held_snapshot_tick, which is the only
    // part of a move that applies to a client with no body. So ACK it. The mark
    // means "I have processed through N", not "I moved you", and leaving it
    // parked was invisible until the client started resending unacked inputs --
    // at which point a spectator resent the same ones forever.
    //
    // No credit is spent. Nothing moved, so there is no rate to bound.
    const bool spectating = !player;
    if (spectating)
    {
      client.latest_processed_input_number = input.input_number();
      continue;
    }

    // filtering so we don't process input people that could probably not move.
    const bool is_dead = player->health.current_health <= 0;

    // This player's view of the frozen world: its team's walls are not there.
    const shared::predicted_world_t world =
        shared::get_predicted_world_for_team(world_storage, player->team_allegiance);

    if (!try_spend_move_credit(client.move_credits))
    {
      const uint32_t warning_interval =
          std::max(1u, context.cvars->sv_tickrate);
      if (context.tick_number - client.last_move_throttle_warning_tick >=
          warning_interval)
      {
        log_warning("slot {} is over its move budget ({} banked max) — dropping "
                    "moves. A stall this long costs the client the input it "
                    "cannot catch up on; a client that never stops being over "
                    "budget is sending faster than the server ticks",
                    client_slot, context.cvars->sv_max_move_backlog);
        client.last_move_throttle_warning_tick = context.tick_number;
      }
      continue;
    }

    client.latest_processed_input_number = input.input_number();

    const std::optional<shared::subtick_input_t> decoded_input =
        network::try_read_subtick_input(input);
    if (!decoded_input)
    {
      log_error("slot {}: input {} carries sub-tick edges that break the grammar "
                "(slots must be 1..{}, strictly ascending, at most {} of them) — "
                "command dropped",
                client_slot, input.input_number(), shared::SUBTICK_SLOT_COUNT - 1,
                shared::MAX_SUBTICK_EDGES);
      continue;
    }
    const shared::subtick_input_t& subtick_input = *decoded_input;

    shared::queue_replay_player_view(
        context.world.replay_recorder,
        shared::replay_player_view_from_input(static_cast<uint8_t>(client_slot),
                                              {.from_tick    = input.interpolated_from_tick(),
                                               .towards_tick = input.interpolated_towards_tick(),
                                               .fraction     = input.interpolation_fraction()},
                                              subtick_input));

    const uint64_t buttons_before_tick = client.latest_buttons_bitmap;
    client.latest_buttons_bitmap = subtick_input.buttons_at_end();

    if (input.has_view_fov_degrees() && input.has_view_aspect())
    {
      const shared::lens_watch_screen_t view =
          shared::clamp_lens_watch_screen({.fov_degrees = input.view_fov_degrees(), .aspect = input.view_aspect()});
      player->view_fov_degrees = view.fov_degrees;
      player->view_aspect      = view.aspect;
    }

    // allowed to move is the moire logical one because we can pile more conditions on here.
    bool allowed_to_move = !is_dead;

    // where the aim ended up at the end so interpolation is proper.
    const float yaw = subtick_input.view_at_end.yaw;
    const float pitch = subtick_input.view_at_end.pitch;

    const float tick_dt = get_tick_interval_seconds();

    const bool world_is_frozen = !is_movement_allowed(context);
    const bool in_freeze = get_match(context).phase == entities::Round_Phase::Freeze;

    const shared::subtick_steps_t steps =
        shared::split_input_per_tick_into_subtick_steps(subtick_input, tick_dt);

    const shared::movement_settings_t move_settings =
        shared::movement_settings_from_cvars(*context.cvars);

    const shared::input_restrictions_t restrictions{
        .is_dead            = is_dead,
        .world_is_frozen    = world_is_frozen,
        // A driver walks the merged body and the passenger fires from it.
        .drives_merged_body = shared::try_find_passenger_by_driver_uid(
                                  context.world.session.entity_system, player->entity_id) != nullptr};

    auto move_events = Move_Events{};
    uint64_t buttons_entering_step = buttons_before_tick;

    for (const shared::subtick_step_t& step : steps)
    {
      const uint64_t pressed_in_this_step = step.buttons & ~buttons_entering_step;

      const shared::subtick_time_t step_time =
          shared::subtick_time(context.tick_number, step.start_slot);

      // The step itself is the shared function: switch, reload, move, triggers, toggles. What
      // follows is the server's half of what it reported.
      shared::player_input_state_t state =
          shared::read_input_state_from_player(context.world.session.entity_system, *player);
      const shared::input_step_outcome_t outcome = shared::apply_input_step_to_player(
          move_settings, context.world.session.bvh, world, restrictions, step, buttons_entering_step,
          step_time, tick_dt, state);
      write_input_state_to_player(context.world.session, *player, state);
      buttons_entering_step = step.buttons;

      if (outcome.switched_to)
      {
        const entities::Weapon_Entity* raised =
            try_find_active_weapon(context.world.session, *player);
        broadcast_server_text_message(
            context,
            std::format("Slot {} equipped {} ({})", client_slot,
                        raised != nullptr
                            ? shared::get_weapon_definition(raised->weapon_id).display_name
                            : "an empty hand",
                        to_string(player->inventory.active_slot)));
      }

      if ((pressed_in_this_step & Button::Jump) && in_freeze)
        player->wants_to_skip_freeze = true;

      if (!world_is_frozen && !is_dead)
        refill_magazines_on_ground(context.world.session, *player);

      // if we tried to throw the weapon: repair the player weapon state.
      if ((pressed_in_this_step & Button::Throw) && !is_dead &&
          try_throw_active_weapon(context, *player,
                                  linalg::direction_from_angles(step.view.yaw, step.view.pitch),
                                  tick_dt))
        cancel_reload(*player);

      // if we tried to ping and we're not frozen.
      if ((pressed_in_this_step & Button::Ping) && !is_dead && allowed_to_move &&
          !world_is_frozen)
        (void)try_place_ping(context, *player,
                             shared::player_eye_position(player->position),
                             linalg::direction_from_angles(step.view.yaw, step.view.pitch),
                             world.disabled_geometry);

      for (const entities::Fire_Trigger trigger :
           {entities::Fire_Trigger::Primary, entities::Fire_Trigger::Secondary})
      {
        if (outcome.refused[trigger] != shared::shot_refusal_t::None)
          log_refused_shot(context, client_slot, *player, outcome.refused[trigger]);

        if (outcome.fire_due[trigger])
          resolve_player_shot(context, client_slot, input, world, player, step.view.yaw,
                              step.view.pitch, *outcome.fire_due[trigger], trigger);
      }

      shared::accumulate_move_events(move_events, outcome.move_events);
    }

    if (allowed_to_move && !world_is_frozen)
    {
      player->view_angle_yaw = yaw;
      player->view_angle_pitch = pitch;
    }

    // play some sounds.
    if (move_events.jumped)
    {
      shared::Jump fx{};
      fx.origin          = player->position;
      fx.attached_entity = player->entity_id;
      shared::fire_jump(context.outgoing.effects, fx);
    }
    if (move_events.landed && move_events.land_impact_speed >
                                  context.cvars->pm_minimum_land_impact_speed)
    {
      shared::Land fx{};
      fx.origin = player->position;
      fx.scale = move_events.land_impact_speed; // for volume scaling
      fx.attached_entity = player->entity_id;
      fx.surface_material =
          shared::material_index_under(context.world.session, move_events.ground_contact);
      shared::fire_land(context.outgoing.effects, fx);
    }
    if (move_events.stepped)
    {
      shared::Footstep fx{};
      fx.origin           = player->position;
      fx.normal           = move_events.ground_contact.plane.normal;
      fx.scale            = move_events.step_speed;
      fx.attached_entity  = player->entity_id;
      fx.surface_material =
          shared::material_index_under(context.world.session, move_events.ground_contact);
      shared::fire_footstep(context.outgoing.effects, fx);
    }

    // launch pad is predicted locally too so it feels good.
    if (move_events.launched_by_pad)
    {
      switch (move_events.pad_kind)
      {
        case shared::movement_volume_kind_t::Jump_Pad:
        {
          shared::Jump_Pad_Launch fx{};
          fx.origin = shared::movement_volume_origin(world.movement_volumes, move_events.pad_uid,
                                                     player->position);
          fx.normal          = linalg::normalize(player->velocity);
          fx.attached_entity = player->entity_id;
          shared::fire_jump_pad_launch(context.outgoing.effects, fx);
          break;
        }
        case shared::movement_volume_kind_t::Bounce:
          pop_bubble(context, move_events.pad_uid, player->entity_id);
          break;
      }
    }

    if (move_events.override_ended.kind == entities::Movement_Override::Pilot &&
        linalg::length_squared(move_events.override_ended.surface_normal) > 0.f)
    {
      shared::Pilot_Impact fx{};
      fx.origin          = move_events.override_ended.surface_point;
      fx.normal          = move_events.override_ended.surface_normal;
      fx.attached_entity = player->entity_id;
      shared::fire_pilot_impact(context.outgoing.effects, fx);
    }
  }
}

void update_body_yaws(server_context_t& context, float tick_dt)
{
  const aim_settings_t settings = aim_settings_from_cvars(*context.cvars);
  for (entities::Player_Entity& player :
       context.world.session.entity_system.entities_of_type<entities::Player_Entity>())
  {
    if (player.health.current_health <= 0)
      continue;
    advance_body_yaw(player.body_yaw, player.view_angle_yaw, tick_dt, settings);
  }
}

} // namespace server
