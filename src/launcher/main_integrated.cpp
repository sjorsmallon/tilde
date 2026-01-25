#include "client/client_api.hpp"
#include "server/server_api.hpp"
#include "shared/cvar.hpp"
#include "shared/detached_console.hpp"
#include "shared/log.hpp"
#include "shared/timed_function.hpp"

#include <iostream>

cvar::CVar<float> r_fov("r_fov", 90.0f, "Field of view in degrees");

int main(int argc, char *argv[]) {
  console::SpawnNew();
  timed_function();

  log_terminal("=== Starting MyGame (Integrated) ===");

  if (!server::Init()) {
    log_error("Server Init Failed");
    return 1;
  }

  if (!client::Init()) {
    log_error("Client Init Failed");
    server::Shutdown(); // Cleanup
    return 1;
  }

  log_terminal("=== Initialization Complete, Entering Loop ===");

  bool running = true;
  while (running) {
    // Run Client Frame (Input/Render)
    // If client wants to quit (window closed), we stop.
    if (!client::Tick()) {
      running = false;
    }

    // Run Server Tick (Game Logic)
    // In a real scenario, this might run on a separate thread or at a fixed
    // timestep
    server::Tick();
  }

  log_terminal("=== Shutdown Initiated ===");
  client::Shutdown();
  server::Shutdown();

  print_timing_stats();

#ifdef _WIN32
  log_terminal("Press Enter to exit...");
  std::cin.get();
#endif

  return 0;
}
