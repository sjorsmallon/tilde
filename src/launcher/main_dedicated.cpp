#include "server/server_api.hpp"
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

// Owned here for the same static-lib reason as the cvars above: the exe and
// game_server.dll each hold their own asset state pointer, and both must point
// at this one object. See the ownership note in asset.hpp.
static assets::asset_state_t  g_asset_state{};

// The one allocation-audit state for this process; see the note in
// main_integrated.cpp. Constant-initialized, so it is usable before any dynamic
// initializer has run.
static memory_audit::memory_audit_state_t g_memory_audit_state{};

// The tick-time distribution. Installed in every build; see main_integrated.cpp.
static frame_timing::frame_timing_state_t g_frame_timing_state{};

int main(int argc, char *argv[])
{
  crash_handler::install();
  // console::SpawnNew();
  timed_function();

  log_terminal("=== Starting MyGame SERVER (Dedicated) ===");

#if TILDE_MEMORY_AUDIT
  memory_audit::set_state(&g_memory_audit_state);
  server::install_memory_audit(&g_memory_audit_state);
#endif
  frame_timing::set_state(&g_frame_timing_state);
  server::install_frame_timing(&g_frame_timing_state);

  // Asset registration is EAGER and must run before anything resolves an asset
  // id. The lazy "__primitive_" init this replaced meant an id resolved to a
  // mesh or to nothing depending on what had run first; get_mesh now reports
  // loudly if it is called before this. set_state comes first: init() fills the
  // state this module points at, and server::Init points the DLL at the same one.
  assets::set_state(&g_asset_state);
  assets::mount_asset_source();
  assets::init();

  if (!server::init(&g_cvar_state, &g_command_table, &g_asset_state))
  {
    log_error("Server Init Failed");
    return 1;
  }

  log_terminal("=== Server Initialized. Press Ctrl+C to stop. ===");

  // Fixed-timestep server loop
  bool running = true;
  std::chrono::high_resolution_clock::time_point previous_time{};
  double accumulator = 0.0;

  // A dedicated server sleeps between ticks, which is what makes Thread
  // Director demote it to an E-core -- the same reason the client launchers pin.
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
      // A dedicated server has no frames; a tick is its unit of work -- and
      // what is worth measuring is how long a tick TOOK, not the period between
      // ticks, which the accumulator fixes by construction. So unlike the two
      // client launchers, the mark comes AFTER the work rather than before it,
      // which is also what pairs the allocation count with the right tick.
      const auto tick_start = std::chrono::high_resolution_clock::now();
      server::Tick();
      memory_audit::mark_frame();
      frame_timing::end_frame(std::chrono::duration<double>(
                                  std::chrono::high_resolution_clock::now() - tick_start)
                                      .count() *
                                  1000.0,
                              memory_audit::last_frame_allocations());
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

  server::shutdown();
  print_timing_stats();

  frame_timing::report();

#if TILDE_MEMORY_AUDIT
  memory_audit::report(30);
#endif

  return 0;
}
