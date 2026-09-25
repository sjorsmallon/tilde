#include "systems/player_input_system.hpp"

#include "../shared/cvars/generated/cvars_generated.hpp"
#include "../shared/effects/generated/effects_generated.hpp"
#include "../shared/network/subtick_codec.hpp"
#include "../shared/player_constants.hpp"
#include "../shared/player_move.hpp"
#include "../shared/replay_recorder.hpp"
#include "../shared/round_phase_rules.hpp"
#include "systems/game_rules_system.hpp"
#include "../shared/subtick.hpp"
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
        shared::predicted_world_of(world_storage, player->team_allegiance);

    if (!try_spend_move_credit(client.move_credits))
    {
      const uint32_t warning_interval =
          static_cast<uint32_t>(std::max(1.f, context.cvars->sv_tickrate));
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

    // allowed to move is the moire logical one because we can pile more conditions on here.
    bool allowed_to_move = !is_dead;

    // where the aim ended up at the end so interpolation is proper.
    const float yaw = subtick_input.view_at_end.yaw;
    const float pitch = subtick_input.view_at_end.pitch;

    float tick_dt = static_cast<float>(get_tick_interval());

    const bool world_is_frozen = !is_movement_allowed(context);
    const bool in_freeze = match_of(context).phase == entities::Round_Phase::Freeze;
    if (world_is_frozen)
    {
      // zero out velocity so nothing builds up.
      player->velocity = {0.f, 0.f, 0.f};
    }

    const shared::subtick_steps_t steps =
        shared::split_input_per_tick_into_subtick_steps(subtick_input, tick_dt);

    const shared::movement_settings_t move_settings =
        shared::movement_settings_from(*context.cvars);

    auto move_events = Move_Events{};
    uint64_t buttons_entering_step = buttons_before_tick;

    for (const shared::subtick_step_t& step : steps)
    {
      const uint64_t pressed_in_this_step = step.buttons & ~buttons_entering_step;
      buttons_entering_step = step.buttons;

      const shared::subtick_time_t step_time =
          shared::subtick_time(context.tick_number, step.start_slot);

      const entities::Inventory_Slot slot_before_switch = player->inventory.active_slot;
      if (const std::optional<entities::Inventory_Slot> selected =
              shared::try_slot_selected_by(pressed_in_this_step))
        player->inventory.active_slot = *selected;

      if (player->inventory.active_slot != slot_before_switch)
      {
        cancel_reload(*player);

        const entities::Weapon_Entity* raised =
            try_find_active_weapon(context.world.session, *player);
        const float deploy_seconds =
            raised != nullptr
                ? shared::get_weapon_definition(raised->weapon_id).deploy_duration_seconds
                : 0.f;

        player->inventory.deploy_complete_time =
            shared::subtick_time_after(step_time, deploy_seconds, tick_dt);

        // debug

        {
          broadcast_server_text_message(
            context,
            std::format("Slot {} equipped {} ({})", client_slot,
                          raised != nullptr
                              ? shared::get_weapon_definition(raised->weapon_id).display_name
                              : "an empty hand",
                          to_string(player->inventory.active_slot)));
        }

      }

      if ((pressed_in_this_step & Button::Jump) && in_freeze)
        player->wants_to_skip_freeze = true;

      // did we press reload?
      if (pressed_in_this_step & Button::Reload)
      {
        const entities::Weapon_Entity *held_entity =
            try_find_active_weapon(context.world.session, *player);
        if (held_entity != nullptr)
        {
          const shared::weapon_definition_t &held =
              shared::get_weapon_definition(held_entity->weapon_id);
          if (!is_reloading(*player) &&
              shared::reload_may_start(held, held_entity->ammo, held_entity->reserve_ammo))
          {
            player->reload_complete_time = shared::subtick_time_after(
                step_time, held.reload_duration_seconds, tick_dt);
          }
        }
      }

      const bool fire_pressed_in_this_step = (pressed_in_this_step & Button::Fire) != 0;
      const bool secondary_fire_pressed_in_this_step =
          (pressed_in_this_step & Button::Secondary_Fire) != 0;

      // process movement and fire stuff only if the world is not frozen.
      if (!world_is_frozen)
      {
        auto step_events = Move_Events{};

        shared::move_input_t move_input = shared::move_input_of(step);
        if (!allowed_to_move)
          move_input.buttons = Move_Input{};

        // canonical move.
        const shared::move_state_t moved = player_move(
            move_settings, context.world.session.bvh, world,
            {.feet = player->position, .velocity = player->velocity, .movement = player->movement},
            move_input, &step_events);

        player->position = moved.feet;
        player->velocity = moved.velocity;
        player->movement = moved.movement;

        // check what movement events happened so we can fire events and track some state.
        move_events.jumped |= step_events.jumped;
        if (step_events.landed &&
            step_events.land_impact_speed > move_events.land_impact_speed)
        {
          move_events.landed            = true;
          move_events.land_impact_speed = step_events.land_impact_speed;
        }
        if (step_events.launched_by_pad)
        {
          move_events.launched_by_pad = true;
          move_events.pad_uid         = step_events.pad_uid;
          move_events.pad_kind        = step_events.pad_kind;
        }
      }

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
                             player->position + vec3f{0.f, shared::player_eye_height, 0.f},
                             linalg::direction_from_angles(step.view.yaw, step.view.pitch),
                             world.disabled_geometry);
      // if we tried to fire.
      // Both buttons resolve the same way, each through its own half of the row.
      // Zoom is the client's (it arrives as Button::Zoom state) and resolves to
      // nothing here.
      // A press fires at the step it opened; a button already down fires when its row says a held one is due.
      const shared::subtick_time_t step_end_time =
          shared::subtick_time(context.tick_number, step.start_slot + step.slot_count);

      struct trigger_button_t
      {
        entities::Fire_Trigger trigger;
        uint64_t               button;
        bool                   pressed_in_this_step;
      };
      const Array<trigger_button_t, 2> trigger_buttons = {{
          {entities::Fire_Trigger::Primary, Button::Fire, fire_pressed_in_this_step},
          {entities::Fire_Trigger::Secondary, Button::Secondary_Fire,
           secondary_fire_pressed_in_this_step},
      }};

      for (const trigger_button_t& trigger_button : trigger_buttons)
      {
        if (!allowed_to_move || world_is_frozen)
          continue;

        std::optional<shared::subtick_time_t> fire_time;
        if (trigger_button.pressed_in_this_step)
          fire_time = step_time;
        else if ((step.buttons & trigger_button.button) != 0)
          fire_time = try_find_held_fire_time(context.world.session, *player,
                                              trigger_button.trigger, step_time, step_end_time);

        if (fire_time)
          resolve_player_shot(context, client_slot, input, world, player, step.view.yaw,
                              step.view.pitch, *fire_time, trigger_button.trigger);
      }
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
      shared::fire_land(context.outgoing.effects, fx);
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
  }
}

} // namespace server
