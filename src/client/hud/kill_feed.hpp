#pragma once

#include "../../shared/events/generated/events_generated.hpp"
#include "../client_context.hpp"

namespace client::kill_feed
{

void on_rocket_detonated(client_context_t &context,
                         const shared::Rocket_Detonated &payload);


void on_player_died(client_context_t &context,
                    const shared::Player_Died &payload);

void on_player_spawned(client_context_t &context,
                       const shared::Player_Spawned &payload);

} // namespace client::kill_feed
