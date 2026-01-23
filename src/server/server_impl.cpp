#include "server_api.hpp"

#include <iostream>
#include <string>

#include "cvar.hpp"
#include "game.pb.h"
#include "log.hpp"
#include "timed_function.hpp"

namespace server {

cvar::CVar<float> sv_tickrate("sv_tickrate", 60.0f, "Server tick rate in Hz");

bool Init() {
  timed_function();
  log_terminal("--- Initializing Server ---");

  // Protobuf Test (Moved from main)
  log_terminal("server: Testing Protobuf...");
  game::Player player;
  player.set_name("Hero");
  player.set_id(1234);
  player.set_score(99.9f);

  std::string serialized;
  if (player.SerializeToString(&serialized)) {
    log_terminal("server: Serialized player: {} ({} bytes)", player.name(),
                 serialized.size());
  }

  game::Player player_loaded;
  if (player_loaded.ParseFromString(serialized)) {
    log_terminal("server: Deserialized player: {}, Score: {}",
                 player_loaded.name(), player_loaded.score());
  }

  return true;
}

bool Tick() {
  timed_function();
  // Server logic simulation
  // In a real server, we'd sleep to maintain sv_tickrate
  return true;
}

void Shutdown() {
  timed_function();
  log_terminal("--- Shutting down Server ---");
}

} // namespace server
