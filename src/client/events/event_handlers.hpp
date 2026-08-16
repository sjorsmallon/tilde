#pragma once

#include "../../shared/events/generated/events_generated.hpp"
#include "../client_context.hpp"

#include <vector>

namespace client
{

// Cosmetic effects (@Queued): decoded from the tail of a snapshot packet into
// values, then handed over one at a time. Fire-and-forget.
void dispatch_received_effects(client_context_t&                              context,
                               const std::vector<shared::dispatched_effect_t>& values);

// Gameplay events (@Streamed): read straight off the batch's bitstream into a
// typed stack local per record. No vector, no tagged union.
//
// `log_received` is cl_event_debug, passed rather than read so this half stays
// free of the cvar family.
void dispatch_received_game_events(client_context_t& context, network::Bit_Reader& reader,
                                   bool log_received);

} // namespace client
