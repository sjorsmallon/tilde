#include "client/client_api.hpp"
#include "server/server_api.hpp"
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

// THE cvar values for this process, owned here and lent to both modules. This
// is the fix the whole CVAR TRACK exists for: game_shared is a STATIC lib
// linked into game_client.dll and game_server.dll alike, so anything with
// static storage inside it exists twice. The old CVarSystem singleton was two
// singletons, which is why `spawn_bot` from the console never reached the
// server and why cl_timescale slowed rendering but not simulation. One object
// here, two borrowed pointers, no static init.
//
// `map` is no longer a cvar: it is a @Server COMMAND (see cvars.def), and the
// boot map comes from last_map.txt inside server::Init.
static cvars::cvar_state_t g_cvar_state{};

// The command tables, however, are ONE PER SIDE even though this is one
// process -- and that distinction is load-bearing. A command_table_t is a
// module's DISPATCH SURFACE (which names it can run, and whether it forwards),
// not shared process state like the values above.
//
// Sharing one table here was a real bug: the loopback client installs
// forward_to_server on connect, so the server, dispatching a `spawn_bot` line
// that had just arrived over loopback UDP, saw a @Server command AND a live
// forwarder and forwarded it straight back to itself. The line ping-ponged
// forever and the handler never ran.
//
// Split, each side gets the shape it actually has: the client's table holds
// the @Client binders plus a forwarder once connected, the server's holds the
// @Server binders and never forwards. The integrated client is a real
// networked client, so `spawn_bot` takes the same path it takes from a remote
// one -- which is the point of testing against the integrated build at all.
static cvars::command_table_t g_client_command_table{};
static cvars::command_table_t g_server_command_table{};

// The asset system's whole mutable state -- pools and manifest -- owned here for
// the same reason the cvars are: game_shared is a static lib, so a file-scope
// registry inside it exists once per module. Assets are firmly in the AGREE
// column: an asset_handle_t is a bare index into a pool, so a handle minted
// against one state is meaningless against another. One object, three borrowed
// pointers.
static assets::asset_state_t g_asset_state{};

int main(int argc, char *argv[])
{
  // Change working directory to the project root (parent of cmake_build/).
  // Executable lives at cmake_build/bin/MyGame.exe, so go up three levels.
  // We use absolute() first so this works regardless of where the game is launched from.
  {
    auto exe_abs    = std::filesystem::absolute(argv[0]);
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
  // console::SpawnNew();
  timed_function();

  log_terminal("=== Starting MyGame (Integrated) ===");

  // Point THIS module (the exe) at the one asset state, then register eagerly
  // before anything resolves an id. Each DLL points its own copy of the state
  // pointer at the same object inside its Init below -- game_shared is a static
  // lib, so there is one pointer per module and all three must be set. See the
  // ownership note in asset.hpp.
  assets::set_state(&g_asset_state);
  assets::init();

  if (!server::Init(&g_cvar_state, &g_server_command_table, &g_asset_state))
  {
    log_error("Server Init Failed");
    return 1;
  }

  if (!client::Init(&g_cvar_state, &g_client_command_table, &g_asset_state))
  {
    log_error("Client Init Failed");
    server::Shutdown(); // Cleanup
    return 1;
  }

  // Integrated build: hand the client a direct pointer to the server's session
  // so it can render entity pools (physics bodies, etc.) without going through
  // the snapshot/interpolation pipeline.
  client::set_integrated_server_session(server::get_session_for_integrated_client());

  // Bridge the editor's commit_map_to_disk → server::reload_map. Editor and
  // server live in different DLLs that don't link each other; this hook lets
  // them rendez-vous through the launcher.
  client::set_server_map_reload_hook(
      [](const char *path) -> bool { return server::reload_map(path); });

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

    // Apply time scale. Same object client_impl.cpp scales the client frame dt
    // from, so slow-mo now slows both halves together.
    double timescale = static_cast<double>(g_cvar_state.cl_timescale);
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
  server::Shutdown();

  print_timing_stats();

// #ifdef _WIN32
//   log_terminal("Press Enter to exit...");
//   std::cin.get();
// #endif

  return 0;
}
