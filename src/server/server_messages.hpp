#pragma once

#include "../shared/network/cvar_mirror.hpp"
#include "../shared/network/udp_socket.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace server
{

struct server_context_t;

// What the wire calls the running map: maps-relative, a basename like
// "new_map.source", resolved per side against that side's maps dir. The accept
// handshake and CmdChangeMap are the two senders and they must agree.
std::string current_map_wire_id(const server_context_t& context);

// @Mirrored values to one client, on its reliable stream. The whole set right
// after CmdAccept, the changed subset every tick after that.
void send_cvar_values(server_context_t& context, int32_t slot,
                      const shared::cvar_values_message_t& msg);

// Console text to one peer, on its reliable stream; a peer with no slot (a
// rejected connect) is told once, unreliably.
void send_text_message_to_a_specific_client(server_context_t& context,
                                            const network::Address& address,
                                            std::string_view text);

// The same line to every connected client.
void broadcast_server_text_message(server_context_t& context, std::string_view text);

} // namespace server
