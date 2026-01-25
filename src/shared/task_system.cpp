#include "task_system.hpp"
#include "log.hpp" // For logging if needed
#include <thread>

Task_System::Task_System() {}

Task_System::~Task_System() { shutdown(); }

void Task_System::initialize() {
  if (running_)
    return;
  running_ = true;

  unsigned int core_count = std::thread::hardware_concurrency();
  if (core_count == 0)
    core_count = 4; // Fallback

  // Create one queue per worker
  workers_.reserve(core_count);
  queues_.reserve(core_count);
  for (unsigned int i = 0; i < core_count; ++i) {
    queues_.push_back(std::make_unique<Queue_Type>());
  }

  for (unsigned int i = 0; i < core_count; ++i) {
    workers_.emplace_back([this, i]() { worker_thread_func(i); });
  }

  // log_terminal("Task System initialized with {} threads", core_count);
}

void Task_System::shutdown() {
  if (!running_)
    return;
  running_ = false;
  for (auto &t : workers_) {
    if (t.joinable())
      t.join();
  }
  workers_.clear();
  queues_.clear();
}

void Task_System::submit(std::function<void()> task) {
  // Round-robin submission
  size_t start_index = submit_index_.fetch_add(1, std::memory_order_relaxed);

  // Try to push to queues starting from round-robin index
  // If full, try next queue? Or just spin on the target queue?
  // "Lock-free" implies we should probably try others if one is full, or just
  // spin. Given the fixed size ring buffer, spinning is safer than dropping.
  // However, to prevent one full queue effectively blocking submission while
  // others are empty, we could try others.

  // Simple approach: Spin on the target queue.
  size_t num_queues = queues_.size();
  if (num_queues == 0)
    return; // Should not happen if initialized

  size_t index = start_index % num_queues;
  while (!queues_[index]->push(task)) {
    std::this_thread::yield();
  }
}

void Task_System::worker_thread_func(size_t thread_index) {
  size_t num_queues = queues_.size();
  while (running_) {
    std::function<void()> task;

    // 1. Try local queue
    if (queues_[thread_index]->pop(task)) {
      task();
      continue;
    }

    // 2. Steal from others
    // Start from (index + 1) to spread contention?
    // Random victim is also popular. Let's do deterministic offset to avoid RNG
    // overhead for now. We will loop through all other queues.
    bool stole = false;
    for (size_t i = 1; i < num_queues; ++i) {
      // Calculate victim index: (thread_index + i) % num_queues
      size_t victim = (thread_index + i) % num_queues;
      if (queues_[victim]->pop(task)) {
        task();
        stole = true;
        break;
      }
    }

    if (!stole) {
      std::this_thread::yield();
    }
  }
}
