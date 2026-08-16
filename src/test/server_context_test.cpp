// The reset seams — src/server/server_context.cpp.
//
// Every case asserts BOTH halves: what the reset cleared, and what it
// deliberately left alone. The second half is the load-bearing one. The bug this
// exists to catch has a shape (entity_storage_def.md §4): state cleared at one
// edge of a lifetime and not the other, or a group that quietly grew a member
// belonging to a different scope. Both show up as a survivor assert failing, not
// as a crash.
//
// No Jolt here. The test never constructs a physics_state_t — it only asserts
// the unique_ptr is nulled — so jolt_init() is not needed and the reset stays
// testable without standing a server up.

#include "server/server_context.hpp"

#include <cassert>
#include <cstdio>

using namespace server;

namespace
{

// A context with every reset-scoped group visibly dirty, so a reset that misses
// something leaves a value the asserts can name. `cvars` is filled because
// reset_state_in_preparation_for_new_map_load dereferences it (the rules deadline needs sv_tickrate).
void make_dirty(server_context_t& context, cvars::cvar_state_t& cvar_state)
{
  context.cvars    = &cvar_state;
  context.commands = nullptr;
  context.tick_number = 900;

  context.world.session.map_name      = "old_map";
  context.world.current_map_path      = "maps/old_map.source";
  context.world.map_content_hash      = 0xDEADBEEF;
  context.world.next_bot_slot         = BOT_SLOT_BASE + 3;
  context.world.bots.push_back(Bot_State{});
  context.world.bots.push_back(Bot_State{});
  context.world.previous_tick_overlapping_trigger_player_pairs.insert({7, 9});
  context.world.death_tick_by_player_uid[42] = 100;
  context.world.rules.round_number   = 5;
  context.world.rules.phase          = round_phase_t::Live;
  context.world.rules.phase_end_tick = 950;

  context.replication.snapshot_history.slot_for(900).tick = 900;

  context.incoming.commands.push_back({0, "noclip 1"});
  context.incoming.map_loaded_acks.push_back({1, {1, 2, 3}});
  // Both channels hold encoded bytes rather than values, so "dirty" is a fired
  // event rather than a pushed one.
  shared::Jump jump{};
  jump.attached_entity = 42;
  shared::fire_jump(context.outgoing.effects, jump);

  shared::Player_Died died{};
  died.victim_id   = 42;
  died.attacker_id = 7;
  shared::fire_player_died(context.outgoing.events, died);

  // A hit the move loop resolved but has not applied. Normally drained the
  // instant that loop closes; it is a group member so that a tick which died
  // halfway cannot replay it.
  pending_hit_t pending{};
  pending.info.victim_uid   = 42;
  pending.info.attacker_uid = 7;
  pending.info.amount       = 35.f;
  pending.impact_point      = {1.f, 2.f, 3.f};
  pending.region            = shared::hit_region_t::Head;
  context.outgoing.pending_hits.push_back(pending);

  for (int32_t slot = 0; slot < network::sv_max_client_count; ++slot)
  {
    client_slot_t& client = context.clients[slot];
    client.player_uid               = 1000 + slot;
    client.latest_processed_command = 70 + slot;
    client.latest_buttons_bitmap    = 0b1011;
    client.held_snapshot_tick       = 880 + slot;
    client.map_ready                = true;
    client.last_map_switch_send_tick = 890 + slot;
    client.last_rewind_warning_tick  = 870 + slot;
  }
}

// --- 1. A new map ------------------------------------------------------------

void test_reset_state_in_preparation_for_new_map_load()
{
  cvars::cvar_state_t cvar_state;
  server_context_t    context;
  make_dirty(context, cvar_state);

  reset_state_in_preparation_for_new_map_load(context);

  // Cleared: everything keyed to the map we left.
  assert(context.world.session.map_name.empty());
  assert(context.world.physics == nullptr);
  assert(context.world.current_map_path.empty());
  assert(context.world.map_content_hash == 0);
  assert(context.world.bots.empty());
  assert(context.world.next_bot_slot == BOT_SLOT_BASE);
  assert(context.world.previous_tick_overlapping_trigger_player_pairs.empty());
  assert(context.world.death_tick_by_player_uid.empty());

  // The ring, so the next snapshot to every client is a full update.
  assert(context.replication.snapshot_history.find(900) == nullptr);

  assert(context.incoming.commands.empty());
  assert(context.incoming.map_loaded_acks.empty());
  assert(context.outgoing.effects.empty());
  assert(context.outgoing.events.empty());
  // Damage resolved against the world we are leaving must not land in the one
  // we are entering — the victim uid may not even exist there.
  assert(context.outgoing.pending_hits.empty());

  // Rules restart the match — by a CALL, so this is not `= {}`: Warmup for
  // round 0 (the first Countdown takes it to 1) with a deadline computed off
  // the CURRENT tick, not off zero.
  assert(context.world.rules.phase == round_phase_t::Warmup);
  assert(context.world.rules.round_number == 0);
  assert(context.world.rules.phase_end_tick > context.tick_number);

  // Survives: the tick clock. Absolute stamps (phase_end_tick above,
  // last_fire_tick / death_tick on entities) and both snapshot rings are keyed
  // by it, so restarting it would collide a new frame with a retained one.
  assert(context.tick_number == 900);

  for (int32_t slot = 0; slot < network::sv_max_client_count; ++slot)
  {
    const client_slot_t& client = context.clients[slot];

    // Cleared: the map-scoped columns. A retained uid can be REISSUED by the
    // fresh session's restarted counter and resolve to someone else's player.
    assert(client.player_uid == shared::null_entity_uid);
    assert(client.held_snapshot_tick == 0);
    assert(!client.map_ready);

    // Survives: the command stream describes the CLIENT, not the world. Wiping
    // latest_buttons_bitmap would make every held button look like a fresh
    // rising edge on the first tick after the switch.
    assert(client.latest_processed_command == 70 + slot);
    assert(client.latest_buttons_bitmap == 0b1011);

    // Survives, but only because clearing it would be dead: change_map_to sends
    // a CmdChangeMap to every OCCUPIED slot right after this reset, and that
    // send stamps the field. An unoccupied slot is never in the retransmit loop.
    assert(client.last_map_switch_send_tick == static_cast<uint32_t>(890 + slot));

    // Survives for the same reason latest_buttons_bitmap does: it describes the
    // CLIENT's connection quality, which a map change does not improve. Wiping
    // it would let one warning through per map load, per slot, for free.
    assert(client.last_rewind_warning_tick == static_cast<uint32_t>(870 + slot));
  }

  printf("  reset_state_in_preparation_for_new_map_load: ok\n");
}

// --- 2. A slot changes occupant ----------------------------------------------

void test_reset_client_slot()
{
  cvars::cvar_state_t cvar_state;
  server_context_t    context;
  make_dirty(context, cvar_state);

  constexpr int32_t target_slot = 1;
  reset_client_slot(context, target_slot);

  // The WHOLE entry, unlike the map reset above: a new occupant inherits none
  // of the previous one's command cursor, ack or map-ready gate.
  const client_slot_t& reset_client = context.clients[target_slot];
  assert(reset_client.player_uid == shared::null_entity_uid);
  assert(reset_client.latest_processed_command == invalid_slot_idx);
  assert(reset_client.latest_buttons_bitmap == 0);
  assert(reset_client.held_snapshot_tick == 0);
  assert(!reset_client.map_ready);
  assert(reset_client.last_map_switch_send_tick == 0);
  assert(reset_client.last_rewind_warning_tick == 0);

  // Every other slot, and the world, are none of this reset's business.
  for (int32_t slot = 0; slot < network::sv_max_client_count; ++slot)
  {
    if (slot == target_slot) continue;
    assert(context.clients[slot].player_uid == static_cast<shared::entity_uid_t>(1000 + slot));
    assert(context.clients[slot].map_ready);
  }
  assert(context.world.bots.size() == 2);
  assert(context.world.session.map_name == "old_map");
  assert(context.tick_number == 900);

  // Out of range is logged and ignored, not indexed.
  reset_client_slot(context, -1);
  reset_client_slot(context, network::sv_max_client_count);
  assert(context.clients[0].player_uid == 1000);

  printf("  reset_client_slot: ok\n");
}

// --- 3. The two tick groups --------------------------------------------------

void test_clear_tick_groups()
{
  cvars::cvar_state_t cvar_state;
  server_context_t    context;
  make_dirty(context, cvar_state);

  clear_incoming(context);
  clear_outgoing(context);

  assert(context.incoming.moves.empty());
  assert(context.incoming.potential_joins.empty());
  assert(context.incoming.net_commands.empty());
  assert(context.incoming.commands.empty());
  assert(context.incoming.map_loaded_acks.empty());
  assert(context.incoming.map_data_requests.empty());
  assert(context.outgoing.effects.empty());
  assert(context.outgoing.events.empty());
  assert(context.outgoing.pending_hits.empty());

  // The reason these are functions and not `= {}` on the group: they run at the
  // tickrate, so the capacity has to survive. An `= {}` "simplification" fails
  // here rather than silently reintroducing a per-tick realloc.
  assert(context.incoming.commands.capacity() > 0);
  assert(context.incoming.map_loaded_acks.capacity() > 0);
  assert(context.outgoing.pending_hits.capacity() > 0);
  // Same intent, different member: a stream keeps its buffer's allocation.
  assert(context.outgoing.effects.writer.buffer.capacity() > 0);
  assert(context.outgoing.events.writer.buffer.capacity() > 0);

  // Neither touches the world or the slots.
  assert(context.world.bots.size() == 2);
  assert(context.clients[0].map_ready);

  printf("  clear_incoming / clear_outgoing: ok\n");
}

} // namespace

int main()
{
  printf("server_context_test\n");
  test_reset_state_in_preparation_for_new_map_load();
  test_reset_client_slot();
  test_clear_tick_groups();
  printf("server_context_test: all passed\n");
  return 0;
}
