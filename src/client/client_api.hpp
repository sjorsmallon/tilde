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

namespace client {
GAME_CLIENT_API bool Init();
GAME_CLIENT_API bool Tick(); // Returns false if should quit
GAME_CLIENT_API void Shutdown();
} // namespace client
