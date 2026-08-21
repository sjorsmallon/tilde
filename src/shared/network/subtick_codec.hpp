#pragma once

#include "../subtick.hpp"

#include "game.pb.h"

#include <optional>

namespace network
{

void write_subtick_input(game::C2S_ClientInput& out_message,
                         const shared::subtick_input_t& input);

[[nodiscard]] std::optional<shared::subtick_input_t>
try_read_subtick_input(const game::C2S_ClientInput& message);

} // namespace network
