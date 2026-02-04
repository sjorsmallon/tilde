#pragma once

#include <format>
#include <print>
#include <source_location>
#include <string>
#include <string_view>
#include <cstdlib>
#include <memory>
#include <type_traits>
#include <typeinfo>

// --- FIX 1: Guard cxxabi.h ---
#if defined(__GNUC__) || defined(__clang__)
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

// --- FIX 2: Cross-platform Demangling ---
template <typename T>
std::string demangle_type_name() {
#if defined(__GNUC__) || defined(__clang__)
    int status = 0;
    std::unique_ptr<char, void (*)(void *)> res {
        abi::__cxa_demangle(typeid(T).name(), nullptr, nullptr, &status),
        std::free
    };
    return (status == 0) ? res.get() : typeid(T).name();
#else
    // MSVC already returns a human-readable string from typeid.name()
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