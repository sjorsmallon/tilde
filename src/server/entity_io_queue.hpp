#pragma once

#include "../shared/entities/generated/entity_io_generated.hpp"
#include "../shared/entity_uid.hpp"
#include "entity_io_context.hpp"

#include <cstdint>
#include <string>

// ============================================================================
// The queue every connection's action goes through, and the seam the generated
// emit_<signal> functions reach it by. entity_io_def.md ss7.
//
// EVERYTHING FROM A CONNECTION IS QUEUED, delay zero included, and everything
// from code is SYNCHRONOUS. That is Source's split and it is the reentrancy
// guard: an action reached through a connection can spawn or destroy, and it
// must not do so under the system that emitted the signal. Both reach the same
// handler.
// ============================================================================

namespace server
{

// An action that has been requested but not yet delivered.
struct pending_action_t
{
  uint32_t fire_tick = 0;

  // Emit order within a tick, from a counter on world_t. Two records queued in
  // one tick with the same delay must arrive in the order they were emitted,
  // and fire_tick alone cannot say which came first.
  uint32_t sequence = 0;

  shared::entity_uid_t    target = shared::null_entity_uid;
  entities::action_data_t data   = {};

  // Who caused the signal this came from, resolved at EMIT time rather than at
  // drain time: `!activator` means the player who walked into the trigger,
  // and by the time a delayed record fires they may have left. Carried through
  // so the handler still gets the answer -- which may by then name nobody, and
  // handlers tolerate that.
  shared::entity_uid_t activator = shared::null_entity_uid;

  // Whether `target` came from an Activator row, which is the ONE case the load
  // check could not settle exactly: it admits a row that SOME `by` type
  // accepts, so the entity that actually showed up may not. The drain dispatches
  // those through try_send_action and logs a miss; a Uid or Self target was
  // checked against one type and stays a fatal_error, because a null dispatch
  // cell there is a generator or loader bug.
  bool target_resolved_from_activator = false;
};

// The ONE walk behind every generated emit_<signal>: find the sender's
// connections for this signal, resolve each target, and push one pending
// record per row.
//
// `payload_bytes` is the signal's payload struct. A row with an override
// ignores it; a pass-through row copies it straight over the action's payload,
// which the load check has already established is legal.
//
// Hand-written and here rather than generated per signal, because the walk is
// identical for every signal and only the bytes differ.
void queue_signal_connections(input_context_t& context, const entities::Entity& sender,
                              entities::entity_signal signal, const void* payload_bytes,
                              uint32_t payload_size);

// Deliver every record whose tick has come, in (fire_tick, sequence) order.
// Runs at the TOP of a tick: a queue drained mid-tick would let one system see
// a world another system's signal had already changed underneath it.
void drain_pending_entity_actions(server_context_t& context);

// How sv_io_debug and ent_fire name one end of a connection: the author's label
// when the entity has one, its classname otherwise, and the uid always -- the
// uid is the identity a row actually stores, so a line that printed only a
// label would name something no connection can be edited by.
//
// A uid nothing resolves is not an error here; it is the ordinary case for an
// activator who has left, so it prints as such rather than refusing.
[[nodiscard]] std::string entity_io_label(const server_context_t& context,
                                          shared::entity_uid_t uid);

} // namespace server
