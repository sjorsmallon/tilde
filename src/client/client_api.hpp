#pragma once

// Cross-platform export macros
#if defined(_WIN32) || defined(__CYGWIN__)
  #ifdef GAME_CLIENT_EXPORTS
    #define GAME_CLIENT_API __declspec(dllexport)
  #else
    #define GAME_CLIENT_API __declspec(dllimport)
  #endif
#else
  // GCC/Clang on Linux/macOS
  #define GAME_CLIENT_API __attribute__((visibility("default")))
#endif

namespace shared { struct game_session_t; }
namespace cvars { struct cvar_state_t; struct command_table_t; }
namespace assets { struct asset_state_t; }
namespace memory_audit { struct memory_audit_state_t; }
namespace frame_timing { struct frame_timing_state_t; }

namespace client {
// Point this DLL's copy of the audit state pointer at the launcher's one
// object. Separate from init() and meant to be called BEFORE it: the earlier it
// runs the fewer allocations are missed, and unlike the three states above this
// one is a diagnostic that a non-audit build never installs at all.
GAME_CLIENT_API void install_memory_audit(memory_audit::memory_audit_state_t *state);
// Same shape, and needed for the same reason: the frame_report handler lives in
// this DLL, so this DLL needs its own pointer at the launcher's one state.
GAME_CLIENT_API void install_frame_timing(frame_timing::frame_timing_state_t *state);

// `cvar_state`, `command_table` and `asset_state` are owned by the LAUNCHER and
// outlive the client module. Init stashes the first two on the client context
// and calls cvars::bind_client_commands(*command_table), which fills the
// @Client handler slots with symbols that live in this DLL.

// `asset_state` exists for the same reason cvar_state does: game_shared is a
// static lib, so this DLL has its OWN copy of the asset accessors' state
// pointer. Init points it at the launcher's one state via assets::set_state.
// Without that, every get_mesh here resolved against an empty manifest the
// launcher's assets::init() never filled -- see the ownership note in asset.hpp.

GAME_CLIENT_API bool init(cvars::cvar_state_t *cvar_state,
                          cvars::command_table_t *command_table,
                          assets::asset_state_t *asset_state);
GAME_CLIENT_API bool Tick(); // Returns false if should quit
GAME_CLIENT_API void shutdown();

// Integrated build only: hand the client a pointer to the server's session
// so the renderer can read entity pools directly. Pass nullptr in
// dedicated/networked builds (this is also the default).
GAME_CLIENT_API void set_integrated_server_session(const shared::game_session_t *session);

// Integrated build only: install a callback the editor invokes after writing a
// map to disk (Save / play). The launcher wires this to server::change_map_to so
// the running server picks up the new map without restarting the executable.
// Pass nullptr (the default) in dedicated/networked builds — the editor will
// then skip the server-side reload step.
using server_map_reload_hook_t = bool (*)(const char *map_path);
GAME_CLIENT_API void set_server_map_reload_hook(server_map_reload_hook_t hook);
} // namespace client
