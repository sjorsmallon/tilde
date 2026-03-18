#include "client/client_api.hpp"
#include "server/server_api.hpp"
#include "shared/crash_handler.hpp"
#include "shared/cvar.hpp"
#include "shared/detached_console.hpp"
#include "shared/game_cvars.hpp"
#include "shared/log.hpp"
#include "shared/timed_function.hpp"

#include <chrono>
#include <iostream>
#include <thread>

cvar::CVar<float> r_fov("r_fov", 90.0f, "Field of view in degrees");
cvar::CVar<float> cl_maxfps("cl_maxfps", 1000.0f, "Maximum client framerate (0 = unlimited)");
cvar::CVar<std::string> map("map", "dm_aabb", "Map to load", cvar::flags::None,
                            [](const std::string &val)
                            { log_terminal("Map changed to: {}", val); });

int main(int argc, char *argv[])
{
  crash_handler::install();
  // console::SpawnNew();
  timed_function();

  log_terminal("=== Starting MyGame (Integrated) ===");

  if (!server::Init())
  {
    log_error("Server Init Failed");
    return 1;
  }

  if (!client::Init())
  {
    log_error("Client Init Failed");
    server::Shutdown(); // Cleanup
    return 1;
  }

  log_terminal("=== Initialization Complete, Entering Loop ===");

  bool running = true;
  auto previous_time = std::chrono::high_resolution_clock::now();
  double server_accumulator = 0.0;

  while (running)
  {
    auto current_time = std::chrono::high_resolution_clock::now();
    double frame_time =
        std::chrono::duration<double>(current_time - previous_time).count();
    previous_time = current_time;

    if (frame_time > 0.25)
      frame_time = 0.25;

    // Apply time scale
    double timescale = static_cast<double>(cl_timescale.Get());
    if (timescale < 0.01)
      timescale = 0.01;
    frame_time *= timescale;

    // Client frame (renders at display rate)
    if (!client::Tick())
    {
      running = false;
      break;
    }

    // Server ticks at fixed rate
    double tick_interval = server::get_tick_interval();
    server_accumulator += frame_time;
    while (server_accumulator >= tick_interval)
    {
      server::Tick();
      server_accumulator -= tick_interval;
    }

    // Framerate cap
    float maxfps = cl_maxfps.Get();
    if (maxfps > 0.f)
    {
      double min_frame_time = 1.0 / static_cast<double>(maxfps);
      auto frame_end = std::chrono::high_resolution_clock::now();
      double elapsed =
          std::chrono::duration<double>(frame_end - previous_time).count();
      if (elapsed < min_frame_time)
      {
        double sleep_us = (min_frame_time - elapsed) * 1000000.0;
        std::this_thread::sleep_for(
            std::chrono::microseconds(static_cast<int64_t>(sleep_us)));
      }
    }
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
