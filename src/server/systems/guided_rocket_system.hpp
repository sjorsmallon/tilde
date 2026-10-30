#pragma once

#include "../server_context.hpp"

namespace server
{

// Every pilot has one rocket where their Movement flew it; a flight that ends leaves what its row says.
void update_guided_rockets(server_context_t& context, uint32_t tickrate_hz);

} // namespace server
