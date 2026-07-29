#include "server/server_api.hpp"
#include "shared/asset.hpp"
#include "shared/crash_handler.hpp"
#include "shared/cvars/generated/cvars_generated.hpp"
#include "shared/detached_console.hpp"
#include "shared/log.hpp"
#include "shared/timed_function.hpp"

#include <chrono>
#include <iostream>
#include <thread>

// THE cvar values and command bindings for this process (see the comment in
// main_integrated.cpp). Only the @Server half of the command table gets
// filled: no client module is loaded, so the @Client slots stay null and
// execute_console_line reports that rather than silently doing nothing.
//
// r_fov was declared here too, for no reason -- a dedicated server renders
// nothing. It is one @Client field on the struct now, unread on this side.
static cvars::cvar_state_t    g_cvar_state{};
static cvars::command_table_t g_command_table{};

int main(int argc, char *argv[])
{
  crash_handler::install();
  // console::SpawnNew();
  timed_function();

  log_terminal("=== Starting MyGame SERVER (Dedicated) ===");

  // Asset registration is EAGER and must run before anything resolves an asset
  // id. The lazy "__primitive_" init this replaced meant an id resolved to a
  // mesh or to nothing depending on what had run first; get_mesh now reports
  // loudly if it is called before this.
  assets::init();

  if (!server::Init(&g_cvar_state, &g_command_table))
  {
    log_error("Server Init Failed");
    return 1;
  }

  log_terminal("=== Server Initialized. Press Ctrl+C to stop. ===");

  // Fixed-timestep server loop
  bool running = true;
  auto previous_time = std::chrono::high_resolution_clock::now();
  double accumulator = 0.0;

  while (running)
  {
    auto current_time = std::chrono::high_resolution_clock::now();
    double frame_time =
        std::chrono::duration<double>(current_time - previous_time).count();
    previous_time = current_time;

    // Clamp to prevent spiral of death
    if (frame_time > 0.25)
      frame_time = 0.25;

    double tick_interval = server::get_tick_interval();
    accumulator += frame_time;

    while (accumulator >= tick_interval)
    {
      server::Tick();
      accumulator -= tick_interval;
    }

    // Sleep for remaining time to avoid busy-waiting
    double sleep_time = tick_interval - accumulator;
    if (sleep_time > 0.001)
    {
      std::this_thread::sleep_for(
          std::chrono::microseconds(static_cast<int>(sleep_time * 1000000)));
    }
  }

  server::Shutdown();
  print_timing_stats();

  return 0;
}
