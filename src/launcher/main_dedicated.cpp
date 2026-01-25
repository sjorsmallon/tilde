#include "server/server_api.hpp"
#include "shared/cvar.hpp"
#include "shared/detached_console.hpp"
#include "shared/log.hpp"
#include "shared/timed_function.hpp"

#include <chrono>
#include <iostream>
#include <thread>

cvar::CVar<float> r_fov("r_fov", 90.0f, "Field of view in degrees");

int main(int argc, char *argv[]) {
  console::SpawnNew();
  timed_function();

  log_terminal("=== Starting MyGame SERVER (Dedicated) ===");

  if (!server::Init()) {
    log_error("Server Init Failed");
    return 1;
  }

  log_terminal("=== Server Initialized. Press Ctrl+C to stop. ===");

  // Simple loop for dedicated server
  bool running = true;
  while (running) {
    // Run Server Logic
    server::Tick();

    // In a real dedicated server, we'd sleep to maintain tickrate
    // For now, sleep 16ms to avoid burning CPU in this loop
    std::this_thread::sleep_for(std::chrono::milliseconds(16));

    // TODO: Handle signal handling for graceful shutdown (SIGINT)
    // For this demo, runs until killed or console closed.
  }

  server::Shutdown();
  print_timing_stats();

  return 0;
}
