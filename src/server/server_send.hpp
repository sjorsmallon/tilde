#pragma once

namespace server
{

struct server_context_t;

// SEND, the last of the tick's three parts (tick_def.md): this tick's snapshot
// into the ring and out to every client that holds the map, the effect batch,
// the event batch, the replay record, then the changed @Mirrored cvars and
// whatever the reliable streams still owe.
//
// Everything before this has finished writing the world, which is what makes
// one frame answer every client.
void send_to_clients(server_context_t& context);

} // namespace server
