#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <new>
#include <thread>
#include <vector>

// Helper for cache line size
#ifdef __cpp_lib_hardware_interference_size
constexpr size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size;
#else
constexpr size_t CACHE_LINE_SIZE = 64;
#endif

// Lock-free MPMC ring buffer
template <typename T, size_t Capacity> class Ring_Buffer {
  static_assert((Capacity & (Capacity - 1)) == 0,
                "Capacity must be power of 2");

public:
  Ring_Buffer() {
    for (size_t i = 0; i < Capacity; ++i) {
      buffer_[i].sequence.store(i, std::memory_order_relaxed);
    }
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
  }

  bool push(T const &data) {
    size_t head = head_.load(std::memory_order_relaxed);
    for (;;) {
      auto &slot = buffer_[head & (Capacity - 1)];
      size_t seq = slot.sequence.load(std::memory_order_acquire);
      intptr_t diff = (intptr_t)seq - (intptr_t)head;

      if (diff == 0) {
        if (head_.compare_exchange_weak(head, head + 1,
                                        std::memory_order_relaxed)) {
          slot.data = data;
          slot.sequence.store(head + 1, std::memory_order_release);
          return true;
        }
      } else if (diff < 0) {
        // Full
        return false;
      } else {
        head = head_.load(std::memory_order_relaxed);
      }
    }
  }

  bool pop(T &data) {
    size_t tail = tail_.load(std::memory_order_relaxed);
    for (;;) {
      auto &slot = buffer_[tail & (Capacity - 1)];
      size_t seq = slot.sequence.load(std::memory_order_acquire);
      intptr_t diff = (intptr_t)seq - (intptr_t)(tail + 1);

      if (diff == 0) {
        if (tail_.compare_exchange_weak(tail, tail + 1,
                                        std::memory_order_relaxed)) {
          data = slot.data; // Move if T is movable? std::function is movable.
          slot.sequence.store(tail + Capacity, std::memory_order_release);
          return true;
        }
      } else if (diff < 0) {
        // Empty
        return false;
      } else {
        tail = tail_.load(std::memory_order_relaxed);
      }
    }
  }

private:
  struct Slot {
    std::atomic<size_t> sequence;
    T data;
  };

  struct alignas(CACHE_LINE_SIZE) Aligned_Head {
    std::atomic<size_t> val;
  };
  struct alignas(CACHE_LINE_SIZE) Aligned_Tail {
    std::atomic<size_t> val;
  };

  Slot buffer_[Capacity];
  std::atomic<size_t> head_;                                 // Producer index
  char pad1_[CACHE_LINE_SIZE - sizeof(std::atomic<size_t>)]; // Padding
  std::atomic<size_t> tail_;                                 // Consumer index
  char pad2_[CACHE_LINE_SIZE - sizeof(std::atomic<size_t>)]; // Padding
};

class Task_System {
public:
  Task_System();
  ~Task_System();

  void initialize();
  void shutdown();

  void submit(std::function<void()> task);

private:
  void worker_thread_func(size_t thread_index);

  static constexpr size_t QUEUE_SIZE = 32; // Must be power of 2

  // One queue per worker
  using Queue_Type = Ring_Buffer<std::function<void()>, QUEUE_SIZE>;
  std::vector<std::unique_ptr<Queue_Type>> queues_;

  std::vector<std::thread> workers_;
  std::atomic<bool> running_{false};

  // For round-robin submission
  std::atomic<size_t> submit_index_{0};
};
