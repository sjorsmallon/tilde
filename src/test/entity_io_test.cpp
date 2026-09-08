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

#include "server/entity_io_queue.hpp"
#include "server/server_api.hpp"
#include "server/server_context.hpp"

#include "shared/game_session.hpp"
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
    // THE ONE WORTH THE MOST. Touched is declared `by Player_Entity,
    // Physics_Body_Entity`, and neither is Colorable -- so this is refused
    // before anything can walk into the trigger, which is the whole point of
    // declaring `by` at all.
    wired_map_t wired = make_wired_map();
    shared::connection_t row = touched_enables_the_light(wired);
    row.target_kind          = shared::connection_target_t::Activator;
    row.data.tag             = entities::entity_action::Set_Color;
    row.has_override         = true;
    wired.map.connections.push_back(row);
    check(refused(wired.map, 0),
          "an activator target is refused when a type in the `by` set does not accept the action");
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

} // namespace

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

  if (failure_count > 0)
  {
    std::printf("\n%d check(s) FAILED\n", failure_count);
    return 1;
  }

  std::printf("\nall checks passed\n");
  return 0;
}
