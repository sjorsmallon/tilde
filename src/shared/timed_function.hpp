#pragma once

#include "log.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <numeric>
#include <source_location>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace timing {

constexpr size_t MOVING_AVERAGE_WINDOW = 5;

struct FunctionStats {
  explicit FunctionStats(std::string_view n) : name(n), count(0) {
    for (auto &s : samples)
      s.store(0.0);
  }

  std::string name;
  std::array<std::atomic<double>, MOVING_AVERAGE_WINDOW> samples;
  std::atomic<size_t> count;

  // Lock-free record
  void Record(double duration_ms) {
    // monotonic unique index
    size_t idx = count.fetch_add(1, std::memory_order_relaxed);
    // write to modulo slot
    samples[idx % MOVING_AVERAGE_WINDOW].store(duration_ms,
                                               std::memory_order_relaxed);
  }
};

class Registry {
public:
  static Registry &Get() {
    static Registry instance;
    return instance;
  }

  // Thread-safe, locks only on new insertion
  FunctionStats *GetOrRegister(const std::source_location &loc) {
    // Construct a unique key (could use loc.file_name() + loc.line() or just
    // function_name) Using function_name implies aggregation by function
    // signature.
    std::string key = loc.function_name();

    // Double-checked locking optimization could be done here if we stored
    // pointers in atomic map, but for simplicity and "lock-free recording", a
    // mutex on registration is acceptable since it happens once per callsite.

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = stats_.find(key);
    if (it != stats_.end()) {
      return it->second.get();
    }

    // Create new
    auto *stats = new FunctionStats(key);
    stats_[key].reset(stats);
    return stats;
  }

  void LogStats() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<FunctionStats *> sorted_stats;
    sorted_stats.reserve(stats_.size());
    for (const auto &[_, stat] : stats_) {
      sorted_stats.push_back(stat.get());
    }
    std::sort(
        sorted_stats.begin(), sorted_stats.end(),
        [](FunctionStats *a, FunctionStats *b) { return a->name < b->name; });

    log_terminal("\n--- Performance Stats (Last 5 runs moving avg) ---");

    for (const auto *stat : sorted_stats)
    {
      double sum = 0.0;
      
      
      size_t count = stat->count.load(std::memory_order_relaxed);

      size_t num_samples = std::min(count, MOVING_AVERAGE_WINDOW);

      // Read latest samples (might be slightly tearing if actively writing, but
      // acceptable for profiling) We go backwards from current count to get
      // "latest" even if wrapping wrapped multiple times? Actually simply
      // iterating the array is approximation enough since it's a moving window.
      // But for exact "last 5", we should look at indices: (count-1) down to
      // (count-5).

      size_t samples_accumulated = 0;
      if (count> 0) {
        // Careful with unsigned underflow logic, simplified loop:
        for (size_t i = 0; i < num_samples; ++i)
        {
          // Index in logic: c - 1 - i
          // Index in array: (c - 1 - i) % WINDOW
          size_t logical_idx = count - 1 - i;
          sum += stat->samples[logical_idx % MOVING_AVERAGE_WINDOW].load(
              std::memory_order_relaxed);
          samples_accumulated++;
        }
      }

      double avg =
          (samples_accumulated > 0) ? (sum / samples_accumulated) : 0.0;
      log_terminal("[{}] Avg: {:.4f} ms (Calls: {})", stat->name, avg, count);
    }
    log_terminal("--------------------------------------------------\n");
  }

private:
  // Using unique_ptr to ensure pointer stability (though map iterators
  // invalidation doesn't affect values, rehash does)
  std::unordered_map<std::string, std::unique_ptr<FunctionStats>> stats_;
  std::mutex mutex_;
};

class ScopedTimer {
public:
  ScopedTimer(FunctionStats *stats) : stats_(stats) {
    start_ = std::chrono::high_resolution_clock::now();
  }

  ~ScopedTimer() {
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> diff = end - start_;
    if (stats_) {
      stats_->Record(diff.count());
    }
  }

private:
  FunctionStats *stats_;
  std::chrono::time_point<std::chrono::high_resolution_clock> start_;
};

} // namespace timing

// Macro usage:
// 1. Declare a static pointer to the stats struct. Initialized ONCE
// (thread-safe static init).
// 2. Instantiate the scoped timer with that pointer.
#define TOKENPASTE(x, y) x##y
#define TOKENPASTE2(x, y) TOKENPASTE(x, y)
#define UNIQUE_TIMER_NAME TOKENPASTE2(_scoped_timer_, __LINE__)
#define UNIQUE_STATS_NAME TOKENPASTE2(_func_stats_, __LINE__)

#define timed_function()                                                       \
  static ::timing::FunctionStats *UNIQUE_STATS_NAME =                          \
      ::timing::Registry::Get().GetOrRegister(                                 \
          std::source_location::current());                                    \
  ::timing::ScopedTimer UNIQUE_TIMER_NAME(UNIQUE_STATS_NAME)

#define print_timing_stats() ::timing::Registry::Get().LogStats()
