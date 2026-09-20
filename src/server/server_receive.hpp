#pragma once

namespace server
{

struct server_context_t;

// RECEIVE, the first of the tick's three parts (tick_def.md). Drains the
// socket, drops the silent, answers connects, leaves, console lines and the map
// and ghost transfer requests, then reads the RIDERS off this tick's inputs --
// the held snapshot tick, map_ready, and the move credits.
//
// Nothing here simulates. The inputs themselves are left in context.incoming,
// sorted, for step 3 to run.
void receive_from_clients(server_context_t& context);

} // namespace server
