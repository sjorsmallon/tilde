#pragma once

#include "../shared/ghost.hpp"

#include <optional>

namespace network
{
struct Client_Inbox;
}

namespace client
{

struct client_context_t;

// S2C_GhostAvailable: adopt <maps dir>/<map>.<n>p.ghost when it hashes to what the server announced, else
// request it. S2C_GhostData: verify, cache there, adopt. The client never picks a ghost by itself.
void consume_ghost_messages(client_context_t& context, const network::Client_Inbox& inbox);

// Where the ghost's run is now, in the GHOST's ticks, on our own input counter (latched to the run's start
// once), so a tie reads as a tie. Every track samples at this one clock.
[[nodiscard]] std::optional<double> try_ghost_run_tick(client_context_t& context);

} // namespace client
