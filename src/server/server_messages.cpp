#include "server_messages.hpp"

#include "log.hpp"
#include "network/bitstream.hpp"
#include "network/server_transport_layer.hpp"
#include "server_context.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace server
{

std::string current_map_wire_id(const server_context_t &context)
{
  return std::filesystem::path(context.world.current_map_path).filename().generic_string();
}

void send_cvar_values(server_context_t &context, int32_t slot,
                      const shared::cvar_values_message_t &msg)
{
  auto writer  = network::Bit_Writer{};
  shared::serialize_cvar_values(writer, msg);
  network::queue_reliable_message(
      context.transport_layer.clients[slot].reliable_stream,
      static_cast<network::uint8>(network::Message_Type::S2C_CvarValues),
      writer.buffer);
}

void send_text_message_to_a_specific_client(server_context_t &context,
                                const network::Address &ip,
                                std::string_view text)
{
  const std::optional<int32_t> slot =
      network::try_find_client_slot(context.transport_layer, ip);
  if (!slot)
  {
    // Console text is the one S2C message with a legitimate unslotted
    // recipient: a rejected connect gets told why. There is no stream for a
    // peer with no slot, so that one goes out unreliably, once, and says so if
    // the socket refuses it.
    game::S2C_ServerMessage msg;
    msg.set_message(std::string(text));
    std::vector<network::uint8> buffer(msg.ByteSizeLong());
    msg.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));
    const auto packets = network::convert_to_packets(
        buffer, static_cast<network::uint8>(network::Message_Type::S2C_ServerMessage),
        context.transport_layer.next_message_id);
    for (const auto &packet : packets)
    {
      if (!context.socket.send(packet, ip))
        log_error("S2C_ServerMessage to {} failed to send ({} bytes): {}",
                  ip.to_string(), packet.header.payload_size, text);
    }
    return;
  }

  game::S2C_ServerMessage msg;
  msg.set_message(std::string(text));
  std::vector<network::uint8> buffer(msg.ByteSizeLong());
  msg.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));
  network::queue_reliable_message(
      context.transport_layer.clients[*slot].reliable_stream,
      static_cast<network::uint8>(network::Message_Type::S2C_ServerMessage),
      buffer);
}

void broadcast_server_text_message(server_context_t &context,
                                   std::string_view text)
{
  int recipient_count = 0;
  for (connected_client_t row : connected_clients(context))
  {
    ++recipient_count;
    send_text_message_to_a_specific_client(context, row.transport.address, text);
  }

  log_terminal("[BROADCAST -> {} client(s)] {}", recipient_count, text);
}

} // namespace server
