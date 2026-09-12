#pragma once

#include "../shared/network/udp_socket.hpp"

#include <string_view>

namespace server
{

struct server_context_t;

// Console text to one peer, on its reliable stream; a peer with no slot (a
// rejected connect) is told once, unreliably.
void send_text_message_to_a_specific_client(server_context_t& context,
                                            const network::Address& address,
                                            std::string_view text);

// The same line to every connected client.
void broadcast_server_text_message(server_context_t& context, std::string_view text);

} // namespace server
