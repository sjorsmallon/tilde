#pragma once

#include <cstdint>
#include <string>

// Cross-platform export macros
#if defined(_WIN32) || defined(__CYGWIN__)
  #ifdef GAME_SERVER_EXPORTS
    #define GAME_SERVER_API __declspec(dllexport)
  #else
    #define GAME_SERVER_API __declspec(dllimport)
  #endif
#else
  // GCC/Clang on Linux/macOS
  #define GAME_SERVER_API __attribute__((visibility("default")))
#endif

namespace shared { struct game_session_t; }
namespace cvars { struct cvar_state_t; struct command_table_t; }
namespace assets { struct asset_state_t; }
namespace memory_audit { struct memory_audit_state_t; }
namespace frame_timing { struct frame_timing_state_t; }

namespace server {
// The server half of the same install; see the note in client_api.hpp.
GAME_SERVER_API void install_memory_audit(memory_audit::memory_audit_state_t *state);
GAME_SERVER_API void install_frame_timing(frame_timing::frame_timing_state_t *state);

// `cvar_state`, `command_table` and `asset_state` are owned by the LAUNCHER and
// outlive the server module. Init stashes the first two on the server context
// and calls cvars::bind_server_commands(*command_table), which fills the
// @Server handler slots with symbols that live in this DLL. None may be null.
//
// `asset_state` is here for the same static-lib reason as on the client (see
// client_api.hpp): this DLL has its own copy of the asset state pointer. The
// server resolves meshes when baking map collision, so it needs the launcher's
// one state too.
GAME_SERVER_API bool init(cvars::cvar_state_t *cvar_state,
                          cvars::command_table_t *command_table,
                          assets::asset_state_t *asset_state);
GAME_SERVER_API bool Tick();
GAME_SERVER_API void shutdown();
GAME_SERVER_API double get_tick_interval();
GAME_SERVER_API uint32_t get_tick_number();

// Integrated-mode only: returns a pointer to the server's authoritative session
// so the client can render directly from it instead of going through the
// snapshot/interpolation pipeline. Returns nullptr if init() has not run.
GAME_SERVER_API const shared::game_session_t *get_session_for_integrated_client();

// Reload the server with a different map. Wipes the current session, physics
// world, bots, and per-client baselines, then loads `map_path` and respawns
// map-defined bots. Any currently-connected players are disconnected; they
// will reconnect on their next Connect packet. Returns true on successful load.
GAME_SERVER_API bool change_map_to(const std::string &map_path);
} // namespace server
