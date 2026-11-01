#pragma once

namespace server
{

struct server_context_t;

// RECEIVE, the first of the tick's three parts (tick_def.md). Drains the
// socket, dispatching every Message payload as it arrives (connects, leaves,
// inputs, console lines, the map and ghost requests, each to its handler under
// messages/), drops the silent, services the ghost transfers, then reads the
// RIDERS off this tick's inputs -- the held snapshot tick, map_ready, and the
// move credits.
//
// Nothing here simulates. The inputs themselves are left in context.incoming,
// sorted, for step 3 to run.
void receive_from_clients(server_context_t& context);

} // namespace server
