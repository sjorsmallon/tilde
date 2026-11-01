#include "server_send.hpp"

#include "../shared/network/bitstream.hpp"
#include "../shared/network/cvar_mirror.hpp"
#include "../shared/network/entity_snapshot.hpp"
#include "../shared/network/generated/messages_generated.hpp"
#include "../shared/network/reliable_stream.hpp"
#include "../shared/network/server_transport_layer.hpp"
#include "../shared/replay/replay_recorder.hpp"
#include "log.hpp"
#include "server_context.hpp"
#include "server_messages.hpp"

#include <algorithm>
#include <vector>

namespace server
{

// Bots have no client, so nothing else on the wire says where one thinks it is
// going; this is the only way to see a path in the viewport. On each ready
// client's own scope, never the broadcast: a long path is a record the cut
// would drop every tick.
static void write_bot_debug(server_context_t &context)
{
  if (context.world.bots.empty())
    return;

  std::vector<shared::Bot_Debug> rows(1);
  for (const Bot_State &bot : context.world.bots)
  {
    shared::Bot_Debug_Entry entry;
    entry.slot     = bot.player_slot;
    entry.goal     = bot.goal;
    entry.behavior = bot.type;
    for (size_t index = static_cast<size_t>(std::max(bot.path_index, 0)); index < bot.path.size(); ++index)
      if (!entry.path.try_push_back(bot.path[index]))
        break;

    if (!rows.back().bots.try_push_back(entry))
    {
      rows.emplace_back();
      (void)rows.back().bots.try_push_back(entry);
    }
  }

  for (connected_client_t row : connected_clients(context))
  {
    if (!row.client.map_ready)
      continue;
    for (const shared::Bot_Debug &message : rows)
      shared::write_bot_debug(context.outgoing.datagram[row.slot], message);
  }
}

// The changed @Mirrored subset, once, on the reliable broadcast. Retained the
// moment it is written: the stream delivers what it holds.
static void broadcast_changed_cvar_values(server_context_t &context)
{
  const std::vector<shared::Cvar_Value> changed =
      shared::collect_changed_mirrored_cvars(*context.cvars, context.last_broadcast_cvars);
  if (changed.empty())
    return;

  shared::write_cvar_value_messages(context.outgoing.tick_broadcast_reliable, changed);
  context.last_broadcast_cvars = *context.cvars;

  for (const shared::Cvar_Value &value : changed)
    log_terminal("Mirroring '{}' = {} to connected clients",
                 cvars::cvar_info(static_cast<cvars::cvar_id>(value.id)).name, value.text.c_str());
}

// One Message datagram per connected client: the slot's own records first, then
// the broadcast, and the broadcast only when the client holds the map. Nothing
// else is gated. An over-long datagram is cut at a record boundary, broadcast
// tail first; the slot's own records are never dropped, so they fragment.
static void send_message_datagrams(server_context_t &context)
{
  context.outgoing.tick_broadcast.finish();

  std::vector<network::uint8> payload;
  bool                        logged_cut = false;
  for (connected_client_t row : connected_clients(context))
  {
    shared::message_stream_t &own = context.outgoing.datagram[row.slot];
    own.finish();

    shared::begin_server_message_payload(payload, context.tick_number);
    if (!own.empty())
      shared::append_message_stream(payload, own);

    if (row.client.map_ready && !context.outgoing.tick_broadcast.empty())
    {
      const size_t fragments = (payload.size() + network::MAX_PAYLOAD_SIZE_IN_BYTES - 1) / network::MAX_PAYLOAD_SIZE_IN_BYTES;
      const size_t budget    = std::max<size_t>(fragments, 1) * network::MAX_PAYLOAD_SIZE_IN_BYTES - payload.size();
      const uint16_t dropped = shared::append_message_records_that_fit(payload, context.outgoing.tick_broadcast, budget);
      if (dropped != 0 && !logged_cut)
      {
        log_warning("tick {}: the broadcast overran slot {}'s datagram; its last {} of {} records were dropped",
                    context.tick_number, row.slot, dropped, context.outgoing.tick_broadcast.count);
        logged_cut = true;
      }
    }

    if (payload.size() == sizeof(uint32_t))
      continue;

    for (const network::Packet &packet :
         network::convert_to_packets(payload, network::Payload_Kind::Message,
                                     context.transport_layer.next_message_id))
      network::send_packet_to_client(context.transport_layer, context.socket, row.slot, packet);
  }
}

// A finished reliable scope as one Message record on a slot's stream.
static void queue_reliable_records(server_context_t &context, int32_t slot,
                                   const shared::message_stream_t &records)
{
  std::vector<network::uint8> payload;
  shared::begin_server_message_payload(payload, context.tick_number);
  shared::append_message_stream(payload, records);
  network::queue_reliable_message(context.transport_layer.clients[slot].reliable_stream,
                                  static_cast<network::uint8>(network::Payload_Kind::Message), payload);
}

// Each client's own reliable records first, then the tick's reliable broadcast,
// written once and appended to every client's stream: the same order a
// datagram keeps.
static void queue_reliable_messages(server_context_t &context)
{
  shared::message_stream_t &broadcast = context.outgoing.tick_broadcast_reliable.records;
  broadcast.finish();

  for (connected_client_t row : connected_clients(context))
  {
    shared::message_stream_t &own = context.outgoing.reliable[row.slot].records;
    own.finish();
    if (!own.empty())
      queue_reliable_records(context, row.slot, own);
    if (!broadcast.empty())
      queue_reliable_records(context, row.slot, broadcast);
  }
}

static void send_reliable_blocks_if_there_are_any(server_context_t &context)
{
  for (connected_client_t row : connected_clients(context))
  {
    network::Reliable_Stream& stream = row.transport.reliable_stream;

    if (network::reliable_outbound_has_overflowed(stream))
    {
      log_error("slot {} has {} bytes of unconfirmed reliable data (cap {}); it "
                "has stopped acking while we kept queueing",
                row.slot, network::reliable_pending_bytes(stream),
                network::RELIABLE_OUTBOUND_CAP_IN_BYTES);
      disconnect_client(context, row.slot, "overflowed its reliable stream.");
      continue;
    }

    if (context.cvars->sv_reliable_debug && stream.block_length == 0 &&
        network::reliable_pending_bytes(stream) != 0)
    {
      network::visit_pending_reliable_records(
          stream, [&](network::uint8 message_type, network::uint32 length,
                      size_t offset) {
            log_terminal("[reliable] slot {}: record type {} at +{} ({} bytes)",
                         row.slot, static_cast<int>(message_type), offset, length);
          });
    }

    network::send_reliable_block(context.transport_layer, context.socket, row.slot);
  }
}

void send_to_clients(server_context_t &context)
{
  network::snapshot_frame_t& frame = context.replication.snapshot_history.slot_for(context.tick_number);
  frame.clear();
  frame.tick = context.tick_number;

  frame.copy_replicated_entities_from(context.world.session.entity_system);

  // Each ready client's snapshot on its own datagram scope, ahead of the
  // broadcast: a delta against the tick it says it holds, or a full update when
  // the history no longer has that tick.
  for (connected_client_t row : connected_clients(context))
  {
    if (!row.client.map_ready) continue;

    const network::snapshot_frame_t* baseline =
        context.replication.snapshot_history.find(row.client.held_snapshot_tick);
    shared::write_entity_package(context.outgoing.datagram[row.slot],
                                 network::entity_package_from_snapshot(context.outgoing.snapshot_writer, frame,
                                                                       baseline,
                                                                       row.client.latest_processed_input_number));
  }

  write_bot_debug(context);
  broadcast_changed_cvar_values(context);

  send_message_datagrams(context);
  queue_reliable_messages(context);

  const shared::message_stream_t &broadcast          = context.outgoing.tick_broadcast;
  const shared::message_stream_t &broadcast_reliable = context.outgoing.tick_broadcast_reliable.records;
  shared::record_replay_tick(context.world.replay_recorder, frame,
                             broadcast.empty() ? Span<const uint8_t>{} : Span<const uint8_t>(broadcast.writer.buffer),
                             broadcast_reliable.empty() ? Span<const uint8_t>{}
                                                        : Span<const uint8_t>(broadcast_reliable.writer.buffer),
                             *context.cvars);

  // in theory redundant but just so we don't have stale shit to send.
  clear_outgoing(context);

  // if we had reliable data to send, do so.
  send_reliable_blocks_if_there_are_any(context);
}

} // namespace server
