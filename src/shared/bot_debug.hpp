#pragma once
#include "linalg.hpp"
#include <cstdint>
#include <vector>

// Bot debug visualisation state, owned and filled by the CLIENT.
//
// This is NOT a memory bridge between game_server and game_client, though it
// claimed to be one for a long time. It cannot be: game_shared is a static lib,
// so each module links its own copy of g_entries and the server's writes were
// never visible here. The server's real path is `g_bots` -> S2C_BotDebug ->
// this vector, filled from the wire in play_state.cpp, which works in both the
// integrated (loopback) and networked builds.
//
// Not thread-safe; assumes a single-threaded client loop.
namespace bot_debug {

struct Entry {
  int32_t  slot       = -1;
  int      goal       = 0; // BotGoal: 0=Idle 1=Chase 2=Attack 3=Retreat
  int      type       = 0; // BotType: 0=Idle 1=Chase 2=Regular
  std::vector<linalg::vec3> path;
  int      path_index = 0;
};

extern std::vector<Entry> g_entries;

inline void clear() { g_entries.clear(); }

} // namespace bot_debug
