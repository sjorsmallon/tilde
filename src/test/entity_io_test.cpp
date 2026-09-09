// Entity I/O, the per-INSTANCE half: connections as map data, the load check
// that decides whether they can run, and the queue that runs them.
// entity_io_def.md ss6 and ss7; this is the pin for ss11 step 3.
//
// What it is here to catch, in the order the file walks it:
//
//   1. The file round trip. A connection survives save/load with its target
//      kind, its delay, its fire-once flag and its override intact -- and the
//      override goes through the same field_from_text an entity field does, so
//      a payload that came back wrong is a payload the editor will show wrong.
//   2. The load check, one case per way a row can be ill-typed. The activator
//      case is the one worth the most: it is checked against EVERY type the
//      signal's `by` list admits, so a connection that would work for a player
//      and not for a physics body is refused before either can walk into it.
//   3. ONE check, two policies. build_session drops what the check named and
//      indexes the rest, which is what keeps the drain's fatal_error on a null
//      dispatch cell unreachable.
//   4. The queue. Delay zero still queues (a connection never dispatches
//      inside the signal that caused it), emit order breaks a tie between two
//      records on one tick, fire_once spends the SESSION's copy and not the
//      map's, and a target destroyed during the delay is dropped rather than
//      fatal.

#include "server/damage.hpp"
#include "server/entity_io_queue.hpp"
#include "server/systems/trigger_system.hpp"
#include "server/server_api.hpp"
#include "server/server_context.hpp"

#include "shared/game_session.hpp"
#include "shared/lighting.hpp"
#include "shared/map.hpp"

#include <cstdio>
#include <string>

// The one symbol off the module's exported surface that the handler chain
// reaches: mortal.cpp routes Damage through inflict_damage, which stamps a
// death tick. server_impl.cpp defines it for the real server; standing in for
// it here is what keeps this test out of the module's whole link.
namespace server
{
uint32_t get_tick_number() { return 0; }
}

using namespace server;

namespace
{

int failure_count = 0;

void check(bool condition, const std::string& what)
{
  if (condition)
    return;
  std::printf("  FAILED: %s\n", what.c_str());
  ++failure_count;
}

constexpr uint32_t tickrate = 60;

// A trigger volume that emits Touched / Left, and a point light that accepts
// Enable / Disable / Toggle_Enabled / Set_Color. The smallest pair the first
// trait set can wire, and deliberately not a player: nothing here needs a body.
struct wired_map_t
{
  shared::map_t         map;
  shared::entity_uid_t  trigger = 0;
  shared::entity_uid_t  light   = 0;
};

wired_map_t make_wired_map()
{
  wired_map_t wired;

  auto trigger = std::make_shared<entities::Trigger_Volume_Entity>();
  trigger->name.set("front_plate");
  wired.trigger = wired.map.add_entity(trigger);

  auto light = std::make_shared<entities::Point_Light_Entity>();
  light->name.set("hall_lamp");
  light->switch_state.value = false;
  wired.light = wired.map.add_entity(light);

  return wired;
}

shared::connection_t touched_enables_the_light(const wired_map_t& wired)
{
  shared::connection_t connection;
  connection.sender      = wired.trigger;
  connection.signal      = entities::entity_signal::Touched;
  connection.target_kind = shared::connection_target_t::Uid;
  connection.target      = wired.light;
  connection.data.tag    = entities::entity_action::Enable;
  return connection;
}

// --- 1. the file round trip -------------------------------------------------

void test_a_connection_survives_the_file()
{
  std::printf("connections: the file round trip\n");

  wired_map_t wired = make_wired_map();

  shared::connection_t plain = touched_enables_the_light(wired);
  plain.delay_seconds        = 1.5f;
  plain.fire_once            = true;
  wired.map.connections.push_back(plain);

  // An override, so the payload half of the format is exercised too:
  // Color_Changed(color: v3) does pass through to Set_Color(color: v3), which
  // makes this the case where the author overrode it anyway.
  shared::connection_t overridden;
  overridden.sender               = wired.light;
  overridden.signal               = entities::entity_signal::Color_Changed;
  overridden.target_kind          = shared::connection_target_t::Self;
  overridden.data.tag             = entities::entity_action::Set_Color;
  overridden.data.set_color.color = {0.25f, 0.5f, 0.75f};
  overridden.has_override         = true;
  wired.map.connections.push_back(overridden);

  // An activator target, whose kind has no uid beside it to carry it.
  shared::connection_t to_activator;
  to_activator.sender      = wired.trigger;
  to_activator.signal      = entities::entity_signal::Left;
  to_activator.target_kind = shared::connection_target_t::Activator;
  to_activator.data.tag    = entities::entity_action::Kill;
  wired.map.connections.push_back(to_activator);

  const std::string text   = shared::serialize_map_to_string(wired.map);
  const shared::map_t back = shared::parse_map_from_string(text);

  check(back.connections.size() == 3, "all three rows come back");
  if (back.connections.size() != 3)
    return;

  check(back.connections[0].sender == plain.sender, "the sender uid survives");
  check(back.connections[0].target == plain.target, "the target uid survives");
  check(back.connections[0].signal == entities::entity_signal::Touched, "the signal survives");
  check(back.connections[0].data.tag == entities::entity_action::Enable, "the action survives");
  check(back.connections[0].delay_seconds == 1.5f, "the delay survives");
  check(back.connections[0].fire_once, "fire_once survives");
  check(!back.connections[0].has_override, "a row with no override does not gain one");

  check(back.connections[1].has_override, "the override survives as an override");
  check(back.connections[1].target_kind == shared::connection_target_t::Self,
        "the Self target kind survives");
  check(back.connections[1].data.set_color.color.x == 0.25f &&
            back.connections[1].data.set_color.color.y == 0.5f &&
            back.connections[1].data.set_color.color.z == 0.75f,
        "the override's payload survives through field_to_text and back");

  check(back.connections[2].target_kind == shared::connection_target_t::Activator,
        "the Activator target kind survives with no uid beside it");

  // The map's content hash is taken over this text, so a connection changing
  // has to change it -- otherwise a client holding the old wiring reports
  // map_ready against a map it does not have.
  shared::map_t without = wired.map;
  without.connections.clear();
  check(shared::compute_map_content_hash(without) != shared::compute_map_content_hash(wired.map),
        "the connections are inside the map's content hash");
}

// --- 2. the load check ------------------------------------------------------

// Whether the check refused the row at `index`, and nothing else about it: a
// row can be refused for two reasons at once, and the test cares which ROW.
bool refused(const shared::map_t& map, size_t index)
{
  for (const shared::connection_refusal_t& refusal : shared::validate_map_connections(map))
    if (refusal.index == index)
      return true;
  return false;
}

void test_the_load_check_refuses_what_cannot_run()
{
  std::printf("connections: the load check\n");

  {
    wired_map_t wired = make_wired_map();
    wired.map.connections.push_back(touched_enables_the_light(wired));
    check(shared::validate_map_connections(wired.map).empty(),
          "a well-typed row is accepted with no reason at all");
  }

  {
    wired_map_t wired = make_wired_map();
    shared::connection_t row = touched_enables_the_light(wired);
    row.sender               = 999;
    wired.map.connections.push_back(row);
    check(refused(wired.map, 0), "a sender uid naming nothing is refused");
  }

  {
    wired_map_t wired = make_wired_map();
    shared::connection_t row = touched_enables_the_light(wired);
    row.target               = 999;
    wired.map.connections.push_back(row);
    check(refused(wired.map, 0), "a target uid naming nothing is refused");
  }

  {
    // The light does not emit Touched -- only a Touchable does.
    wired_map_t wired = make_wired_map();
    shared::connection_t row = touched_enables_the_light(wired);
    row.sender               = wired.light;
    wired.map.connections.push_back(row);
    check(refused(wired.map, 0), "a sender that does not emit the signal is refused");
  }

  {
    // A trigger volume is Switchable but not Colorable.
    wired_map_t wired = make_wired_map();
    shared::connection_t row = touched_enables_the_light(wired);
    row.target               = wired.trigger;
    row.data.tag             = entities::entity_action::Set_Color;
    row.has_override         = true;
    wired.map.connections.push_back(row);
    check(refused(wired.map, 0), "a target that does not accept the action is refused");
  }

  {
    // NOTHING that can touch is Colorable, so this row could never do anything
    // whoever walks in -- which is the one activator case the load check can
    // still settle, and the whole point of declaring `by` at all.
    wired_map_t wired = make_wired_map();
    shared::connection_t row = touched_enables_the_light(wired);
    row.target_kind          = shared::connection_target_t::Activator;
    row.data.tag             = entities::entity_action::Set_Color;
    row.has_override         = true;
    wired.map.connections.push_back(row);
    check(refused(wired.map, 0),
          "an activator target is refused when NO type in the `by` set accepts the action");
  }

  {
    // SOME, not every, and this is the row all-accept could not spell. Touched
    // is truthfully activated by a player or a physics body; a player is Mortal
    // and a crate is not, and "kill whoever touched this" is the most ordinary
    // trigger in any level. Under all-accept the crate refused the row for the
    // player; the miss is the drain's business now.
    wired_map_t wired = make_wired_map();
    shared::connection_t row = touched_enables_the_light(wired);
    row.target_kind          = shared::connection_target_t::Activator;
    row.data.tag             = entities::entity_action::Kill;
    row.has_override         = true;
    wired.map.connections.push_back(row);
    check(!refused(wired.map, 0),
          "an activator target is accepted when SOME type in the `by` set accepts the action");
  }

  {
    // Color_Changed declares no `by`, so there is no type to check an
    // activator target against and nothing may target one.
    wired_map_t wired = make_wired_map();
    shared::connection_t row;
    row.sender       = wired.light;
    row.signal       = entities::entity_signal::Color_Changed;
    row.target_kind  = shared::connection_target_t::Activator;
    row.data.tag     = entities::entity_action::Enable;
    row.has_override = true;
    wired.map.connections.push_back(row);
    check(refused(wired.map, 0), "an activator target on a signal with no `by` is refused");
  }

  {
    // Color_Changed carries a v3 and Enable takes nothing, so there is no
    // pass-through. With an override it is fine; without one it is refused.
    wired_map_t wired = make_wired_map();
    shared::connection_t row;
    row.sender      = wired.light;
    row.signal      = entities::entity_signal::Color_Changed;
    row.target_kind = shared::connection_target_t::Self;
    row.data.tag    = entities::entity_action::Enable;
    wired.map.connections.push_back(row);
    check(refused(wired.map, 0), "a payload that cannot pass through is refused without an override");

    wired.map.connections[0].has_override = true;
    check(!refused(wired.map, 0), "the same row with an override is accepted");
  }

  check(shared::signal_payload_passes_through(entities::entity_signal::Color_Changed,
                                              entities::entity_action::Set_Color),
        "Color_Changed(color: v3) passes through to Set_Color(color: v3)");
  check(shared::signal_payload_passes_through(entities::entity_signal::Touched,
                                              entities::entity_action::Enable),
        "two empty payloads pass through");
  check(!shared::signal_payload_passes_through(entities::entity_signal::Died,
                                               entities::entity_action::Set_Health),
        "Died(killer: entity) does not pass through to Set_Health(amount: i32) -- same width, "
        "different meaning, and the field names are what separate them");
}

// --- 3. one check, two policies ---------------------------------------------

void test_build_session_drops_what_the_check_named()
{
  std::printf("connections: build_session drops the bad rows and indexes the rest\n");

  wired_map_t wired = make_wired_map();
  wired.map.connections.push_back(touched_enables_the_light(wired));

  shared::connection_t bad = touched_enables_the_light(wired);
  bad.target               = 999;
  wired.map.connections.push_back(bad);

  wired.map.connections.push_back(touched_enables_the_light(wired));

  const shared::game_session_t session = shared::build_session(wired.map);

  auto bucket = session.connections_by_sender.find(wired.trigger);
  check(bucket != session.connections_by_sender.end(), "the sender has a bucket");
  if (bucket != session.connections_by_sender.end())
    check(bucket->second.size() == 2, "the two well-typed rows are indexed and the third is not");

  check(session.connections_by_sender.find(wired.light) == session.connections_by_sender.end(),
        "an entity with no rows has no bucket at all");
  check(wired.map.connections.size() == 3, "the map itself is not edited by building a session");
}

// --- 4. the queue -----------------------------------------------------------

// A context standing on nothing but a map: no socket, no clients, no physics.
// The queue reaches world.session and world.pending_actions and nothing else,
// which is what makes this testable without a server.
void install(server_context_t& context, cvars::cvar_state_t& cvar_state, const shared::map_t& map)
{
  context.cvars               = &cvar_state;
  context.cvars->sv_tickrate  = (float)tickrate;
  context.tick_number         = 1;
  context.world.current_map   = map;
  context.world.session       = shared::build_session(map);
}

const entities::Point_Light_Entity* light_in(const server_context_t& context,
                                             shared::entity_uid_t uid)
{
  return const_cast<server_context_t&>(context)
      .world.session.entity_system.get<entities::Point_Light_Entity>(uid);
}

void emit_touched_from(server_context_t& context, shared::entity_uid_t sender,
                       shared::entity_uid_t activator)
{
  entities::Entity* trigger = context.world.session.entity_system.try_find(sender);
  input_context_t   handler_context{context, activator, context.tick_number};
  entities::emit_touched(*trigger, {}, handler_context);
}

void test_a_connection_is_queued_and_then_delivered()
{
  std::printf("connections: the queue\n");

  wired_map_t wired = make_wired_map();
  wired.map.connections.push_back(touched_enables_the_light(wired));

  cvars::cvar_state_t cvar_state;
  server_context_t    context;
  install(context, cvar_state, wired.map);

  check(light_in(context, wired.light) != nullptr && !light_in(context, wired.light)->switch_state.value,
        "the light starts disabled");

  emit_touched_from(context, wired.trigger, 0);

  // Delay zero and it is STILL queued. Everything from a connection is, which
  // is the reentrancy guard: an action reached this way must not run under the
  // system that emitted the signal.
  check(context.world.pending_actions.size() == 1, "the emit queued one record");
  check(!light_in(context, wired.light)->switch_state.value,
        "and delivered nothing -- delay zero still waits for the drain");

  drain_pending_actions(context);
  check(context.world.pending_actions.empty(), "the drain empties the queue");
  check(light_in(context, wired.light)->switch_state.value, "the handler ran and enabled the light");
}

void test_a_delay_is_counted_in_ticks()
{
  std::printf("connections: a delayed connection\n");

  wired_map_t wired        = make_wired_map();
  shared::connection_t row = touched_enables_the_light(wired);
  row.delay_seconds        = 0.5f;   // 30 ticks at 60Hz
  wired.map.connections.push_back(row);

  cvars::cvar_state_t cvar_state;
  server_context_t    context;
  install(context, cvar_state, wired.map);

  emit_touched_from(context, wired.trigger, 0);
  check(context.world.pending_actions.size() == 1, "the delayed record is queued");
  check(context.world.pending_actions[0].fire_tick == context.tick_number + 30,
        "half a second at 60Hz is 30 ticks");

  for (uint32_t tick = 0; tick < 30; ++tick)
  {
    drain_pending_actions(context);
    check(!light_in(context, wired.light)->switch_state.value,
          "the light stays off for every tick before its own");
    ++context.tick_number;
  }

  drain_pending_actions(context);
  check(light_in(context, wired.light)->switch_state.value, "and turns on when it does");
}

void test_emit_order_breaks_a_tie_within_one_tick()
{
  std::printf("connections: two records on one tick keep emit order\n");

  wired_map_t wired = make_wired_map();

  // Disable then enable, both from the same signal with no delay: the light
  // ends ON only if the second row ran second.
  shared::connection_t off = touched_enables_the_light(wired);
  off.data.tag             = entities::entity_action::Disable;
  wired.map.connections.push_back(off);
  wired.map.connections.push_back(touched_enables_the_light(wired));

  cvars::cvar_state_t cvar_state;
  server_context_t    context;
  install(context, cvar_state, wired.map);

  emit_touched_from(context, wired.trigger, 0);
  check(context.world.pending_actions.size() == 2, "both rows queued");
  check(context.world.pending_actions[0].sequence < context.world.pending_actions[1].sequence,
        "the sequence counter separates them");

  drain_pending_actions(context);
  check(light_in(context, wired.light)->switch_state.value,
        "the second row ran second, so the light ends enabled");
}

void test_fire_once_spends_the_sessions_copy()
{
  std::printf("connections: fire_once\n");

  wired_map_t wired        = make_wired_map();
  shared::connection_t row = touched_enables_the_light(wired);
  row.fire_once            = true;
  wired.map.connections.push_back(row);

  cvars::cvar_state_t cvar_state;
  server_context_t    context;
  install(context, cvar_state, wired.map);

  emit_touched_from(context, wired.trigger, 0);
  check(context.world.pending_actions.size() == 1, "the first touch queues");
  drain_pending_actions(context);

  emit_touched_from(context, wired.trigger, 0);
  check(context.world.pending_actions.empty(), "the second touch queues nothing");

  // The MAP's row is untouched, which is what makes a reload start over and
  // what stops the editor's copy from acquiring runtime state.
  check(context.world.current_map.connections[0].fire_once,
        "the map's own row is untouched");
  const shared::game_session_t fresh = shared::build_session(context.world.current_map);
  check(!fresh.connections_by_sender.at(wired.trigger)[0].spent,
        "a fresh session from the same map starts unspent");
}

void test_a_target_that_dies_during_the_delay_is_dropped()
{
  std::printf("connections: a target destroyed while the record waits\n");

  wired_map_t wired        = make_wired_map();
  shared::connection_t row = touched_enables_the_light(wired);
  row.delay_seconds        = 0.5f;
  wired.map.connections.push_back(row);

  cvars::cvar_state_t cvar_state;
  server_context_t    context;
  install(context, cvar_state, wired.map);

  emit_touched_from(context, wired.trigger, 0);
  check(context.world.session.entity_system.destroy(wired.light), "the light is destroyed");

  context.tick_number += 60;
  drain_pending_actions(context);   // logs and drops; a fatal here is the failure
  check(context.world.pending_actions.empty(), "the record is gone rather than retried forever");
}


// --- 5. the trigger system: where a Touched comes from ----------------------
//
// The pin for ss11 step 4. What this replaced dispatched a Trigger_Action enum
// straight at the toucher; the volume now says only WHEN, and the two things
// worth pinning are that WHEN is an EDGE and that a disabled volume has no
// overlaps rather than merely no new ones.

shared::entity_uid_t place_player_in(server_context_t& context, const vec3f& position)
{
  const shared::entity_uid_t uid =
      context.world.session.entity_system.spawn<entities::Player_Entity>();
  context.world.session.entity_system.get<entities::Player_Entity>(uid)->position = position;
  return uid;
}

void move_player(server_context_t& context, shared::entity_uid_t uid, const vec3f& position)
{
  context.world.session.entity_system.get<entities::Player_Entity>(uid)->position = position;
}

void test_the_trigger_system_emits_edges()
{
  std::printf("connections: a trigger emits Touched on entry and Left on exit\n");

  wired_map_t wired = make_wired_map();

  shared::connection_t on = touched_enables_the_light(wired);
  wired.map.connections.push_back(on);

  shared::connection_t off = touched_enables_the_light(wired);
  off.signal   = entities::entity_signal::Left;
  off.data.tag = entities::entity_action::Disable;
  wired.map.connections.push_back(off);

  cvars::cvar_state_t cvar_state;
  server_context_t    context;
  install(context, cvar_state, wired.map);

  // The trigger is at the origin with the default 64-unit half extents, so a
  // player standing there overlaps and one 500 units away does not.
  const shared::entity_uid_t player = place_player_in(context, {0.f, 0.f, 0.f});

  update_triggers(context);
  check(context.world.pending_actions.size() == 1, "walking in emits exactly one Touched");
  drain_pending_actions(context);
  check(light_in(context, wired.light)->switch_state.value, "...which turned the light on");

  // The EDGE, which is the whole reason fire_mode is gone: standing still
  // inside a volume is not a second touch.
  ++context.tick_number;
  update_triggers(context);
  check(context.world.pending_actions.empty(), "standing still emits nothing");

  ++context.tick_number;
  move_player(context, player, {500.f, 0.f, 0.f});
  update_triggers(context);
  check(context.world.pending_actions.size() == 1, "walking out emits exactly one Left");
  drain_pending_actions(context);
  check(!light_in(context, wired.light)->switch_state.value, "...which turned it off again");

  ++context.tick_number;
  update_triggers(context);
  check(context.world.pending_actions.empty(), "and staying out emits nothing");
}

void test_a_disabled_trigger_releases_whoever_is_inside()
{
  std::printf("connections: switching a trigger off is a Left, not a freeze\n");

  wired_map_t wired = make_wired_map();
  shared::connection_t off = touched_enables_the_light(wired);
  off.signal   = entities::entity_signal::Left;
  off.data.tag = entities::entity_action::Disable;
  wired.map.connections.push_back(off);

  cvars::cvar_state_t cvar_state;
  server_context_t    context;
  install(context, cvar_state, wired.map);

  place_player_in(context, {0.f, 0.f, 0.f});
  update_triggers(context);
  context.world.pending_actions.clear();

  // A Touched with no Left is a door that never closes, which is worse than
  // one that closes early -- so the volume going dark releases the toucher.
  ++context.tick_number;
  context.world.session.entity_system.get<entities::Trigger_Volume_Entity>(wired.trigger)
      ->switch_state.value = false;
  update_triggers(context);
  check(context.world.pending_actions.size() == 1,
        "disabling a volume somebody stands in emits their Left");
}

// The activator is resolved at EMIT time, and for a trigger that is whoever
// walked in -- which is what makes every converted Trigger_Action row work.
// The other half of some-accept: the check let the row through because a PLAYER
// accepts Kill, and a crate is what actually rolled in. That has to be a logged
// miss rather than the fatal_error a Uid target's null dispatch cell earns --
// under all-accept this row could not exist, so nothing ever reached here.
void test_an_activator_that_does_not_accept_is_a_logged_miss()
{
  std::printf("connections: an activator that does not accept the action is dropped, not fatal\n");

  wired_map_t wired = make_wired_map();
  shared::connection_t row = touched_enables_the_light(wired);
  row.target_kind          = shared::connection_target_t::Activator;
  row.data.tag             = entities::entity_action::Kill;
  row.has_override         = true;
  wired.map.connections.push_back(row);

  cvars::cvar_state_t cvar_state;
  server_context_t    context;
  install(context, cvar_state, wired.map);

  const shared::entity_uid_t crate =
      context.world.session.entity_system.spawn<entities::Physics_Body_Entity>();
  context.world.session.entity_system.get<entities::Physics_Body_Entity>(crate)->position = {
      0.f, 0.f, 0.f};

  update_triggers(context);
  check(context.world.pending_actions.size() == 1 &&
            context.world.pending_actions[0].target == crate,
        "a physics body touching a volume emits, and the record names the crate");
  check(context.world.pending_actions[0].target_resolved_from_activator,
        "the record remembers it came from an Activator row");

  // Reaching this without a fatal_error IS the assertion.
  drain_pending_actions(context);
  check(context.world.pending_actions.empty(),
        "the drain consumed it and did not die on a crate that cannot be killed");
}

void test_the_toucher_is_the_activator()
{
  std::printf("connections: the toucher is the activator\n");

  wired_map_t wired = make_wired_map();
  shared::connection_t row = touched_enables_the_light(wired);
  row.target_kind          = shared::connection_target_t::Activator;
  row.data.tag             = entities::entity_action::Set_Respawn_Point;
  row.has_override         = true;
  row.data.set_respawn_point.location = wired.trigger;
  wired.map.connections.push_back(row);

  cvars::cvar_state_t cvar_state;
  server_context_t    context;
  install(context, cvar_state, wired.map);

  const shared::entity_uid_t player = place_player_in(context, {0.f, 0.f, 0.f});

  update_triggers(context);
  check(context.world.pending_actions.size() == 1 &&
            context.world.pending_actions[0].target == player,
        "the queued record names the player who walked in");

  drain_pending_actions(context);
  check(context.world.session.entity_system.get<entities::Player_Entity>(player)->checkpoint_uid ==
            wired.trigger,
        "and the handler wrote the volume the row named");
}

// --- 5. Died and Health_Changed, out of the damage choke point ---------------

// A crate wired to a lamp, which is step 5's whole point: the second sender,
// and the first that is not a volume.
struct wired_damageable_t
{
  shared::map_t        map;
  shared::entity_uid_t crate = 0;
  shared::entity_uid_t light = 0;
};

wired_damageable_t make_wired_damageable()
{
  wired_damageable_t wired;

  auto crate = std::make_shared<entities::Damageable_Entity>();
  crate->name.set("target_dummy");
  wired.crate = wired.map.add_entity(crate);

  auto light = std::make_shared<entities::Point_Light_Entity>();
  light->name.set("hall_lamp");
  light->switch_state.value = false;
  wired.light = wired.map.add_entity(light);

  return wired;
}

shared::connection_t signal_to_the_light(const wired_damageable_t& wired,
                                         entities::entity_signal signal,
                                         entities::entity_action action)
{
  shared::connection_t connection;
  connection.sender       = wired.crate;
  connection.signal       = signal;
  connection.target_kind  = shared::connection_target_t::Uid;
  connection.target       = wired.light;
  connection.data.tag     = action;
  connection.has_override = true;
  return connection;
}

pending_hit_t hit_on(shared::entity_uid_t victim, shared::entity_uid_t attacker, float amount)
{
  pending_hit_t hit;
  hit.info.victim_uid   = victim;
  hit.info.attacker_uid = attacker;
  hit.info.amount       = amount;
  return hit;
}

void test_a_damageable_emits_died_and_health_changed()
{
  std::printf("connections: a damageable announces its health and its death\n");

  wired_damageable_t wired = make_wired_damageable();
  wired.map.connections.push_back(signal_to_the_light(
      wired, entities::entity_signal::Health_Changed, entities::entity_action::Toggle_Enabled));
  wired.map.connections.push_back(signal_to_the_light(
      wired, entities::entity_signal::Died, entities::entity_action::Enable));

  cvars::cvar_state_t cvar_state;
  server_context_t    context;
  install(context, cvar_state, wired.map);
  context.world.rules.phase = shared::Round_Phase::Live;

  const shared::entity_uid_t grazer =
      context.world.session.entity_system.spawn<entities::Player_Entity>();
  const shared::entity_uid_t finisher =
      context.world.session.entity_system.spawn<entities::Player_Entity>();

  const pending_hit_t graze = hit_on(wired.crate, grazer, 10.f);
  inflict_damage_batch(context, Span<const pending_hit_t>{&graze, 1});

  check(context.world.pending_actions.size() == 1 &&
            context.world.pending_actions[0].data.tag == entities::entity_action::Toggle_Enabled,
        "a survivable hit announces the new health and nothing else");
  check(context.world.pending_actions.empty() ||
            context.world.pending_actions[0].activator == grazer,
        "and the activator is whoever landed it");
  context.world.pending_actions.clear();

  // Two hits, one tick, neither lethal alone. The sum kills, so Died fires
  // ONCE and its killer is the larger contributor -- the same rule the player
  // path credits a frag by.
  const pending_hit_t lethal[2] = {hit_on(wired.crate, grazer, 30.f),
                                   hit_on(wired.crate, finisher, 70.f)};
  inflict_damage_batch(context, Span<const pending_hit_t>{lethal, 2});

  uint32_t died_records = 0;
  shared::entity_uid_t killer = shared::null_entity_uid;
  for (const pending_action_t& record : context.world.pending_actions)
    if (record.data.tag == entities::entity_action::Enable)
    {
      ++died_records;
      killer = record.activator;
    }

  check(died_records == 1, "the crossing emits Died exactly once for the whole tick");
  check(killer == finisher, "and names the largest single contributor as the killer");

  const entities::Damageable_Entity* crate =
      context.world.session.entity_system.get<entities::Damageable_Entity>(wired.crate);
  check(crate != nullptr && crate->health.current_health <= 0 && !crate->render.visible,
        "the crate is destroyed and hidden, as it was before it had wiring");

  drain_pending_actions(context);
  const entities::Point_Light_Entity* lamp = light_in(context, wired.light);
  check(lamp != nullptr && lamp->switch_state.value, "and the lamp it was wired to came on");
}

} // namespace

// --- 5. the switch is VISIBLE ----------------------------------------------
//
// The end of the seam, and the half a queue test cannot see: an Enable that
// changes a bool nothing reads is a connection that does nothing. So this walks
// the wiring all the way to the frame array the shader is handed, through the
// one gather every draw path uses (entity_io_def.md ss11 step 6).

void test_a_switched_light_leaves_the_frame()
{
  std::printf("connections: the switch reaches the frame\n");

  wired_map_t wired = make_wired_map();
  wired.map.connections.push_back(touched_enables_the_light(wired));

  cvars::cvar_state_t cvar_state;
  server_context_t    context;
  install(context, cvar_state, wired.map);

  // Analytic, or a Baked light is deliberately absent from the tail whatever
  // its switch says and this would pass for the wrong reason.
  entities::Point_Light_Entity* lamp =
      context.world.session.entity_system.get<entities::Point_Light_Entity>(wired.light);
  lamp->light.mode = entities::Light_Mode::Dynamic;

  const shared::lightmap_t unbaked;

  const auto tail_entries_for_the_lamp = [&]() -> size_t
  {
    shared::frame_lights_t frame;
    shared::begin_frame_lights(frame, unbaked);
    shared::add_frame_light(frame, unbaked, wired.light, *lamp);
    return frame.entries.size();
  };

  check(!shared::light_is_switched_on(*lamp), "the light starts switched off");
  check(shared::try_light_of(*lamp).has_value(),
        "and is still a light -- try_light_of does not filter by the switch, so "
        "the inspector can describe one that is off");
  check(tail_entries_for_the_lamp() == 0, "a switched-off light is in no frame");

  emit_touched_from(context, wired.trigger, 0);
  drain_pending_actions(context);

  check(shared::light_is_switched_on(*lamp), "the connection switched it on");
  check(tail_entries_for_the_lamp() == 1, "and now it is in the frame");
}

int main()
{
  std::printf("--- entity I/O: connections, the load check and the queue ---\n");

  test_a_connection_survives_the_file();
  test_the_load_check_refuses_what_cannot_run();
  test_build_session_drops_what_the_check_named();
  test_a_connection_is_queued_and_then_delivered();
  test_a_delay_is_counted_in_ticks();
  test_emit_order_breaks_a_tie_within_one_tick();
  test_fire_once_spends_the_sessions_copy();
  test_a_target_that_dies_during_the_delay_is_dropped();
  test_the_trigger_system_emits_edges();
  test_a_disabled_trigger_releases_whoever_is_inside();
  test_the_toucher_is_the_activator();
  test_an_activator_that_does_not_accept_is_a_logged_miss();
  test_a_damageable_emits_died_and_health_changed();
  test_a_switched_light_leaves_the_frame();

  if (failure_count > 0)
  {
    std::printf("\n%d check(s) FAILED\n", failure_count);
    return 1;
  }

  std::printf("\nall checks passed\n");
  return 0;
}
