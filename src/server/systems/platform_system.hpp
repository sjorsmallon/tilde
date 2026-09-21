#pragma once

#include "../../shared/span.hpp"
#include "../server_context.hpp"

#include <cstdint>

namespace server
{

// Latches a fresh platform's launch (retiring its owner's older one), writes every platform's position, reaps the expired.
void update_platforms(server_context_t& context, Span<const uint8_t> disabled_geometry);

} // namespace server
