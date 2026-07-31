#pragma once

#include "../../shared/game_events.hpp"
#include "../client_context.hpp"

namespace client::kill_feed
{

void on_rocket_detonated(client_context_t &context,
                         const shared::rocket_detonated_payload_t &payload);


void on_player_died(client_context_t &context,
                    const shared::player_died_payload_t &payload);

void on_player_spawned(client_context_t &context,
                       const shared::player_spawned_payload_t &payload);

} // namespace client::kill_feed
