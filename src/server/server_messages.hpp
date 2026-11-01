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
// "new_map.source", resolved per side against that side's maps dir.
std::string current_map_wire_id(const server_context_t& context);

// The map as the Accept and every Change_Map name it: generation, path, name, hash.
shared::Server_Map current_server_map(const server_context_t& context);

// @Mirrored values to one client, on its own reliable scope. The whole set
// right after the Accept; the changed subset rides the reliable broadcast.
void send_cvar_values(server_context_t& context, int32_t slot, Span<const shared::Cvar_Value> values);

// Console text to one client, on its own reliable scope. A line longer than a
// Server_Text holds goes as several, which the stream keeps in order.
void send_server_text(server_context_t& context, int32_t slot, std::string_view text);

// The same line to every connected client.
void broadcast_server_text_message(server_context_t& context, std::string_view text);

// The one message to a peer with no slot: a Reject, sent once as its own
// datagram. The hashes are 0 unless the rejection IS that mismatch.
void send_reject_to_unslotted_peer(server_context_t& context, const network::Address& address,
                                   std::string_view reason, uint32_t server_schema_hash,
                                   uint32_t server_asset_table_hash);

} // namespace server
