#include "server/server_api.hpp"
#include "shared/crash_handler.hpp"
#include "shared/cvar.hpp"
#include "shared/detached_console.hpp"
#include "shared/log.hpp"
#include "shared/timed_function.hpp"

#include <chrono>
#include <iostream>
#include <thread>

cvar::CVar<float> r_fov("r_fov", 90.0f, "Field of view in degrees");

int main(int argc, char *argv[])
{
  crash_handler::install();
  // console::SpawnNew();
  timed_function();

  log_terminal("=== Starting MyGame SERVER (Dedicated) ===");

  if (!server::Init())
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
