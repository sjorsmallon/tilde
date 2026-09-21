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
  float freeze_seconds    = 0.0f;
  float live_seconds      = 0.0f;
  float round_end_seconds = 0.0f;
  float game_over_seconds = 0.0f;
  float next_map_seconds  = 0.0f;
};

[[nodiscard]] round_timing_t round_timing_from_cvars(const cvars::cvar_state_t &cvars);

// The map's one Game_Rules_Entity. Null only before the first map load has run
// install_match, which is the one state a context can be in without one.
[[nodiscard]] entities::Game_Rules_Entity *try_find_rules_entity(server_context_t &context);
[[nodiscard]] const entities::Game_Rules_Entity *try_find_rules_entity(const server_context_t &context);

// The same, for a caller that runs inside a loaded world: none is a bug.
[[nodiscard]] entities::Match &match_of(server_context_t &context);
[[nodiscard]] const entities::Match &match_of(const server_context_t &context);

[[nodiscard]] const game_mode_settings_t &current_mode(const server_context_t &context);

// How many Game_Rules_Entity a map may carry is one; more is a refusal the
// loader reports before the map replaces the running one.
[[nodiscard]] uint32_t count_rules_entities(const shared::map_t &map);

// After build_session: mint the rules entity the map did not carry, and enter
// Warmup once. The ready vote gates a session's FIRST map: a map loaded over a
// started match starts by itself (Match::starts_when_loaded).
void install_match(server_context_t &context, uint32_t current_tick, uint32_t tickrate_hz,
                   bool replaces_a_started_match);

// Read before a map load wipes the world it asks about.
[[nodiscard]] bool match_has_started(const server_context_t &context);

// Whether a phase can take a request. The handlers ask it before writing one and
// update_match asks it again before paying one.
[[nodiscard]] bool match_request_is_allowed(entities::Round_Phase phase,
                                            entities::Match_Request request);

// Joined humans (a connected client with a body) and how many of them voted
// ready. Bots have no client slot, so they neither count nor vote.
struct warmup_vote_t
{
  int32_t joined = 0;
  int32_t ready  = 0;
};

[[nodiscard]] warmup_vote_t count_warmup_vote(server_context_t &context);

// The one step that changes the phase: a request, else the mode's poll, else the
// deadline. Call once per server tick.
void update_match(server_context_t &context, uint32_t current_tick, uint32_t tickrate_hz);

[[nodiscard]] entities::Team_Allegiance pick_team_for_new_player(server_context_t &context);

bool is_round_live(const server_context_t &context);

bool is_movement_allowed(const server_context_t &context);

bool can_take_damage(const server_context_t &context);

} // namespace server
