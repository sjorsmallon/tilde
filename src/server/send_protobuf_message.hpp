#pragma once

#include "../shared/network/packet.hpp"
#include "server_context.hpp"

#include <cstdint>
#include <vector>

// although it's not constrained, the packet_traits specialization trick here constrains the template.
// @FIXME(SJM): this is a duplicate of a different thing that client
// also uses that we maybe need to unify.
template <typename Type>
inline void send_protobuf_message(server::server_context_t& context, const network::Address& sender,
                                  const Type& msg)
{
  auto buffer = std::vector<uint8_t>(msg.ByteSizeLong());
  msg.SerializeToArray(buffer.data(), static_cast<int>(buffer.size()));

  const std::vector<network::Packet> packets = network::convert_to_packets(
      buffer, static_cast<uint8_t>(network::Packet_Traits<Type>::type),
      context.transport_layer.next_message_id);

  for (const network::Packet& packet : packets)
  {
    context.socket.send(packet, sender);
  }
}
