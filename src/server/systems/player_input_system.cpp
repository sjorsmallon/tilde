#include "entities/generated/entities/player_entity_generated.hpp"
#include "entities/generated/entities/weapon_entity_generated.hpp"
#include "systems/player_input_system.hpp"

#include "../shared/animation/player_animator.hpp"
#include "../shared/cvars/generated/cvars_generated.hpp"
#include "../shared/lighting/lens.hpp"
#include "../shared/camera_swap.hpp"
#include "../shared/merge.hpp"
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
#include <optional>

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

  const uint32_t warning_interval = std::max(1u, context.cvars->sv_tickrate);
  if (context.tick_number - player.last_empty_fire_warning_tick >= warning_interval)
  {
    player.last_empty_fire_warning_tick = context.tick_number;
    log_warning("slot {}: trigger pulled on an empty {} — no shot resolved", client_slot,
                weapon_name);
  }
}

struct accepted_client_input_t
{
  entities::Player_Entity* player;
  shared::Tick_Input  tick_input;
};

// Everything that drops or acks an input before it moves anyone. The high-water mark means "I have
// processed through N", not "I moved you": a spectator's input is acked and spends no credit, since
// its held_snapshot_tick was the only part of it that applied, and leaving it unacked had the client
// resend it forever. An input that breaks the sub-tick grammar is acked and dropped.
[[nodiscard]] static std::optional<accepted_client_input_t> try_accept_client_input(
    server_context_t& context, int32_t client_slot, const shared::Client_Input& input)
{
  if (!is_valid_client_slot(client_slot))
  {
    log_error("Tick: a move arrived tagged with slot {}, which is out of range — dropped",
              client_slot);
    return std::nullopt;
  }

  client_slot_t& client = context.clients[client_slot];
  if (input.input_number <= client.latest_processed_input_number)
    return std::nullopt;

  entities::Player_Entity* player =
      context.world.session.entity_system.get<entities::Player_Entity>(client.player_uid);
  if (player == nullptr)
  {
    client.latest_processed_input_number = input.input_number;
    return std::nullopt;
  }

  if (!try_spend_move_credit(client.move_credits))
  {
    const uint32_t warning_interval = std::max(1u, context.cvars->sv_tickrate);
    if (context.tick_number - client.last_move_throttle_warning_tick >= warning_interval)
    {
      log_warning("slot {} is over its move budget ({} banked max) — dropping "
                  "moves. A stall this long costs the client the input it "
                  "cannot catch up on; a client that never stops being over "
                  "budget is sending faster than the server ticks",
                  client_slot, context.cvars->sv_max_move_backlog);
      client.last_move_throttle_warning_tick = context.tick_number;
    }
    return std::nullopt;
  }

  client.latest_processed_input_number = input.input_number;

  if (!shared::button_changes_follow_grammar(input.input))
  {
    log_error("slot {}: input {} carries sub-tick button changes that break the grammar "
              "(slots must be 1..{}, strictly ascending) — command dropped",
              client_slot, input.input_number, shared::SUBTICK_SLOT_COUNT - 1);
    return std::nullopt;
  }

  return accepted_client_input_t{.player = player, .tick_input = input.input};
}

// The sounds of a tick's movement, fired once per input off the accumulated events. The launch pad
// and the bubble are predicted locally too, so the client suppresses its own copy of these.
static void fire_move_event_effects(server_context_t& context, const entities::Player_Entity& player,
                                    const shared::predicted_world_t& world,
                                    const Move_Events& move_events)
{
  if (move_events.jumped)
  {
    shared::Jump fx{};
    fx.site.origin          = player.position;
    fx.site.attached_entity = player.entity_id;
    shared::write_jump(context.outgoing.tick_broadcast, fx);
  }
  if (move_events.landed && move_events.land_impact_speed > context.cvars->pm_minimum_land_impact_speed)
  {
    shared::Land fx{};
    fx.site.origin           = player.position;
    fx.site.scale            = move_events.land_impact_speed;
    fx.site.attached_entity  = player.entity_id;
    fx.site.surface_material = shared::material_index_under(context.world.session, move_events.ground_contact);
    shared::write_land(context.outgoing.tick_broadcast, fx);
  }
  if (move_events.stepped)
  {
    shared::Footstep fx{};
    fx.site.origin           = player.position;
    fx.site.normal           = move_events.ground_contact.plane.normal;
    fx.site.scale            = move_events.step_speed;
    fx.site.attached_entity  = player.entity_id;
    fx.site.surface_material = shared::material_index_under(context.world.session, move_events.ground_contact);
    shared::write_footstep(context.outgoing.tick_broadcast, fx);
  }

  if (move_events.launched_by_pad)
  {
    switch (move_events.pad_kind)
    {
    case shared::movement_volume_kind_t::Jump_Pad:
    {
      shared::Jump_Pad_Launch fx{};
      fx.site.origin = shared::movement_volume_origin(world.movement_volumes, move_events.pad_uid,
                                                 player.position);
      fx.site.normal          = linalg::normalize(player.velocity);
      fx.site.attached_entity = player.entity_id;
      shared::write_jump_pad_launch(context.outgoing.tick_broadcast, fx);
      break;
    }
    case shared::movement_volume_kind_t::Bounce:
      pop_bubble(context, move_events.pad_uid, player.entity_id);
      break;
    }
  }

  if (move_events.override_ended.kind == entities::Movement_Override::Pilot &&
      linalg::length_squared(move_events.override_ended.surface_normal) > 0.f)
  {
    shared::Pilot_Impact fx{};
    fx.site.origin          = move_events.override_ended.surface_point;
    fx.site.normal          = move_events.override_ended.surface_normal;
    fx.site.attached_entity = player.entity_id;
    shared::write_pilot_impact(context.outgoing.tick_broadcast, fx);
  }
}

// The view a steered body's steps run under: its hand's mouse, as that client last sent it. A hand without
// a slot steers nothing and the body follows its own input.
static std::optional<shared::View> try_find_steering_view_for_player(server_context_t& context,
                                                                     const entities::Player_Entity& player)
{
  if (!shared::player_view_is_steered_by_another(player))
    return std::nullopt;
  for (connected_client_t row : connected_clients(context))
    if (row.client.player_uid == player.view_steered_by_uid)
      return row.client.latest_view;
  return std::nullopt;
}

void update_player_inputs(server_context_t& context, const shared::predicted_world_storage_t& world_storage)
{
  const float tick_dt         = get_tick_interval_seconds();
  const bool  world_is_frozen = !is_movement_allowed(context);
  const bool  in_freeze       = get_match(context).phase == entities::Round_Phase::Freeze;

  const shared::movement_settings_t move_settings = shared::movement_settings_from_cvars(*context.cvars);

  for (const received_client_input_t& received : context.incoming.client_inputs)
  {
    const int32_t               client_slot = received.slot;
    const shared::Client_Input& input       = received.input;
    const std::optional<accepted_client_input_t> accepted = try_accept_client_input(context, client_slot, input);
    if (!accepted)
      continue;

    client_slot_t&                 client        = context.clients[client_slot];
    entities::Player_Entity&       player        = *accepted->player;
    const shared::Tick_Input& tick_input = accepted->tick_input;

    shared::queue_replay_player_view(
        context.world.replay_recorder,
        shared::replay_player_view_from_input(static_cast<uint8_t>(client_slot),
                                              {.from_tick    = input.interpolated_from_tick,
                                               .towards_tick = input.interpolated_towards_tick,
                                               .fraction     = input.interpolation_fraction},
                                              tick_input));

    const uint64_t buttons_before_tick = client.latest_buttons_bitmap;
    client.latest_buttons_bitmap       = shared::buttons_at_end_of_tick(tick_input);
    client.latest_view                 = tick_input.view_at_end;
    const std::optional<shared::View> steering_view = try_find_steering_view_for_player(context, player);

    const shared::lens_watch_screen_t screen = shared::clamp_lens_watch_screen(
        {.fov_degrees = input.view_fov_degrees, .aspect = input.view_aspect});
    player.view_fov_degrees = screen.fov_degrees;
    player.view_aspect      = screen.aspect;

    const bool is_dead = player.health.current_health <= 0;

    // This player's view of the frozen world: its team's walls are not there, nor its own statue or shadow.
    const shared::predicted_world_t world = shared::get_predicted_world_for_player(world_storage, player);

    const shared::input_restrictions_t restrictions{
        .is_dead            = is_dead,
        .world_is_frozen    = world_is_frozen,
        // A driver walks the merged body and the passenger fires from it.
        .drives_merged_body = shared::try_find_passenger_by_driver_uid(
                                  context.world.session.entity_system, player.entity_id) != nullptr};

    const shared::subtick_steps_t steps =
        shared::split_tick_input_into_subtick_steps(tick_input, tick_dt);

    Move_Events move_events{};
    uint64_t    buttons_entering_step = buttons_before_tick;

    for (const shared::subtick_step_t& input_step : steps)
    {
      shared::subtick_step_t step = input_step;
      if (steering_view)
        step.view = step.view_at_end = *steering_view;
      const uint64_t               pressed_in_this_step = step.buttons & ~buttons_entering_step;
      const shared::subtick_time_t step_time = shared::subtick_time(context.tick_number, step.start_slot);
      const vec3f                  aim       = linalg::direction_from_angles(step.view.yaw, step.view.pitch);

      // The step itself is the shared function: switch, reload, move, triggers, toggles. What
      // follows is the server's half of what it reported.
      shared::player_input_state_t state =
          shared::read_input_state_from_player(context.world.session.entity_system, player);
      const shared::input_step_outcome_t outcome = shared::apply_input_step_to_player(
          move_settings, context.world.session.bvh, world, restrictions, step, buttons_entering_step,
          step_time, tick_dt, state);
      write_input_state_to_player(context.world.session, player, state);
      buttons_entering_step = step.buttons;
      shared::accumulate_move_events(move_events, outcome.move_events);

      if (outcome.switched_to)
      {
        const entities::Weapon_Entity* raised = try_find_active_weapon(context.world.session, player);
        broadcast_server_text_message(
            context, std::format("Slot {} equipped {} ({})", client_slot,
                                 raised != nullptr
                                     ? shared::get_weapon_definition(raised->weapon_id).display_name
                                     : "an empty hand",
                                 to_string(player.inventory.active_slot)));
      }

      if ((pressed_in_this_step & Button::Jump) && in_freeze)
        player.wants_to_skip_freeze = true;

      if (!world_is_frozen && !is_dead)
        refill_magazines_on_ground(context.world.session, player);

      if ((pressed_in_this_step & Button::Throw) && !is_dead &&
          try_throw_active_weapon(context, player, aim, tick_dt))
        cancel_reload(player);

      if ((pressed_in_this_step & Button::Ping) && !is_dead && !world_is_frozen)
        (void)try_place_ping(context, player, shared::player_eye_position(player.position), aim,
                             world.disabled_geometry);

      for (const entities::Fire_Trigger trigger :
           {entities::Fire_Trigger::Primary, entities::Fire_Trigger::Secondary})
      {
        if (outcome.refused[trigger] != shared::shot_refusal_t::None)
          log_refused_shot(context, client_slot, player, outcome.refused[trigger]);

        if (outcome.fire_due[trigger])
          resolve_player_shot(context, client_slot, input, world, player, step.view.yaw,
                              step.view.pitch, *outcome.fire_due[trigger], trigger);
      }
    }

    if (!is_dead && !world_is_frozen)
    {
      const shared::View head = steering_view.value_or(tick_input.view_at_end);
      player.view_angle_yaw   = head.yaw;
      player.view_angle_pitch = head.pitch;
    }

    fire_move_event_effects(context, player, world, move_events);
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
