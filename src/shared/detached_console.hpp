#pragma once

#ifdef _WIN32
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <windows.h>
#endif

namespace console {

inline void SpawnNew() {
#ifdef _WIN32
  // Detach from any existing console (e.g. if run from command line)
  // Note: This closes the connection to the calling terminal!
  FreeConsole();

  // Allocate a new console
  if (AllocConsole()) {
    FILE *fp;
    // Redirect std streams to the new console
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
    freopen_s(&fp, "CONIN$", "r", stdin);

    // Sync C++ streams
    std::cout.clear();
    std::cerr.clear();
    std::cin.clear();

    // Optional: Disable close button specific logic if needed,
    // but standard console behavior is usually desired.

    // Set title
    SetConsoleTitle(TEXT("Detached Console"));
  }
#elif defined(__APPLE__)
  // On macOS, creating a *new* Terminal window attached to this specific
  // process's stdout/stderr programmatically is complex and requires IPC or
  // relaunching.
  //
  // However, standard behavior for an executable double-clicked in Finder
  // is to open a new Terminal window automatically.
  //
  // If run from an existing Terminal, standard behavior is to output there.
  // To force a NEW window from CLI is non-standard.
  // We will leave this as a no-op for now unless we want to use AppleScript to
  // spawn 'tail'.
#endif
}

} // namespace console
