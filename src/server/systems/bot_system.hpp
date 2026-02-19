#pragma once

#include "../../shared/game_session.hpp"
#include "../../shared/network/network_types.hpp"
#include <vector>

namespace server
{

// Bots use client_slot_index >= BOT_SLOT_BASE so the server never confuses
// them with a real network connection. Starting right after the real-player
// range keeps the value meaningful and in sync with sv_max_player_count.
static constexpr int32_t BOT_SLOT_BASE = network::sv_max_player_count;

struct Bot_State
{
  int32_t player_slot  = -1;
  float   fire_cooldown = 0.f;
};

// Spawns a bot Player_Entity at position and returns its tracking state.
Bot_State spawn_bot(shared::game_session_t &session, const vec3f &position,
                    int32_t slot);

// Called once per server tick.
void update_bots(std::vector<Bot_State> &bots,
                 shared::game_session_t  &session,
                 float                    dt);

} // namespace server
