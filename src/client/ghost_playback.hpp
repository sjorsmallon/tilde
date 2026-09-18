#pragma once

#include "../shared/ghost.hpp"

#include <optional>

namespace client
{

struct client_context_t;

// Reads <maps dir>/<map>.ghost into context.world.ghost, or clears it.
void reload_map_ghost(client_context_t& context);

// Where the ghost is now, on our own input counter (latched to the run's start once), so a tie reads as a tie.
[[nodiscard]] std::optional<shared::ghost_pose_t> try_sample_map_ghost(client_context_t& context);

} // namespace client
