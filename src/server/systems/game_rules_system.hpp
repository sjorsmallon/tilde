#pragma once

#include "../game_mode.hpp"
#include "../server_context.hpp"

#include <cstdint>

namespace server
{

struct round_timing_t
{
  float warmup_seconds    = 0.0f;
  float countdown_seconds = 0.0f;
  float live_seconds      = 0.0f;
  float round_end_seconds = 0.0f;
  float game_over_seconds = 0.0f;
};

[[nodiscard]] round_timing_t round_timing_from_cvars(const cvars::cvar_state_t &cvars);

[[nodiscard]] const game_mode_settings_t &current_mode(const server_context_t &context);

void apply_game_mode_cvar(server_context_t &context);

void try_start_match_when_enough_players(server_context_t &context,
                                         uint32_t current_tick,
                                         uint32_t tickrate_hz);

void check_win_condition(server_context_t &context,
                         uint32_t current_tick,
                         uint32_t tickrate_hz);

// autoassign
[[nodiscard]] entities::Team_Allegiance pick_team_for_new_player(server_context_t &context);


void reset_game_rules(server_context_t &context,
                      uint32_t current_tick,
                      uint32_t tickrate_hz);

// call once per server tick.
void update_game_rules(server_context_t &context,
                       uint32_t current_tick,
                       uint32_t tickrate_hz);

void start_match(server_context_t &context,
                 uint32_t current_tick,
                 uint32_t tickrate_hz);

void end_round(server_context_t &context,
               uint32_t current_tick,
               uint32_t tickrate_hz);

bool is_round_live(const server_context_t &context);

bool is_movement_allowed(const server_context_t &context);

bool can_take_damage(const server_context_t &context);

} // namespace server
