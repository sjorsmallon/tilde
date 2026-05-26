#pragma once

#include "../shared/game_events.hpp"
#include "client_context.hpp"

#include <vector>

namespace client
{

// Route every event in `events` to its consumers. Implementation is a single
// switch over `event.kind` calling concrete `consumer::on_<kind>(...)`
// functions — see plan §"Direct client-side dispatch" for why this beats a
// subscribe(kind, handler) registry. Unknown kinds log_error+assert; closed
// enum means server should never emit something we can't dispatch.
void dispatch_received_game_events(client_context_t &context,
                                   const std::vector<shared::game_event_t> &events);

} // namespace client
