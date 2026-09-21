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
#include "server/entity_io_console.hpp"
#include "server/entity_io_queue.hpp"
#include "server/systems/game_rules_system.hpp"
#include "server/systems/timer_system.hpp"
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

void test_deleting_an_entity_drops_the_rows_naming_it()
{
  std::printf("connections: deleting an entity drops every row naming it\n");

  wired_map_t wired = make_wired_map();
  auto other = std::make_shared<entities::Point_Light_Entity>();
  const shared::entity_uid_t other_light = wired.map.add_entity(other);

  wired.map.connections.push_back(touched_enables_the_light(wired));

  shared::connection_t from_the_light;
  from_the_light.sender      = wired.light;
  from_the_light.signal      = entities::entity_signal::Color_Changed;
  from_the_light.target_kind = shared::connection_target_t::Self;
  from_the_light.data.tag    = entities::entity_action::Set_Color;
  wired.map.connections.push_back(from_the_light);

  shared::connection_t names_it_in_the_payload = touched_enables_the_light(wired);
  names_it_in_the_payload.target_kind                     = shared::connection_target_t::Activator;
  names_it_in_the_payload.data.tag                        = entities::entity_action::Set_Respawn_Point;
  names_it_in_the_payload.has_override                    = true;
  names_it_in_the_payload.data.set_respawn_point.location = wired.light;
  wired.map.connections.push_back(names_it_in_the_payload);

  shared::connection_t untouched = touched_enables_the_light(wired);
  untouched.target               = other_light;
  wired.map.connections.push_back(untouched);

  shared::connection_t already_dangling = touched_enables_the_light(wired);
  already_dangling.sender               = 999;
  already_dangling.target               = other_light;
  wired.map.connections.push_back(already_dangling);

  check(wired.map.remove_object(wired.light), "the light is removed");
  const shared::entity_uid_t removed[] = {wired.light};
  const size_t dropped = shared::remove_connections_naming(wired.map.connections, removed);

  check(dropped == 3, "the target row, the sender row and the payload row are dropped");
  check(wired.map.connections.size() == 2, "two rows remain");
  if (wired.map.connections.size() != 2)
    return;
  check(wired.map.connections[0].target == other_light && wired.map.connections[0].sender == wired.trigger,
        "a row naming only survivors is kept");
  check(wired.map.connections[1].sender == 999,
        "a row already naming a missing uid is not the delete's to drop");
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
  install_match(context, context.tick_number, tickrate);
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

  deliver_pending_entity_actions(context);
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
    deliver_pending_entity_actions(context);
    check(!light_in(context, wired.light)->switch_state.value,
          "the light stays off for every tick before its own");
    ++context.tick_number;
  }

  deliver_pending_entity_actions(context);
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

  deliver_pending_entity_actions(context);
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
  deliver_pending_entity_actions(context);

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
  deliver_pending_entity_actions(context);   // logs and drops; a fatal here is the failure
  check(context.world.pending_actions.empty(), "the record is gone rather than retried forever");
}

// --- 4b. the drain HOPS -----------------------------------------------------
//
// A counter emits Limit_Reached from inside its own Add handler, so a row off
// that signal is an action queued during the drain. Under a drain at the top of
// the tick each of those cost a whole tick; it runs at the END of the tick now
// and loops while anything is due, so a zero-delay chain settles inside the
// tick that started it (prediction_def.md ss2). The reentrancy guard is
// unchanged and is what the queue sizes below assert: a handler's emit opens
// the NEXT hop, never a nested dispatch.

shared::entity_uid_t add_counter_to(shared::map_t& map, const char* name, int32_t limit)
{
  auto counter = std::make_shared<entities::Logic_Counter_Entity>();
  counter->name.set(name);
  counter->counter.limit = limit;
  return map.add_entity(counter);
}

shared::connection_t limit_reached_adds_one(shared::entity_uid_t sender,
                                            shared::connection_target_t target_kind,
                                            shared::entity_uid_t target)
{
  shared::connection_t connection;
  connection.sender          = sender;
  connection.signal          = entities::entity_signal::Limit_Reached;
  connection.target_kind     = target_kind;
  connection.target          = target;
  connection.data.tag        = entities::entity_action::Add;
  connection.data.add.amount = 1;
  // Limit_Reached() carries nothing and Add takes an amount, so the row has to
  // say what it sends -- a pass-through needs the two field tables to match.
  connection.has_override = true;
  return connection;
}

int32_t counter_value_in(server_context_t& context, shared::entity_uid_t uid)
{
  const entities::Logic_Counter_Entity* counter =
      context.world.session.entity_system.get<entities::Logic_Counter_Entity>(uid);
  return counter == nullptr ? -1 : counter->counter.value;
}

void test_a_zero_delay_chain_settles_in_one_drain()
{
  std::printf("connections: a chain of emits settles inside one tick\n");

  wired_map_t wired = make_wired_map();

  const shared::entity_uid_t first  = add_counter_to(wired.map, "first", 1);
  const shared::entity_uid_t second = add_counter_to(wired.map, "second", 1);
  const shared::entity_uid_t third  = add_counter_to(wired.map, "third", 1);

  shared::connection_t touch = touched_enables_the_light(wired);
  touch.target               = first;
  touch.data.tag             = entities::entity_action::Add;
  touch.data.add.amount      = 1;
  touch.has_override         = true;
  wired.map.connections.push_back(touch);

  wired.map.connections.push_back(
      limit_reached_adds_one(first, shared::connection_target_t::Uid, second));
  wired.map.connections.push_back(
      limit_reached_adds_one(second, shared::connection_target_t::Uid, third));

  shared::connection_t finish;
  finish.sender      = third;
  finish.signal      = entities::entity_signal::Limit_Reached;
  finish.target_kind = shared::connection_target_t::Uid;
  finish.target      = wired.light;
  finish.data.tag    = entities::entity_action::Enable;
  wired.map.connections.push_back(finish);

  cvars::cvar_state_t cvar_state;
  server_context_t    context;
  install(context, cvar_state, wired.map);

  const uint32_t tick_it_started_on = context.tick_number;

  emit_touched_from(context, wired.trigger, 0);
  check(context.world.pending_actions.size() == 1,
        "the touch queues one record and dispatches nothing");

  deliver_pending_entity_actions(context);

  check(counter_value_in(context, first) == 1 && counter_value_in(context, second) == 1 &&
            counter_value_in(context, third) == 1,
        "all three counters were reached by one drain");
  check(light_in(context, wired.light) != nullptr && light_in(context, wired.light)->switch_state.value,
        "and so was the light four hops down the chain");
  check(context.world.pending_actions.empty(), "with nothing left over for the next tick");
  check(context.tick_number == tick_it_started_on,
        "the whole chain belongs to the tick the signal fired in");
}

void test_a_wiring_loop_is_capped_and_dropped()
{
  std::printf("connections: a loop hits the hop cap (error lines below are the test passing)\n");

  wired_map_t wired = make_wired_map();

  // Reset first, then Add: the counter is put back below its limit and crossed
  // again, so it emits Limit_Reached on every hop forever. Both rows target
  // Self, which is what makes one entity a complete loop.
  const shared::entity_uid_t counter = add_counter_to(wired.map, "spinner", 1);

  shared::connection_t touch = touched_enables_the_light(wired);
  touch.target               = counter;
  touch.data.tag             = entities::entity_action::Add;
  touch.data.add.amount      = 1;
  touch.has_override         = true;
  wired.map.connections.push_back(touch);

  shared::connection_t rewind;
  rewind.sender      = counter;
  rewind.signal      = entities::entity_signal::Limit_Reached;
  rewind.target_kind = shared::connection_target_t::Self;
  rewind.data.tag    = entities::entity_action::Reset;
  wired.map.connections.push_back(rewind);

  wired.map.connections.push_back(
      limit_reached_adds_one(counter, shared::connection_target_t::Self, 0));

  cvars::cvar_state_t cvar_state;
  server_context_t    context;
  install(context, cvar_state, wired.map);

  // A delayed record, to pin that the cap drops what is DUE and nothing else.
  shared::connection_t later = touched_enables_the_light(wired);
  later.delay_seconds        = 1.0f;
  context.world.current_map.connections.push_back(later);
  context.world.session = shared::build_session(context.world.current_map);

  emit_touched_from(context, wired.trigger, 0);

  // The failure this pins is a hang, so reaching the next line at all is most
  // of it.
  deliver_pending_entity_actions(context);

  check(context.world.pending_actions.size() == 1,
        "the still-due records are dropped rather than run again every tick");
  check(!context.world.pending_actions.empty() &&
            context.world.pending_actions[0].fire_tick > context.tick_number,
        "and the one that survives is the delayed record, which is not part of the loop");
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
  deliver_pending_entity_actions(context);
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
  deliver_pending_entity_actions(context);
  check(!light_in(context, wired.light)->switch_state.value, "...which turned it off again");

  ++context.tick_number;
  update_triggers(context);
  check(context.world.pending_actions.empty(), "and staying out emits nothing");
}

shared::connection_t wire(shared::entity_uid_t sender, entities::entity_signal signal,
                          shared::entity_uid_t target, entities::entity_action action)
{
  shared::connection_t connection;
  connection.sender      = sender;
  connection.signal      = signal;
  connection.target_kind = shared::connection_target_t::Uid;
  connection.target      = target;
  connection.data.tag    = action;
  return connection;
}

void run_one_tick(server_context_t& context)
{
  ++context.tick_number;
  update_triggers(context);
  update_timers(context);
  deliver_pending_entity_actions(context);
}

void test_a_platform_crumbles_under_one_and_holds_under_two()
{
  std::printf("connections: a counter's falling edge resumes a paused timer\n");

  wired_map_t wired = make_wired_map();

  const shared::entity_uid_t occupancy = add_counter_to(wired.map, "occupancy", 2);

  auto timer_entity = std::make_shared<entities::Logic_Timer_Entity>();
  timer_entity->name.set("crumble");
  timer_entity->timer.duration_seconds = 1.0f;
  const shared::entity_uid_t timer = wired.map.add_entity(timer_entity);

  shared::connection_t entered = wire(wired.trigger, entities::entity_signal::Touched, occupancy,
                                      entities::entity_action::Add);
  entered.data.add.amount = 1;
  entered.has_override    = true;
  wired.map.connections.push_back(entered);

  shared::connection_t left = wire(wired.trigger, entities::entity_signal::Left, occupancy,
                                   entities::entity_action::Add);
  left.data.add.amount = -1;
  left.has_override    = true;
  wired.map.connections.push_back(left);

  wired.map.connections.push_back(wire(wired.trigger, entities::entity_signal::Touched, timer,
                                       entities::entity_action::Start));
  wired.map.connections.push_back(wire(occupancy, entities::entity_signal::Limit_Reached, timer,
                                       entities::entity_action::Pause));
  wired.map.connections.push_back(wire(occupancy, entities::entity_signal::Fell_Below_Limit, timer,
                                       entities::entity_action::Resume));
  wired.map.connections.push_back(wire(timer, entities::entity_signal::Elapsed, wired.light,
                                       entities::entity_action::Enable));

  cvars::cvar_state_t cvar_state;
  server_context_t    context;
  install(context, cvar_state, wired.map);

  const entities::Logic_Timer_Entity* crumble =
      context.world.session.entity_system.get<entities::Logic_Timer_Entity>(timer);

  const shared::entity_uid_t first = place_player_in(context, {0.f, 0.f, 0.f});
  run_one_tick(context);
  check(crumble->timer.running, "the first touch starts the timer");

  for (uint32_t tick = 0; tick < 29; ++tick)
    run_one_tick(context);

  const shared::entity_uid_t second = place_player_in(context, {0.f, 0.f, 0.f});
  run_one_tick(context);
  check(counter_value_in(context, occupancy) == 2, "the second touch is counted");
  check(!crumble->timer.running && crumble->timer.paused_remaining_ticks > 0,
        "and pauses the timer rather than restarting it");
  const uint32_t remaining_when_paused = crumble->timer.paused_remaining_ticks;

  for (uint32_t tick = 0; tick < 120; ++tick)
    run_one_tick(context);
  check(!light_in(context, wired.light)->switch_state.value,
        "two players can stand there for as long as they like");
  check(crumble->timer.paused_remaining_ticks == remaining_when_paused,
        "and the clock does not move while they do");

  move_player(context, second, {500.f, 0.f, 0.f});
  run_one_tick(context);
  check(crumble->timer.running, "the second leaving resumes it");

  for (uint32_t tick = 0; tick + 2 < remaining_when_paused; ++tick)
    run_one_tick(context);
  check(!light_in(context, wired.light)->switch_state.value,
        "with what was left, not a fresh duration");

  for (uint32_t tick = 0; tick < 4; ++tick)
    run_one_tick(context);
  check(light_in(context, wired.light)->switch_state.value, "and then it elapses");

  (void)first;
}

void test_a_launcher_fires_its_weapon_row_along_its_aim()
{
  std::printf("connections: a launcher fires one projectile per Fire, and none while disabled\n");

  wired_map_t wired = make_wired_map();

  auto launcher_entity = std::make_shared<entities::Launcher_Entity>();
  launcher_entity->name.set("bubble_cannon");
  launcher_entity->position = {0.f, 300.f, 0.f};
  const shared::entity_uid_t launcher = wired.map.add_entity(launcher_entity);

  wired.map.connections.push_back(wire(wired.trigger, entities::entity_signal::Touched, launcher,
                                       entities::entity_action::Fire));

  cvars::cvar_state_t cvar_state;
  server_context_t    context;
  install(context, cvar_state, wired.map);

  shared::Entity_System& entity_system = context.world.session.entity_system;
  const auto bubble_count = [&entity_system]()
  {
    size_t count = 0;
    for (const entities::Bubble_Entity& bubble : entity_system.entities_of<entities::Bubble_Entity>())
    {
      (void)bubble;
      ++count;
    }
    return count;
  };

  const shared::entity_uid_t toucher = place_player_in(context, {0.f, 0.f, 0.f});
  run_one_tick(context);
  check(bubble_count() == 1, "one touch is one bubble");

  for (const entities::Bubble_Entity& bubble : entity_system.entities_of<entities::Bubble_Entity>())
  {
    const entities::Launcher_Entity* in_session = entity_system.get<entities::Launcher_Entity>(launcher);
    check(bubble.projectile.owner_uid == launcher, "owned by the launcher, not by whoever touched");
    check(bubble.position.y == 300.f, "spawned at the launcher");
    const linalg::vec3f aim = linalg::forward(in_session->orientation);
    check(linalg::dot(bubble.projectile.velocity, aim) > 0.f &&
              linalg::length(linalg::cross(bubble.projectile.velocity, aim)) < 0.001f,
          "flying along the launcher's orientation");
  }

  move_player(context, toucher, {5000.f, 0.f, 0.f});
  run_one_tick(context);
  entity_system.get<entities::Launcher_Entity>(launcher)->switch_state.value = false;
  move_player(context, toucher, {0.f, 0.f, 0.f});
  run_one_tick(context);
  check(bubble_count() == 1, "a disabled launcher swallows its Fire");
}

std::vector<linalg::vec3f> launcher_shot_directions(uint32_t shot_count)
{
  wired_map_t wired = make_wired_map();

  auto launcher_entity = std::make_shared<entities::Launcher_Entity>();
  launcher_entity->position             = {0.f, 300.f, 0.f};
  launcher_entity->weapon               = entities::Weapon::Rocket_Launcher;
  launcher_entity->spread_yaw_degrees   = 20.f;
  launcher_entity->spread_pitch_degrees = 10.f;
  const shared::entity_uid_t launcher = wired.map.add_entity(launcher_entity);

  wired.map.connections.push_back(wire(wired.trigger, entities::entity_signal::Touched, launcher,
                                       entities::entity_action::Fire));

  cvars::cvar_state_t cvar_state;
  server_context_t    context;
  install(context, cvar_state, wired.map);

  const shared::entity_uid_t toucher = place_player_in(context, {5000.f, 0.f, 0.f});

  std::vector<linalg::vec3f> directions;
  for (uint32_t shot = 0; shot < shot_count; ++shot)
  {
    move_player(context, toucher, {0.f, 0.f, 0.f});
    run_one_tick(context);
    move_player(context, toucher, {5000.f, 0.f, 0.f});
    run_one_tick(context);
  }

  for (const entities::Rocket_Entity& rocket :
       context.world.session.entity_system.entities_of<entities::Rocket_Entity>())
    directions.push_back(linalg::normalize(rocket.projectile.velocity));
  return directions;
}

void test_a_launchers_spread_is_the_same_pattern_every_attempt()
{
  std::printf("connections: a launcher's spread is derived, bounded and repeats per attempt\n");

  constexpr uint32_t SHOT_COUNT = 8;
  const std::vector<linalg::vec3f> first  = launcher_shot_directions(SHOT_COUNT);
  const std::vector<linalg::vec3f> second = launcher_shot_directions(SHOT_COUNT);

  check(first.size() == SHOT_COUNT && second.size() == SHOT_COUNT, "every Fire spawned a rocket");

  bool repeats = first.size() == second.size();
  bool bounded = true;
  bool varies  = false;
  for (size_t shot = 0; shot < first.size() && shot < second.size(); ++shot)
  {
    repeats = repeats && linalg::length(first[shot] - second[shot]) == 0.f;
    // Forward is +X at the identity: yaw swings it in XZ, pitch lifts it in Y.
    const float yaw_degrees   = std::atan2(first[shot].z, first[shot].x) * (180.f / 3.14159265f);
    const float pitch_degrees = std::asin(first[shot].y) * (180.f / 3.14159265f);
    bounded = bounded && std::abs(yaw_degrees) <= 20.001f && std::abs(pitch_degrees) <= 10.001f;
    varies  = varies || linalg::length(first[shot] - first[0]) > 0.01f;
  }
  check(repeats, "a second attempt fires the same directions, shot for shot");
  check(bounded, "every shot is inside the two half-angles");
  check(varies, "and they are not all the same shot");
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
  deliver_pending_entity_actions(context);
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

  deliver_pending_entity_actions(context);
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
  match_of(context).phase = entities::Round_Phase::Live;

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

  deliver_pending_entity_actions(context);
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
  deliver_pending_entity_actions(context);

  check(shared::light_is_switched_on(*lamp), "the connection switched it on");
  check(tail_entries_for_the_lamp() == 1, "and now it is in the frame");
}

// --- 6. ent_fire's parameter grammar ---------------------------------------
//
// The console's `field=value` tail is split by the ACTION'S FIELD TABLE, not by
// whitespace -- a v3 writes as "1 0 0", so whitespace would cut one value into
// three. Pinned here because getting it wrong sends the field's DEFAULT rather
// than failing, which is the silent kind of wrong (entity_io_def.md ss11 6c).

void test_the_console_parses_a_parameter_tail()
{
  std::printf("ent_fire: the field=value grammar\n");

  {
    entities::action_data_t data;
    data.tag = entities::entity_action::Set_Velocity;

    // The whole point: three numbers, two spaces, ONE value.
    const std::vector<std::string> refusals =
        server::parse_action_parameters(data, "velocity=0 0 400");
    check(refusals.empty(), "a v3 whose value contains spaces is one pair");
    check(data.set_velocity.velocity.x == 0.f && data.set_velocity.velocity.y == 0.f &&
              data.set_velocity.velocity.z == 400.f,
          "and all three components arrive");
  }

  {
    entities::action_data_t data;
    data.tag = entities::entity_action::Teleport;

    // A space-containing value FOLLOWED by another pair: the value has to end
    // at the next field name rather than at the next space.
    const std::vector<std::string> refusals =
        server::parse_action_parameters(data, "destination=7 keep_velocity=true");
    check(refusals.empty(), "two pairs on one line");
    check(data.teleport.destination == 7, "the first keeps its value");
    check(data.teleport.keep_velocity, "and the second is not swallowed by it");
  }

  {
    entities::action_data_t data;
    data.tag                     = entities::entity_action::Set_Health;
    data.set_health.amount       = 55;

    const std::vector<std::string> refusals = server::parse_action_parameters(data, "");
    check(refusals.empty(), "an empty tail refuses nothing");
    check(data.set_health.amount == 55, "and writes nothing -- an unnamed field keeps what it had");
  }

  {
    entities::action_data_t data;
    data.tag = entities::entity_action::Set_Health;

    // Not silently ignored: it is text the author typed that nothing reads.
    const std::vector<std::string> refusals =
        server::parse_action_parameters(data, "100 amount=25");
    check(refusals.size() == 1, "a bare token before the first pair is reported");
    check(data.set_health.amount == 25, "and the pair after it still lands");
  }

  {
    entities::action_data_t data;
    data.tag = entities::entity_action::Set_Health;

    const std::vector<std::string> refusals =
        server::parse_action_parameters(data, "amount=not_a_number");
    check(refusals.size() == 1, "a value the field's own parser refuses is reported");
    check(data.set_health.amount == entities::Set_Health_Data{}.amount,
          "and the field keeps its default rather than half a value");
  }

  {
    entities::action_data_t data;
    data.tag = entities::entity_action::Set_Health;

    // `health` is not one of Set_Health's fields (the field is `amount`), so
    // this is not a pair boundary at all -- the whole thing is stray text.
    const std::vector<std::string> refusals = server::parse_action_parameters(data, "health=10");
    check(refusals.size() == 1, "an identifier the action does not declare is not a boundary");
  }
}

int main()
{
  std::printf("--- entity I/O: connections, the load check and the queue ---\n");

  test_a_connection_survives_the_file();
  test_the_load_check_refuses_what_cannot_run();
  test_build_session_drops_what_the_check_named();
  test_deleting_an_entity_drops_the_rows_naming_it();
  test_a_connection_is_queued_and_then_delivered();
  test_a_delay_is_counted_in_ticks();
  test_emit_order_breaks_a_tie_within_one_tick();
  test_fire_once_spends_the_sessions_copy();
  test_a_target_that_dies_during_the_delay_is_dropped();
  test_a_zero_delay_chain_settles_in_one_drain();
  test_a_wiring_loop_is_capped_and_dropped();
  test_the_trigger_system_emits_edges();
  test_a_disabled_trigger_releases_whoever_is_inside();
  test_a_platform_crumbles_under_one_and_holds_under_two();
  test_a_launcher_fires_its_weapon_row_along_its_aim();
  test_a_launchers_spread_is_the_same_pattern_every_attempt();
  test_the_toucher_is_the_activator();
  test_an_activator_that_does_not_accept_is_a_logged_miss();
  test_a_damageable_emits_died_and_health_changed();
  test_a_switched_light_leaves_the_frame();
  test_the_console_parses_a_parameter_tail();

  if (failure_count > 0)
  {
    std::printf("\n%d check(s) FAILED\n", failure_count);
    return 1;
  }

  std::printf("\nall checks passed\n");
  return 0;
}
