#include "entities/generated/entities_tables_generated.hpp"
#include "server_receive.hpp"

#include "../shared/network/bitstream.hpp"
#include "../shared/network/ghost_transfer.hpp"
#include "../shared/network/packet.hpp"
#include "../shared/network/server_transport_layer.hpp"
#include "log.hpp"
#include "message_handlers.hpp"
#include "move_budget.hpp"
#include "server_context.hpp"
#include "server_messages.hpp"

#include <algorithm>
#include <vector>

namespace server
{

static void drop_timed_out_clients(server_context_t &context)
{
  const float timeout_seconds = context.cvars->sv_timeout;

  // there's no timeout.
  if (timeout_seconds <= 0.0f) return;

  const uint32_t timeout_in_ticks =
      shared::ticks_from_seconds(timeout_seconds, context.cvars->sv_tickrate);

  for (connected_client_t row : connected_clients(context))
  {
    const uint32_t silent_ticks =
        context.tick_number - row.transport.latest_packet_tick;
    if (silent_ticks < timeout_in_ticks)
      continue;

    log_warning("Slot {} ({}) has been silent for {:.1f}s (sv_timeout {:.1f}s)",
                row.slot, row.transport.address.to_string(),
                static_cast<float>(silent_ticks) * shared::tick_interval_seconds(context.cvars->sv_tickrate),
                timeout_seconds);
    disconnect_client(context, row.slot, "timed out.");
  }
}

// A peer with no slot may say exactly one thing, a Connect on its own: the
// first record's tag, read where the dispatch would read it.
static bool unslotted_payload_is_one_connect(Span<const network::uint8> payload)
{
  network::Bit_Reader reader(payload.data, payload.count);
  if (reader.bits_remaining() < (size_t)(shared::MESSAGE_STREAM_COUNT_BITS + 16))
    return false;
  const uint32_t count = reader.read_bits(shared::MESSAGE_STREAM_COUNT_BITS);
  const uint32_t kind  = reader.read_bits(16);
  return count == 1 && kind == (uint32_t)shared::Message_Type::Connect;
}

// Every Message payload as it arrives, datagram and reliable record alike, in
// arrival order, through the one generated dispatch.
static void receive_messages(server_context_t &context)
{
  const shared::message_log_filter_t filter =
      shared::message_log_filter_from_text(context.cvars->sv_event_debug.c_str());

  network::poll_network(
      context.transport_layer, context.socket, network::server_receive_drain_cap_in_datagrams,
      context.tick_number,
      [&context, filter](int32_t slot, const network::Address &sender, Span<const network::uint8> payload) {
        if (slot < 0 && !unslotted_payload_is_one_connect(payload))
        {
          log_warning("{} holds no slot and sent something other than a lone Connect; dropped",
                      sender.to_string());
          return;
        }

        context.incoming.unslotted_sender = sender;
        network::Bit_Reader reader(payload.data, payload.count);
        dispatch_received_messages(context, slot, reader, filter);
      });
}

// The ghost, in two passes that are STATE compared with state (coop_ghost_plan.md §2F): what a
// client asked for (on_ghost_request held it) and may be sent now, and what it has been told.
static void service_ghost_transfers(server_context_t &context)
{
  const shared::ghost_announcement_t &announced = context.world.announced_ghost;

  for (connected_client_t row : connected_clients(context))
  {
    const uint32_t requested = row.client.requested_ghost_hash;
    if (requested == 0)
      continue;

    if (requested != announced.hash)
    {
      log_terminal("slot {} asked for ghost {:#x}, which is no longer the announced one ({:#x}); "
                   "the newer announce is already on its stream",
                   row.slot, requested, announced.hash);
      row.client.requested_ghost_hash = 0;
      continue;
    }

    // Held, never restarted over: begin_paced_transfer REPLACES what the slot is sending, and that is the map.
    if (row.transport.outbound_transfer.in_progress())
      continue;

    shared::ghost_data_message_t message;
    message.party_size = announced.party_size;
    message.ghost_hash = announced.hash;
    message.bytes      = announced.bytes;

    network::Bit_Writer writer;
    shared::serialize_ghost_data(writer, message);
    network::begin_paced_transfer(context.transport_layer, row.slot, writer.buffer,
                                  network::Payload_Kind::Ghost_Transfer);
    row.client.requested_ghost_hash = 0;

    log_terminal("Queued {}-player ghost ({} bytes, hash {:#x}) for slot {}", announced.party_size,
                 announced.bytes.size(), announced.hash, row.slot);
  }

  // Not before map_ready: a client mid-load clears its ghost when the load finishes.
  for (connected_client_t row : connected_clients(context))
  {
    if (!row.client.map_ready || row.client.ghost_hash_told_to_this_client == announced.hash)
      continue;

    shared::Ghost_Available message;
    message.party_size = announced.party_size;
    message.ghost_hash = announced.hash;
    message.byte_count = static_cast<uint32_t>(announced.bytes.size());
    shared::write_ghost_available(context.outgoing.reliable[row.slot], message);
    row.client.ghost_hash_told_to_this_client = announced.hash;
  }
}

// Everything on a Client_Input that is NOT input: the snapshot the client
// says it holds, the map it says it holds, and this tick's move credit.
static void read_riders_off_client_inputs(server_context_t &context)
{
  tick_incoming_t& inbox = context.incoming;

  // Ordered by (slot, input_number) so a slot's inputs run oldest first and
  // the last of each slot's run is its newest.
  std::sort(inbox.client_inputs.begin(), inbox.client_inputs.end(),
            [](const received_client_input_t& a, const received_client_input_t& b)
            {
              if (a.slot != b.slot)
                return a.slot < b.slot;
              return a.input.input_number < b.input.input_number;
            });

  for (size_t index = 0; index < inbox.client_inputs.size(); ++index)
  {
    const int32_t               client_slot = inbox.client_inputs[index].slot;
    const shared::Client_Input& input       = inbox.client_inputs[index].input;
    if (!is_valid_client_slot(client_slot))
      continue; // the input loop below logs it; one complaint per input is enough

    client_slot_t& client = context.clients[client_slot];
    client.held_snapshot_tick = std::max(client.held_snapshot_tick, input.held_snapshot_tick);

    const bool this_is_the_slots_newest_input =
        index + 1 == inbox.client_inputs.size() ||
        inbox.client_inputs[index + 1].slot != client_slot;

    if (!this_is_the_slots_newest_input) continue;

    const bool map_ready_now =
        input.map_content_hash == context.world.map_content_hash;

    if (map_ready_now != client.map_ready)
      log_terminal("Slot {} {} map '{}' (hash {:#x}); {} snapshots.", client_slot,
                   map_ready_now ? "now holds" : "no longer holds",
                   context.world.session.map_name, context.world.map_content_hash,
                   map_ready_now ? "resuming" : "withholding");

    client.map_ready = map_ready_now;
  }

  // gate the amouint of moves that clients can execute in a single tick.
  // this is just for sanity.
  for (connected_client_t row : connected_clients(context))
    row.client.move_credits = grant_move_credit(
        row.client.move_credits, context.cvars->sv_max_move_backlog);
}

void receive_from_clients(server_context_t &context)
{
  clear_incoming(context);
  receive_messages(context);

  // if we lost someone, no use processing them.
  drop_timed_out_clients(context);

  service_ghost_transfers(context);

  // send a block of map fragments so not to swamp the connection.
  network::service_paced_transfers(
      context.transport_layer, context.socket,
      static_cast<size_t>(std::max(1, context.cvars->sv_map_transfer_fragments_per_tick)));

  read_riders_off_client_inputs(context);
}

} // namespace server
