#include "client/client_api.hpp"
#include "shared/asset.hpp"
#include "shared/crash_handler.hpp"
#include "shared/cvars/generated/cvars_generated.hpp"
#include "shared/detached_console.hpp"
#include "shared/log.hpp"
#include "shared/cpu_topology.hpp"
#include "shared/frame_timing.hpp"
#include "shared/memory_audit.hpp"
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

// Owned here for the same static-lib reason as the cvars above: the exe and
// game_client.dll each hold their own asset state pointer, and both must point
// at this one object. See the ownership note in asset.hpp.
static assets::asset_state_t  g_asset_state{};

// The one allocation-audit state for this process; see the note in
// main_integrated.cpp. Constant-initialized, so it is usable before any dynamic
// initializer has run.
static memory_audit::memory_audit_state_t g_memory_audit_state{};

// The one frame-time distribution for this process. Unlike the audit above this
// is installed in EVERY build: a QPC read and a histogram bump per frame costs
// nothing, and a hitch is what we are trying not to have.
static frame_timing::frame_timing_state_t g_frame_timing_state{};

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

#if TILDE_MEMORY_AUDIT
  memory_audit::set_state(&g_memory_audit_state);
  client::install_memory_audit(&g_memory_audit_state);
#endif
  frame_timing::set_state(&g_frame_timing_state);
  client::install_frame_timing(&g_frame_timing_state);

  // Point this module at the one asset state, then register eagerly before
  // anything resolves an id. client::Init points the DLL's copy at it too.
  assets::set_state(&g_asset_state);
  assets::mount_asset_source();
  assets::init();

  if (!client::init(&g_cvar_state, &g_command_table, &g_asset_state))
  {
    log_error("Client Init Failed");
    return 1;
  }

  // No in-process server: leave the integrated session pointer null (the
  // default) so the renderer reads remote state from snapshots, and skip the
  // editor→server reload hook (the editor will skip the server-side reload step).

  log_terminal("=== Initialization Complete, Entering Loop ===");

  bool running = true;
  std::chrono::high_resolution_clock::time_point previous_time{};

  // Before the loop, and read once: on a hybrid CPU the scheduler moving the
  // main thread to an E-core costs ~40% of its throughput for that frame, which
  // is a hitch no allocation or cache profiler can see. The framerate cap below
  // SLEEPS every frame, and a thread that sleeps is exactly what Thread
  // Director demotes -- so this matters more here than it would otherwise.
  if (g_cvar_state.pin_main_thread)
    cpu_topology::try_pin_current_thread_to_performance_cores();

  // Closes the load out as STARTUP so it is not attributed to frame 1. Without
  // this the worst frame is always the load, which hides the worst gameplay
  // frame -- the one hitches actually come from.
  memory_audit::mark_startup_complete();

  // AFTER the two above, so the pin's processor enumeration is not measured as
  // the first frame.
  previous_time = std::chrono::high_resolution_clock::now();

  while (running)
  {
    auto current_time = std::chrono::high_resolution_clock::now();
    const double raw_frame_milliseconds =
        std::chrono::duration<double>(current_time - previous_time).count() * 1000.0;
    previous_time = current_time;

    // Closes the previous frame. Both calls cover the SAME interval -- the
    // period between loop iterations -- so the duration and the allocation
    // count describe one frame and can be reported together. The period is what
    // the player experiences, cap sleep included; timing only the work would
    // hide a stall in the cap or in the present.
    memory_audit::mark_frame();
    frame_timing::end_frame(raw_frame_milliseconds, memory_audit::last_frame_allocations());

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

  log_terminal("=== shutdown Initiated ===");
  client::shutdown();

  print_timing_stats();

  frame_timing::report();

#if TILDE_MEMORY_AUDIT
  memory_audit::report(30);
#endif

  return 0;
}
