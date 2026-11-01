#pragma once

#include "../shared/network/generated/messages_generated.hpp"
#include "client_context.hpp"

namespace client
{

// Defined by client_messages_bindings_generated.cpp: a switch over every
// @Server_To_Client row of messages.def calling client::messages::on_<name>,
// one hand-written file each under src/client/messages/. A row with no file is
// a link error naming the symbol. `reader` holds the streams of a Message
// payload, positioned past the server tick. `log_filter` is cl_event_debug.
void dispatch_received_messages(client_context_t& context, network::Bit_Reader& reader,
                                shared::message_log_filter_t log_filter);

// The one reader of a Message payload, live or replayed: the server tick off
// its head into connection.last_server_tick, its streams through the dispatch
// above, then the streams into the recording. Defined in play_state.cpp.
void receive_server_message_payload(client_context_t& context, Span<const network::uint8> payload);

} // namespace client
