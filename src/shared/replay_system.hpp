#pragma once

#include "game.pb.h"
#include "log.hpp"
#include <fstream>
#include <string>

namespace game {

// Stateless free function to save a replay to disk
inline bool save_replay(const std::string &path, const Replay &replay) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    log_error("Failed to open file for writing: {}", path);
    return false;
  }

  if (!replay.SerializeToOstream(&out)) {
    log_error("Failed to serialize replay to: {}", path);
    return false;
  }

  log_terminal("Saved replay to {} ({} ticks)", path, replay.ticks_size());
  return true;
}

// Stateless free function to load a replay from disk
inline bool load_replay(const std::string &path, Replay &out_replay) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) {
    log_error("Failed to open file for reading: {}", path);
    return false;
  }

  if (!out_replay.ParseFromIstream(&in)) {
    log_error("Failed to parse replay from: {}", path);
    return false;
  }

  log_terminal("Loaded replay from {} ({} ticks)", path,
               out_replay.ticks_size());
  return true;
}

} // namespace game
