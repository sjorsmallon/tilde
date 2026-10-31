#pragma once

#include "../subtick.hpp"

#include "game.pb.h"

#include <optional>

namespace network
{

void write_tick_input(game::C2S_ClientInput& out_message,
                         const shared::tick_input_t& input);

[[nodiscard]] std::optional<shared::tick_input_t>
try_read_tick_input(const game::C2S_ClientInput& message);

} // namespace network
