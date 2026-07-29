#include "client/client_api.hpp"
#include "shared/asset.hpp"
#include "shared/crash_handler.hpp"
#include "shared/cvars/generated/cvars_generated.hpp"
#include "shared/detached_console.hpp"
#include "shared/log.hpp"
#include "shared/timed_function.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

// Networked client build: runs ONLY the client and connects to a separate
// server over UDP (127.0.0.1 today — see the "configurable server address" TODO).
// Unlike MyGame (integrated), there is no in-process server and no integrated
// session pointer, so the client goes fully through the snapshot/streaming
// pipeline. This is the target to use for testing map streaming: launch
// MyGame_Server first, then this against it. To exercise the no-local-map /
// mismatch path, remove or point last_map.txt at a map this client lacks — it
// will download the compiled package from the server.

// THE cvar values and command bindings for this process (see the comment in
// main_integrated.cpp). Only the @Client half of the command table gets filled
// here: no server module is loaded, so @Server names are forwarded over the
// wire instead of run locally.
static cvars::cvar_state_t    g_cvar_state{};
static cvars::command_table_t g_command_table{};

int main(int argc, char *argv[])
{
  // Change working directory to the project root (parent of cmake_build/), same
  // as the integrated launcher, so relative paths (last_map.txt, maps/,
  // resources/, shaders) resolve regardless of where the game is launched from.
  {
    auto exe_abs      = std::filesystem::absolute(argv[0]);
    auto project_root = exe_abs
                            .parent_path()  // cmake_build/bin/
                            .parent_path()  // cmake_build/
                            .parent_path(); // project root
    std::error_code ec;
    std::filesystem::current_path(project_root, ec);
    if (ec)
      printf("[main] WARNING: failed to set working directory to '%s': %s\n",
             project_root.string().c_str(), ec.message().c_str());
    else
      printf("[main] Working directory set to: %s\n", project_root.string().c_str());
  }

  crash_handler::install();
  timed_function();

  log_terminal("=== Starting MyGame (Networked Client) ===");

  // Eager asset registration, before anything resolves an id. See asset.hpp.
  assets::init();

  if (!client::Init(&g_cvar_state, &g_command_table))
  {
    log_error("Client Init Failed");
    return 1;
  }

  // No in-process server: leave the integrated session pointer null (the
  // default) so the renderer reads remote state from snapshots, and skip the
  // editor→server reload hook (the editor will skip the server-side reload step).

  log_terminal("=== Initialization Complete, Entering Loop ===");

  bool running = true;
  auto previous_time = std::chrono::high_resolution_clock::now();

  while (running)
  {
    auto current_time = std::chrono::high_resolution_clock::now();
    previous_time = current_time;

    // Client frame (renders at display rate; the server ticks in its own process)
    if (!client::Tick())
    {
      running = false;
      break;
    }

    // Framerate cap
    float maxfps = g_cvar_state.cl_maxfps;
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

  print_timing_stats();

  return 0;
}
