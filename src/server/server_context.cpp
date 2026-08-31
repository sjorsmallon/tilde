#include "server_context.hpp"

#include "../shared/log.hpp"
#include "../shared/network/cvar_mirror.hpp"
#include "systems/game_rules_system.hpp"

// The one place that answers "what resets when, and why". Each group's presence
// or absence below carries its reason on the line that does it; if a group ever
// needs to be half-cleared, that is the signal its boundary is drawn wrong, not
// a reason to open-code a field list at the call site again.
//
// The one standing exception is client_slot_t, which is genuinely two scopes in
// one struct — see reset_state_in_preparation_for_new_map_load.

namespace server
{

void reset_state_in_preparation_for_new_map_load(server_context_t& context)
{
  // The outgoing map's own settings, back to their defaults before the incoming
  // map's list runs. A map's cvars are the MAP's for as long as it is loaded --
  // without this, scoutzknivez's gravity follows you to the next map, and the
  // @Mirrored half follows every connected client there too.
  //
  // Read BEFORE the wipe below, which owns the list. Only the cvars that map
  // actually set are touched; everything else in cvar_state_t still lives for
  // the process, which is why this is a named subset rather than a group reset.
  if (context.cvars != nullptr)
    shared::revert_cvars_to_defaults(*context.cvars, context.world.cvars_applied_by_map);

  // Session, the retained map, bots, trigger overlaps, pending deaths.
  //
  // This NULLS world.physics rather than rebuilding it: jolt_init() must have
  // run before a physics_state_t exists, and g_server_context is a file-scope
  // object, so a world that made its own could not be constructed at static
  // init. The map load calls make_physics_state() on the next line; keeping the
  // construction there is also what lets server_context_test assert the whole
  // reset without standing Jolt up.
  context.world = {};
  context.replication = {};
  clear_incoming(context);
  clear_outgoing(context);


  reset_game_rules(context, context.tick_number,
                   static_cast<uint32_t>(context.cvars->sv_tickrate));

  // Only the map-scoped COLUMNS of each slot. Not the whole entry: the command
  // stream describes the client's input, which survives a map change, and
  // wiping it would make the next move look like a fresh rising edge on every
  // held button.
  for (client_slot_t& client : context.clients)
  {
    client.player_uid         = shared::null_entity_uid;
    client.held_snapshot_tick = 0;
    // The client is still running the OLD map for at least a round trip. It
    // reports the new one on an input once it has loaded it, and this comes
    // back on its own -- so this line is the correct answer right now rather
    // than a flag someone has to remember to clear later.
    client.map_ready          = false;
  }
}

void reset_client_slot(server_context_t& context, int32_t slot)
{
  if (!is_valid_client_slot(slot))
  {
    log_error("reset_client_slot: slot {} is out of range", slot);
    return;
  }

  // The WHOLE entry, and called from both edges of the lifetime. Leave used to
  // clear the uid alone and let the next join re-clear the rest, which held only
  // because the two sites happened to agree.
  context.clients[slot] = {};

  // The reliable stream is the one piece of this client's state that lives a
  // stratum down, in the transport layer, and it MUST go with the rest: the next
  // occupant would otherwise inherit a block number and a half-reassembled
  // inbound buffer from its predecessor. release_client_slot and
  // occupy_client_slot clear it too -- both edges are already paired with one of
  // them -- but this is what makes the guarantee unconditional rather than a
  // fact about how the two call sites happen to be written.
  context.transport_layer.reliable_streams[slot] = {};
}

// Both tick functions clear() per member rather than assigning `= {}` to the
// group. They run at the tickrate, and `= {}` would free and re-grow every
// vector's capacity every tick — the inbox reallocation this retained-on-the-
// context arrangement exists to stop. That makes them the one deliberate
// exception to "a group resets as `= {}`".

void clear_incoming(server_context_t& context)
{
  context.incoming.inputs.clear();
  context.incoming.potential_joins.clear();
  context.incoming.net_commands.clear();
  context.incoming.commands.clear();
  context.incoming.map_data_requests.clear();
}

// The client's clear_client_inbox carries the reasoning; this is the same
// tripwire on the same silent failure, one connection over. An inbox member
// nothing clears replays last tick's traffic every tick.
static_assert(sizeof(network::ServerInbox) == 5 * sizeof(std::vector<int>),
              "ServerInbox gained or lost a member. If you added one: clear it "
              "above AND drain it in server_impl.cpp's Tick(), then update this "
              "count");

void clear_outgoing(server_context_t& context)
{
  // reset(), not clear(): a stream re-reserves the 16 bits its count is
  // backpatched into, so it comes out of this ready to be fired into. Keeps the
  // buffer's capacity for the same reason the vectors above do.
  context.outgoing.effects.reset();
  context.outgoing.events.reset();
  // clear(), not `= {}`, for the same capacity reason as the inbox vectors: this
  // is refilled from scratch at the tickrate.
  context.outgoing.pending_hits.clear();

  // sv_event_debug, latched once per tick rather than read per fire. This is
  // the one place guaranteed to run exactly once before anything can fire, and
  // it keeps the generated fire helpers free of the cvar family.
  const bool log_fired = context.cvars != nullptr && context.cvars->sv_event_debug;
  context.outgoing.effects.log_fired = log_fired;
  context.outgoing.events.log_fired  = log_fired;
}

} // namespace server
