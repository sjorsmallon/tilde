#include "entity_io_queue.hpp"

#include "../shared/log.hpp"
#include "server_context.hpp"

#include <algorithm>
#include <cstring>

namespace server
{

namespace
{

// A delay in seconds as a whole number of ticks, rounded UP: a connection that
// asks for any delay at all must not fire in the tick that emitted it, which
// rounding down would let it do for anything under half a tick.
uint32_t ticks_for_delay(const server_context_t& context, float delay_seconds)
{
  if (delay_seconds <= 0.0f)
    return 0;

  const float tickrate = std::max(1.0f, (float)context.cvars->sv_tickrate);
  return (uint32_t)std::ceil(delay_seconds * tickrate);
}

} // namespace

void queue_signal_connections(input_context_t& context, const entities::Entity& sender,
                              entities::entity_signal signal, const void* payload_bytes,
                              uint32_t payload_size)
{
  shared::game_session_t& session = context.server.world.session;

  auto bucket = session.connections_by_sender.find(sender.entity_id);
  if (bucket == session.connections_by_sender.end())
    return;

  for (shared::session_connection_t& connection : bucket->second)
  {
    if (connection.row.signal != signal || connection.spent)
      continue;

    pending_action_t record;
    record.data      = connection.row.data;
    record.activator = context.activator;
    record.fire_tick = context.tick + ticks_for_delay(context.server, connection.row.delay_seconds);
    record.sequence  = context.server.world.next_action_sequence++;

    // Resolved NOW, not at drain time. `!activator` names whoever caused this
    // signal, and a delayed record outlives that moment -- resolving late
    // would hand the action to whoever happens to be the activator then.
    switch (connection.row.target_kind)
    {
    case shared::connection_target_t::Uid: record.target = connection.row.target; break;
    case shared::connection_target_t::Self: record.target = sender.entity_id; break;
    case shared::connection_target_t::Activator:
      record.target                          = context.activator;
      record.target_resolved_from_activator  = true;
      break;
    }

    // The payload, from the override or from the signal. The load check has
    // already established that a pass-through row's two payloads have the same
    // field table and the same size, so the copy needs no conversion and can
    // have no partial case.
    if (!connection.row.has_override)
    {
      const uint32_t action_size = entities::action_payload_size(record.data.tag);
      if (action_size != payload_size)
        fatal_error("emit {}: its payload is {} bytes and {} takes {} — the load check should "
                    "have refused this connection",
                    entities::to_string(signal), payload_size,
                    entities::to_string(record.data.tag), action_size);
      std::memcpy(entities::action_payload_bytes(record.data), payload_bytes, payload_size);
    }

    context.server.world.pending_actions.push_back(record);

    if (connection.row.fire_once)
      connection.spent = true;
  }
}

void drain_pending_actions(server_context_t& context)
{
  std::vector<pending_action_t>& queue = context.world.pending_actions;
  if (queue.empty())
    return;

  // A queue holding only delayed records is the common non-empty case, and it
  // runs every tick until they come due -- so it has to cost no allocation.
  bool anything_is_due = false;
  for (const pending_action_t& record : queue)
    anything_is_due = anything_is_due || record.fire_tick <= context.tick_number;
  if (!anything_is_due)
    return;

  // Whatever is due, in emit order. A handler can emit again -- a signal fired
  // from a system it reaches -- so the due records are MOVED OUT before any of
  // them runs: anything queued during the drain belongs to the next tick,
  // which is what makes "a queue that emits feeds itself" not a possibility.
  std::vector<pending_action_t> due;
  std::vector<pending_action_t> later;
  for (const pending_action_t& record : queue)
    (record.fire_tick <= context.tick_number ? due : later).push_back(record);

  queue = std::move(later);

  std::sort(due.begin(), due.end(),
            [](const pending_action_t& left, const pending_action_t& right)
            {
              if (left.fire_tick != right.fire_tick)
                return left.fire_tick < right.fire_tick;
              return left.sequence < right.sequence;
            });

  for (const pending_action_t& record : due)
  {
    entities::Entity* target = context.world.session.entity_system.try_find(record.target);
    if (target == nullptr)
    {
      // The one way a queued action can fail, and it is not the author's
      // fault: the demon died during the delay. Dropped with a line, never a
      // fatal.
      log_warning("entity I/O: {} for uid {} dropped — the target no longer exists",
                  entities::to_string(record.data.tag), record.target);
      continue;
    }

    input_context_t handler_context{context, record.activator, context.tick_number};

    // A Uid or Self target was checked against ONE type at load, so a null
    // dispatch cell there is a generator or loader bug and send_action's
    // fatal_error is right. An Activator was checked against a `by` list that
    // only had to contain SOME accepting type, so the entity that turned up may
    // legitimately not accept -- a crate rolling into a volume wired to kill
    // whoever touched it. That is a logged miss, and the log line is the only
    // way an author ever learns why nothing happened.
    if (!record.target_resolved_from_activator)
    {
      entities::send_action(*target, record.data, handler_context);
      continue;
    }

    if (!entities::try_send_action(*target, record.data, handler_context))
      log_warning("entity I/O: {} reached {} (uid {}), which does not accept it — the row targets "
                  "its activator and this one is not a receiver",
                  entities::to_string(record.data.tag),
                  entities::entity_info(target->type).classname, record.target);
  }
}

} // namespace server
