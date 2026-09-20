#include "server_receive.hpp"

#include "../shared/cvars/cvar_console.hpp"
#include "../shared/entities/generated/entities_generated.hpp"
#include "../shared/network/bitstream.hpp"
#include "../shared/network/cvar_mirror.hpp"
#include "../shared/network/ghost_transfer.hpp"
#include "../shared/network/map_transfer.hpp"
#include "../shared/network/packet.hpp"
#include "../shared/network/server_transport_layer.hpp"
#include "game_mode.hpp"
#include "log.hpp"
#include "move_budget.hpp"
#include "send_protobuf_message.hpp"
#include "server_context.hpp"
#include "server_messages.hpp"
#include "systems/game_rules_system.hpp"
#include "entity_lifecycle.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <vector>

namespace server
{

static void send_message_to_reject_incoming_connection(
  server_context_t &context,
  const network::Address& sender,
  std::string_view reason,
  uint32_t server_schema_hash)
{
  game::S2C_Connection reply;
  auto *reject = reply.mutable_reject();
  reject->set_reason(std::string(reason));
  reject->set_server_schema_hash(server_schema_hash);

  ::send_protobuf_message(context, sender, reply);
}

static void drop_timed_out_clients(server_context_t &context)
{
  const float timeout_seconds = context.cvars->sv_timeout;

  // there's no timeout.
  if (timeout_seconds <= 0.0f) return;

  const uint32_t timeout_in_ticks = std::max(
      1u, static_cast<uint32_t>(timeout_seconds * context.cvars->sv_tickrate));

  for (connected_client_t row : connected_clients(context))
  {
    const uint32_t silent_ticks =
        context.tick_number - row.transport.latest_packet_tick;
    if (silent_ticks < timeout_in_ticks)
      continue;

    log_warning("Slot {} ({}) has been silent for {:.1f}s (sv_timeout {:.1f}s)",
                row.slot, row.transport.address.to_string(),
                static_cast<float>(silent_ticks) / context.cvars->sv_tickrate,
                timeout_seconds);
    disconnect_client(context, row.slot, "timed out.");
  }
}

static network::pascal_string_t<32> sanitized_player_name(const std::string& requested_name, int32_t slot)
{
  auto filtered_name = std::string{};
  for (const char character : requested_name)
  {
    if (filtered_name.size() >= 32)
    {
      log_warning("slot {}: player name '{}' exceeds 32 characters — truncated",
                  slot, requested_name);
      break;
    }
    if (static_cast<unsigned char>(character) >= 0x20 &&
        static_cast<unsigned char>(character) != 0x7f)
      filtered_name.push_back(character);
  }

  if (filtered_name.empty())
    filtered_name = std::format("Player {}", slot);

  network::pascal_string_t<32> name;
  name.set(filtered_name.c_str());
  return name;
}

static void handle_connection_messages(server_context_t &context)
{
  for (const auto& [sender, cmd] : context.incoming.connection_messages)
  {
    if (cmd.has_connect())
    {
      // duplicate connect, no meaningful work to do.
      if (network::try_find_client_slot(context.transport_layer, sender))
      {
        log_warning("duplicate connect received from sender: <not sure if i should log ip addresses.>");
        continue;
      }

      // are we talking the same version of the game?
      const uint32_t client_schema_hash = cmd.connect().schema_hash();
      if (client_schema_hash != entities::SCHEMA_HASH)
      {
        log_error("Refusing connection from {}: schema hash mismatch "
                  "(client {:#010x}, server {:#010x}). Both sides must be "
                  "built from the same entities.def and asset set.",
                  cmd.connect().player_name(), client_schema_hash,
                  entities::SCHEMA_HASH);
        send_message_to_reject_incoming_connection(context, sender,
                    std::format("Schema mismatch: client {:#010x}, server "
                                "{:#010x} -- rebuild against the same "
                                "entities.def",
                                client_schema_hash, entities::SCHEMA_HASH),
                    entities::SCHEMA_HASH);
        continue;
      }

      // find a free slot. if there's a free slot, connect them.
      int32_t slot = invalid_slot_idx;
      for (int32_t candidate = 0; candidate < network::sv_max_client_count; ++candidate)
      {
        if (!context.transport_layer.clients[candidate].occupied)
        {
          slot = candidate;
          break;
        }
      }

      if (slot != invalid_slot_idx)
      {
        connect_client(context, slot, sender,
                       sanitized_player_name(cmd.connect().player_name(), slot));

        // actually handshake back to the client.
        {
          game::S2C_Connection reply;
          auto *accept = reply.mutable_accept();
          accept->set_client_slot(slot);
          accept->set_map_name(context.world.session.map_name.empty()
                                  ? "start.map"
                                  : context.world.session.map_name);
          accept->set_server_tickrate(
              static_cast<int>(context.cvars->sv_tickrate));
          accept->set_map_path(current_map_wire_id(context));
          accept->set_content_hash(context.world.map_content_hash);

          ::send_protobuf_message(context, sender, reply);
        }

        send_cvar_values(context, slot,
                         shared::collect_mirrored_cvars(*context.cvars));

        // Announce join to all clients (including the new one)
        broadcast_server_text_message(
            context, std::format("{} joined the server (slot {})",
                                 cmd.connect().player_name(), slot));

        if (try_find_rules_entity(context) != nullptr && current_mode(context).admit_on_connect)
          try_admit_player(context, slot);
      }
      else
      {
        send_message_to_reject_incoming_connection(context, sender, "Server is Full. please try again later.", 0);
      }
    }
    else if (cmd.has_disconnect())
    {
      process_client_leave_message(context, sender);
    }
  }
}

static void handle_developer_console_entries(server_context_t &context)
{
  for (const auto& [client_slot, line] : context.incoming.developer_console_entries)
  {
    log_terminal("Command from slot {}: {}", client_slot, line);
    const network::Address &client_address =
        context.transport_layer.clients[client_slot].address;

    cvars::command_context_t command_context{.caller_slot = client_slot};
    auto reply = std::string{};
    cvars::console_result_t result = cvars::execute_console_line(
        *context.cvars, *context.commands, line, command_context, &reply);

    if (result == cvars::console_result_t::unknown_name)
      log_terminal("Unknown command from slot {}: {}", client_slot, line);

    // echo something back, if it succeeded or not.
    send_text_message_to_a_specific_client(
        context, client_address, reply.empty() ? ("OK: " + line) : reply);
  }
}

static void handle_map_data_requests(server_context_t &context)
{
  for (const auto &[client_slot, payload] : context.incoming.map_data_requests)
  {
    network::Bit_Reader reader(payload.data(), payload.size());
    shared::request_map_data_message_t request =
        shared::deserialize_request_map_data(reader);

    shared::map_package_t package =
        shared::build_map_package(context.world.current_map);
    std::vector<network::uint8> blob = shared::serialize_map_package(package);

    shared::map_data_message_t msg;
    msg.map_name     = context.world.session.map_name;
    msg.package_hash = shared::compute_map_package_hash(blob);
    msg.compressed   = false;
    msg.bytes        = std::move(blob);

    auto writer = network::Bit_Writer{};
    shared::serialize_map_data(writer, msg);

    network::begin_paced_transfer(
        context.transport_layer, client_slot, writer.buffer,
        static_cast<network::uint8>(network::Message_Type::S2C_MapData));

    log_terminal("Queued map package '{}' ({} bytes, {} fragments, hash {:#x}) "
                 "for slot {} (requested '{}').",
                 msg.map_name, msg.bytes.size(),
                 context.transport_layer.clients[client_slot].outbound_transfer.fragments.size(),
                 msg.package_hash, client_slot, request.map_name);
  }
}

// The ghost, in three passes that are all STATE compared with state (coop_ghost_plan.md §2F): what a
// client asked for, what it may be sent now, and what it has been told.
static void service_ghost_transfers(server_context_t &context)
{
  for (const auto &[client_slot, payload] : context.incoming.ghost_requests)
  {
    network::Bit_Reader reader(payload.data(), payload.size());
    context.clients[client_slot].requested_ghost_hash = shared::deserialize_request_ghost(reader).ghost_hash;
  }

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
                                  static_cast<network::uint8>(network::Message_Type::S2C_GhostData));
    row.client.requested_ghost_hash = 0;

    log_terminal("Queued {}-player ghost ({} bytes, hash {:#x}) for slot {}", announced.party_size,
                 announced.bytes.size(), announced.hash, row.slot);
  }

  // Not before map_ready: a client mid-load clears its ghost when the load finishes.
  for (connected_client_t row : connected_clients(context))
  {
    if (!row.client.map_ready || row.client.announced_ghost_hash == announced.hash)
      continue;

    auto message = shared::ghost_available_message_t{};
    message.party_size = announced.party_size;
    message.ghost_hash = announced.hash;
    message.byte_count = static_cast<uint32_t>(announced.bytes.size());

    auto writer = network::Bit_Writer{};
    shared::serialize_ghost_available(writer, message);
    network::queue_reliable_message(row.transport.reliable_stream,
                                    static_cast<network::uint8>(network::Message_Type::S2C_GhostAvailable),
                                    writer.buffer);
    row.client.announced_ghost_hash = announced.hash;
  }
}

// Everything on a C2S_ClientInput that is NOT input: the snapshot the client
// says it holds, the map it says it holds, and this tick's move credit.
static void read_riders_off_client_inputs(server_context_t &context)
{
  network::Server_Inbox& inbox = context.incoming;

  // this used to sort by timestamp which was broken regardless.
  // now ordered monotonically by command number so that commands in the same tick
  // will at least be processed later. :~)
  std::sort(inbox.client_inputs.begin(), inbox.client_inputs.end(),
    [](const auto& a, const auto& b)
    {
      if (a.first != b.first) return a.first < b.first;

      return a.second.input_number() < b.second.input_number();
    });

  // update (on the servers internal data structure)
  // each client's held snapshot, based on the held_snapshot tick from the move,
  // which (in theory?) should be the latest snapshot.
  for (size_t index = 0; index < inbox.client_inputs.size(); ++index)
  {
    const auto& [client_slot, input] = inbox.client_inputs[index];
    if (!is_valid_client_slot(client_slot))
      continue; // the input loop below logs it; one complaint per input is enough

    client_slot_t& client = context.clients[client_slot];
    client.held_snapshot_tick = std::max(client.held_snapshot_tick, input.held_snapshot_tick());

    // does this mean that it's a redundant input?
    const bool this_is_the_slots_newest_input =
        index + 1 == inbox.client_inputs.size() ||
        inbox.client_inputs[index + 1].first != client_slot;

    if (!this_is_the_slots_newest_input) continue;

    const bool map_ready_now =
        input.map_content_hash() == context.world.map_content_hash;

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

  network::poll_network(context.transport_layer, context.socket,
                        network::server_receive_drain_cap_in_datagrams,
                        context.tick_number, context.incoming);

  // if we lost someone, no use processing them.
  drop_timed_out_clients(context);

  handle_connection_messages(context);
  handle_developer_console_entries(context);
  handle_map_data_requests(context);
  service_ghost_transfers(context);

  // send a block of map fragments so not to swamp the connection.
  network::service_paced_transfers(
      context.transport_layer, context.socket,
      static_cast<size_t>(std::max(1, context.cvars->sv_map_transfer_fragments_per_tick)));

  read_riders_off_client_inputs(context);
}

} // namespace server
