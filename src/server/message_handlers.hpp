#pragma once

#include "../shared/network/generated/messages_generated.hpp"
#include "server_context.hpp"

namespace server
{

// Defined by server_messages_bindings_generated.cpp: a switch over every
// @Client_To_Server row of messages.def calling server::messages::on_<name>,
// one hand-written file each under src/server/messages/. A row with no file is
// a link error naming the symbol. `reader` holds a Message payload: one or more
// finished streams back to back. `log_filter` is sv_event_debug.
void dispatch_received_messages(server_context_t& context, int32_t slot,
                                network::Bit_Reader& reader, shared::message_log_filter_t log_filter);

} // namespace server
