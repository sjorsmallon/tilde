#pragma once
#include "linalg.hpp"
#include <cstdint>
#include <vector>

// Lightweight debug bridge between game_server and game_client.
// The server populates g_entries once per tick (via update_bots);
// the client reads them each render frame.  Integrated mode only —
// not thread-safe, assumes a single-threaded game loop.
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
