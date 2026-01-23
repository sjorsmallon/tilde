#pragma once

#include <format>
#include <print>
#include <source_location>
#include <string_view>

// Check for stacktrace support
#if __has_include(<stacktrace>)
#include <stacktrace>
#define HAS_STACKTRACE 1
#else
#define HAS_STACKTRACE 0
#endif

namespace logging {

// Implementation details not meant to be called directly
namespace detail {

template <typename... Args>
void log_terminal_impl(const std::source_location &loc,
                       std::format_string<Args...> fmt, Args &&...args) {
  // Format: [FILE:LINE] Message
  std::println(stdout, "[{}:{}] {}", loc.file_name(), loc.line(),
               std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args>
void log_error_impl(const std::source_location &loc,
                    std::format_string<Args...> fmt, Args &&...args) {
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

} // namespace detail
} // namespace logging

// Macros to automatically capture source_location
// We use a lambda to enforce type checking on the format string while
// forwarding args

#define log_terminal(fmt, ...)                                                 \
  ::logging::detail::log_terminal_impl(std::source_location::current(), fmt,   \
                                       ##__VA_ARGS__)

#define log_error(fmt, ...)                                                    \
  ::logging::detail::log_error_impl(std::source_location::current(), fmt,      \
                                    ##__VA_ARGS__)
