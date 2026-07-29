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

namespace client {
// `cvar_state` and `command_table` are owned by the LAUNCHER and outlive the
// client module. Init stashes both on the client context and calls
// cvars::bind_client_commands(*command_table), which fills the @Client handler
// slots with symbols that live in this DLL. Neither may be null.
GAME_CLIENT_API bool Init(cvars::cvar_state_t *cvar_state,
                          cvars::command_table_t *command_table);
GAME_CLIENT_API bool Tick(); // Returns false if should quit
GAME_CLIENT_API void Shutdown();

// Integrated build only: hand the client a pointer to the server's session
// so the renderer can read entity pools directly. Pass nullptr in
// dedicated/networked builds (this is also the default).
GAME_CLIENT_API void set_integrated_server_session(const shared::game_session_t *session);

// Integrated build only: install a callback the editor invokes after writing a
// map to disk (Save / Play). The launcher wires this to server::reload_map so
// the running server picks up the new map without restarting the executable.
// Pass nullptr (the default) in dedicated/networked builds — the editor will
// then skip the server-side reload step.
using server_map_reload_hook_t = bool (*)(const char *map_path);
GAME_CLIENT_API void set_server_map_reload_hook(server_map_reload_hook_t hook);
} // namespace client
