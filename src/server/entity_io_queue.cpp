#include "entity_io_queue.hpp"

#include "../shared/log.hpp"
#include "../shared/reflection.hpp"
#include "server_context.hpp"

#include <algorithm>
#include <cstring>
#include <format>

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

bool io_debug_is_on(const server_context_t& context)
{
  return context.cvars != nullptr && context.cvars->sv_io_debug;
}

// What a row's parameters actually are once the override rule has been applied,
// rendered through the action's own field table -- the same field_to_text the
// map writer uses, so a logged value and a saved one read identically.
std::string describe_action_payload(const entities::action_data_t& data)
{
  std::string text;
  for (const field_info_t& field : entities::action_payload_fields(data.tag))
  {
    std::string value;
    if (!field_to_text(entities::action_payload_bytes(data) + field.offset, field, value))
      value = "?";

    if (!text.empty())
      text += ", ";
    text += std::format("{}={}", field.name, value);
  }
  return text;
}

} // namespace

std::string entity_io_label(const server_context_t& context, shared::entity_uid_t uid)
{
  if (uid == shared::null_entity_uid)
    return "nobody";

  const entities::Entity* entity =
      const_cast<server_context_t&>(context).world.session.entity_system.try_find(uid);
  if (entity == nullptr)
    return std::format("uid {} (gone)", uid);

  const std::string_view label(entity->name.data, entity->name.length);
  if (label.empty())
    return std::format("{} uid {}", entities::entity_info(entity->type).classname, uid);

  return std::format("\"{}\" ({} uid {})", label, entities::entity_info(entity->type).classname,
                     uid);
}

void queue_signal_connections(input_context_t& context, const entities::Entity& sender,
                              entities::entity_signal signal, const void* payload_bytes,
                              uint32_t payload_size)
{
  shared::game_session_t& session = context.server.world.session;
  const bool              debug   = io_debug_is_on(context.server);

  auto bucket = session.connections_by_sender.find(sender.entity_id);
  if (bucket == session.connections_by_sender.end())
  {
    // THE line this cvar exists for. "I walked into the trigger and nothing
    // happened" has three causes and they are indistinguishable from the
    // viewport; this one separates "the signal never fired" from "it fired and
    // nothing was wired to it".
    if (debug)
      log_terminal("[io] {} emitted {} — no connections from this sender",
                   entity_io_label(context.server, sender.entity_id),
                   entities::to_string(signal));
    return;
  }

  uint32_t matched_count = 0;

  for (shared::session_connection_t& connection : bucket->second)
  {
    if (connection.row.signal != signal)
      continue;

    ++matched_count;

    if (connection.spent)
    {
      // Distinguishable from "no row matched", because it is a different fix:
      // the wiring is right and has already had its one turn.
      if (debug)
        log_terminal("[io] {} emitted {} — a matching row is spent (fire_once)",
                     entity_io_label(context.server, sender.entity_id),
                     entities::to_string(signal));
      continue;
    }

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

    if (debug)
    {
      const std::string parameters = describe_action_payload(record.data);
      log_terminal("[io] {} {} -> {} {}{}{}{}", entity_io_label(context.server, sender.entity_id),
                   entities::to_string(signal), entity_io_label(context.server, record.target),
                   entities::to_string(record.data.tag),
                   parameters.empty() ? "" : std::format("({})", parameters),
                   record.fire_tick == context.tick
                       ? std::string(" [this tick]")
                       : std::format(" [tick {}, +{}]", record.fire_tick,
                                     record.fire_tick - context.tick),
                   connection.row.fire_once ? " [fire_once, now spent]" : "");
    }

    if (connection.row.fire_once)
      connection.spent = true;
  }

  // Separated from the no-bucket case above: this sender HAS wiring, just none
  // for this signal. The fix is a different one -- the row names the wrong
  // signal rather than being absent.
  if (debug && matched_count == 0)
    log_terminal("[io] {} emitted {} — this sender has connections, but none for that signal",
                 entity_io_label(context.server, sender.entity_id), entities::to_string(signal));
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

    if (io_debug_is_on(context))
    {
      const std::string parameters = describe_action_payload(record.data);
      // Each optional clause carries its OWN leading space and the line has
      // none of its own: a separator split between the format string and the
      // clause reads fine while both clauses are present and doubles up the
      // moment neither is, which is the common case.
      log_terminal("[io] dispatch {}{}{} to {}, activator {}",
                   entities::to_string(record.data.tag),
                   parameters.empty() ? "" : std::format(" ({})", parameters),
                   record.target_resolved_from_activator ? " [from !activator]" : "",
                   entity_io_label(context, record.target),
                   entity_io_label(context, record.activator));
    }

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
