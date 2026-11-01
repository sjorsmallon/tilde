#pragma once

#include "../shared/network/generated/messages_generated.hpp"
#include "../shared/replay/ghost.hpp"

#include <optional>

namespace client
{

struct client_context_t;

// Ghost_Available: adopt <maps dir>/<map>.<n>p.ghost when it hashes to what the server announced, else
// request it. The client never picks a ghost by itself.
void apply_ghost_announcement(client_context_t& context, const shared::Ghost_Available& announced);

// A Ghost_Transfer, the paced transfer's payload: verify, cache there, adopt.
void receive_ghost_transfer(client_context_t& context, Span<const network::uint8> payload);

// Where the ghost's run is now, in the GHOST's ticks, on our own input counter (latched to the run's start
// once), so a tie reads as a tie. Every track samples at this one clock.
[[nodiscard]] std::optional<double> try_ghost_run_tick(client_context_t& context);

} // namespace client
