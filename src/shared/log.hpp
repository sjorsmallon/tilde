#pragma once

#include <format>
#include <print>
#include <source_location>
#include <string>
#include <string_view>

#include <cstdlib>
#include <cxxabi.h>
#include <memory>
#include <type_traits>
#include <typeinfo>

// Check for stacktrace support
#if __has_include(<stacktrace>)
#include <stacktrace>
#define HAS_STACKTRACE 1
#else
#define HAS_STACKTRACE 0
#endif

namespace logging
{

// Implementation details not meant to be called directly
namespace detail
{

// Demangle type name using abi::__cxa_demangle
template <typename T> std::string demangle_type_name()
{
  int status = 0;
  std::unique_ptr<char, void (*)(void *)> res{
      abi::__cxa_demangle(typeid(T).name(), nullptr, nullptr, &status),
      std::free};
  return (status == 0) ? res.get() : typeid(T).name();
}

template <typename... Args>
void log_terminal_fmt(const std::source_location &loc,
                      std::format_string<Args...> fmt, Args &&...args)
{
  // Format: [FILE:LINE] Message
  std::println(stdout, "[{}:{}] {}", loc.file_name(), loc.line(),
               std::format(fmt, std::forward<Args>(args)...));
}

// Overload 1: Format string with arguments
template <typename... Args>
void log_dispatch(const std::source_location &loc, const char *name,
                  std::format_string<Args...> fmt, Args &&...args)
{
  std::println(stdout, "[{}:{}] {}", loc.file_name(), loc.line(),
               std::format(fmt, std::forward<Args>(args)...));
}

// Overload 2: C-string literal (treated as message)
// Non-template prefers over T&& template for literals (decay vs exact match,
// but non-template wins)
inline void log_dispatch(const std::source_location &loc, const char *name,
                         const char *msg)
{
  std::println(stdout, "[{}:{}] {}", loc.file_name(), loc.line(), msg);
}

// Overload 3: Variable dump (T&&)
template <typename T>
void log_dispatch(const std::source_location &loc, const char *name, T &&val)
{
  // Format: [FILE:LINE] name: (type): value
  std::println(stdout, "[{}:{}] {}: ({}): {}", loc.file_name(), loc.line(),
               name, demangle_type_name<std::decay_t<T>>(), val);
}

template <typename... Args>
void log_error_impl(const std::source_location &loc,
                    std::format_string<Args...> fmt, Args &&...args)
{
  // Format: [ERROR] [FILE:LINE] Message
  //         Stacktrace...
  std::println(stderr, "\033[1;31m[ERROR] [{}:{}] {}\033[0m", loc.file_name(),
               loc.line(), std::format(fmt, std::forward<Args>(args)...));

#if HAS_STACKTRACE
  std::println(stderr, "Stacktrace:\n{}",
               std::to_string(std::stacktrace::current()));
#else
  std::println(stderr, "Stacktrace: (Not supported by this compiler)");
#endif
}

template <typename... Args>
void log_warning_impl(const std::source_location &loc,
                      std::format_string<Args...> fmt, Args &&...args)
{
  // Format: [WARNING] [FILE:LINE] Message
  // ANSI Yellow: \033[1;33m
  std::println(stdout, "\033[1;33m[WARNING] [{}:{}] {}\033[0m", loc.file_name(),
               loc.line(), std::format(fmt, std::forward<Args>(args)...));
}

} // namespace detail
} // namespace logging

// Macros to automatically capture source_location
// We use a lambda to enforce type checking on the format string while
// forwarding args

// We use a variadic macro that passes the first argument as a stringified name
// AND as a value, plus the rest of the args. This allows us to have the
// variable name if it turns out to be a variable dump.
#define log_terminal(first, ...)                                               \
  ::logging::detail::log_dispatch(std::source_location::current(), #first,     \
                                  first, ##__VA_ARGS__)

#define log_error(fmt, ...)                                                    \
  ::logging::detail::log_error_impl(std::source_location::current(), fmt,      \
                                    ##__VA_ARGS__)

#define log_warning(fmt, ...)                                                  \
  ::logging::detail::log_warning_impl(std::source_location::current(), fmt,    \
                                      ##__VA_ARGS__)