#pragma once

#include <cstdint>

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

namespace server {
GAME_SERVER_API bool Init();
GAME_SERVER_API bool Tick();
GAME_SERVER_API void Shutdown();
GAME_SERVER_API double get_tick_interval();
GAME_SERVER_API uint32_t get_tick_number();

// Integrated-mode only: returns a pointer to the server's authoritative session
// so the client can render directly from it instead of going through the
// snapshot/interpolation pipeline. Returns nullptr if Init() has not run.
GAME_SERVER_API const shared::game_session_t *get_session_for_integrated_client();
} // namespace server
