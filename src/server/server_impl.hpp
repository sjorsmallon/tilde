#pragma once

#include "server_context.hpp"

namespace server
{

// The ONE server_context_t, defined in server_impl.cpp. That file also owns the
// module's lifetime -- init, shutdown, the map load -- and the @Server command
// handlers, which the generated binder calls with console arguments and nothing
// else. Only those entry points name this object; everything below them takes a
// server_context_t& parameter.
extern server_context_t g_server_context;

// A `map` console line or Game_Over's deadline wrote context.pending_map_change.
// Serviced at the TOP of the tick, because the load frees the world and nothing
// may be holding a pointer into it (tick_def.md).
void service_pending_map_change(server_context_t& context);

} // namespace server
