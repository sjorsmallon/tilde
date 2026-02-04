#pragma once

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

namespace server {
GAME_SERVER_API bool Init();
GAME_SERVER_API bool Tick();
GAME_SERVER_API void Shutdown();
} // namespace server
