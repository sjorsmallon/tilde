#include "server_context.hpp"

#include "../shared/log.hpp"
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
    client.player_uid          = shared::null_entity_uid;
    client.acked_snapshot_tick = 0;
    client.map_ready           = false;
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
}

// Both tick functions clear() per member rather than assigning `= {}` to the
// group. They run at the tickrate, and `= {}` would free and re-grow every
// vector's capacity every tick — the inbox reallocation this retained-on-the-
// context arrangement exists to stop. That makes them the one deliberate
// exception to "a group resets as `= {}`".

void clear_incoming(server_context_t& context)
{
  context.incoming.moves.clear();
  context.incoming.potential_joins.clear();
  context.incoming.net_commands.clear();
  context.incoming.commands.clear();
  context.incoming.map_loaded_acks.clear();
  context.incoming.map_data_requests.clear();
}

void clear_outgoing(server_context_t& context)
{
  // reset(), not clear(): a stream re-reserves the 16 bits its count is
  // backpatched into, so it comes out of this ready to be fired into. Keeps the
  // buffer's capacity for the same reason the vectors above do.
  context.outgoing.effects.reset();
  context.outgoing.events.reset();

  // sv_event_debug, latched once per tick rather than read per fire. This is
  // the one place guaranteed to run exactly once before anything can fire, and
  // it keeps the generated fire helpers free of the cvar family.
  const bool log_fired = context.cvars != nullptr && context.cvars->sv_event_debug;
  context.outgoing.effects.log_fired = log_fired;
  context.outgoing.events.log_fired  = log_fired;
}

} // namespace server
