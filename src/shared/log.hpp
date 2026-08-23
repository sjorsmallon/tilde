#pragma once

#include <format>
#include <print>
#include <source_location>
#include <string>
#include <string_view>
#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#include <crtdbg.h> // _set_abort_behavior: fatal_error must not open a dialog
#endif
#include <memory>
#include <type_traits>
#include <typeinfo>

// --- FIX 1: Guard cxxabi.h ---
#if (defined(__GNUC__) || defined(__clang__)) && !defined(_WIN32)
    #include <cxxabi.h>
#endif

// Check for stacktrace support (C++23)
#if __has_include(<stacktrace>)
    #include <stacktrace>
    #define HAS_STACKTRACE 1
#else
    #define HAS_STACKTRACE 0
#endif

namespace logging
{
namespace detail
{

// --- Die without a dialog ----------------------------------------------------
//
// On Windows both ways this codebase dies open a MODAL BOX by default: abort()
// pops the CRT's "abnormal program termination" window, and a failed assert()
// pops _CrtDbgReport's. Both BLOCK, so a failing test under ctest is not a
// failing test, it is a hung one waiting for somebody to click OK -- which is
// why one broken asset made the suite take four minutes instead of two seconds.
// The reason is already on stderr in every case; a box adds nothing.
//
// An inline variable rather than a call every main() has to remember, and a
// HEADER rather than a TU in game_shared: a static initializer in a static lib
// gets dropped by the linker when nothing references its object file, which
// this codebase has already been bitten by once. Every TU that can die includes
// log.hpp, so this runs.
inline const int crash_dialogs_disabled = []
{
#ifdef _WIN32
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
  // assert() goes through _CrtDbgReport, whose default for a console app is the
  // window. Send all three report kinds to stderr instead, where the rest of
  // the failure already is.
  for (int report_kind : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT})
  {
    _CrtSetReportMode(report_kind, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(report_kind, _CRTDBG_FILE_STDERR);
  }
#endif
#endif
  return 0;
}();

// --- FIX 2: Cross-platform Demangling ---
template <typename T>
std::string demangle_type_name() {
#if (defined(__GNUC__) || defined(__clang__)) && !defined(_WIN32)
    int status = 0;
    std::unique_ptr<char, void (*)(void *)> res {
        abi::__cxa_demangle(typeid(T).name(), nullptr, nullptr, &status),
        std::free
    };
    return (status == 0) ? res.get() : typeid(T).name();
#else
    // MSVC or Windows already returns a human-readable string from typeid.name()
    return typeid(T).name();
#endif
}

template <typename... Args>
void log_terminal_fmt(const std::source_location &loc,
                      std::format_string<Args...> fmt, Args &&...args)
{
  std::println(stdout, "[{}:{}] {}", loc.file_name(), loc.line(),
               std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void log_dispatch(const std::source_location &loc, const char *name,
                  std::format_string<Args...> fmt, Args &&...args)
{
  std::println(stdout, "[{}:{}] {}", loc.file_name(), loc.line(),
               std::format(fmt, std::forward<Args>(args)...));
}

inline void log_dispatch(const std::source_location &loc, const char *name,
                         const char *msg)
{
  std::println(stdout, "[{}:{}] {}", loc.file_name(), loc.line(), msg);
}

template <typename T>
void log_dispatch(const std::source_location &loc, const char *name, T &&val)
{
  std::println(stdout, "[{}:{}] {}: ({}): {}", loc.file_name(), loc.line(),
               name, demangle_type_name<std::decay_t<T>>(), val);
}

template <typename... Args>
void log_error_impl(const std::source_location &loc,
                    std::format_string<Args...> fmt, Args &&...args)
{
  std::println(stderr, "\033[1;31m[ERROR] [{}:{}] {}\033[0m", loc.file_name(),
               loc.line(), std::format(fmt, std::forward<Args>(args)...));

#if HAS_STACKTRACE
  std::println(stderr, "Stacktrace:\n{}",
               std::to_string(std::stacktrace::current()));
#else
  std::println(stderr, "Stacktrace: (Not supported by this compiler/standard)");
#endif
}

// For failures with no recovery: a missing asset the game cannot run without, a
// buffer the caller sized wrong. Logs like log_error and then aborts, so the
// crash carries the reason instead of arriving as a bare access violation.
//
// Deliberately not assert(): this stays live in release, where a broken install
// is exactly as unrecoverable as it is in a debug build.
template <typename... Args>
[[noreturn]] void fatal_error_impl(const std::source_location &loc,
                                   std::format_string<Args...> fmt, Args &&...args)
{
  std::println(stderr, "\033[1;31m[FATAL] [{}:{}] {}\033[0m", loc.file_name(),
               loc.line(), std::format(fmt, std::forward<Args>(args)...));

#if HAS_STACKTRACE
  std::println(stderr, "Stacktrace:\n{}",
               std::to_string(std::stacktrace::current()));
#else
  std::println(stderr, "Stacktrace: (Not supported by this compiler/standard)");
#endif

  std::fflush(stderr);
  std::abort();
}

template <typename... Args>
void log_warning_impl(const std::source_location &loc,
                      std::format_string<Args...> fmt, Args &&...args)
{
  std::println(stdout, "\033[1;33m[WARNING] [{}:{}] {}\033[0m", loc.file_name(),
               loc.line(), std::format(fmt, std::forward<Args>(args)...));
}

} // namespace detail
} // namespace logging

#define log_terminal(first, ...)                                               \
  ::logging::detail::log_dispatch(std::source_location::current(), #first,     \
                                  first, ##__VA_ARGS__)

#define log_error(fmt, ...)                                                    \
  ::logging::detail::log_error_impl(std::source_location::current(), fmt,      \
                                    ##__VA_ARGS__)

#define log_warning(fmt, ...)                                                  \
  ::logging::detail::log_warning_impl(std::source_location::current(), fmt,    \
                                      ##__VA_ARGS__)

#define fatal_error(fmt, ...)                                                  \
  ::logging::detail::fatal_error_impl(std::source_location::current(), fmt,    \
                                      ##__VA_ARGS__)