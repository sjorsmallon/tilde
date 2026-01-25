#include "shared/task_system.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <vector>

int main() {
  Task_System ts;
  ts.initialize();

  std::cout << "Task System initialized." << std::endl;

  constexpr int TASK_COUNT = 10000;
  std::atomic<int> counter{0};

  auto start_time = std::chrono::high_resolution_clock::now();

  for (int i = 0; i < TASK_COUNT; ++i) {
    ts.submit([&counter]() {
      counter.fetch_add(1, std::memory_order_relaxed);
      // Simulate some work?
      // std::this_thread::sleep_for(std::chrono::microseconds(1));
    });
  }

  // Wait for all tasks to complete
  while (counter.load(std::memory_order_relaxed) < TASK_COUNT) {
    std::this_thread::yield();
  }

  auto end_time = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
      end_time - start_time);

  std::cout << "Processed " << TASK_COUNT << " tasks in " << duration.count()
            << "ms." << std::endl;
  std::cout << "Final counter value: " << counter.load() << std::endl;

  assert(counter.load() == TASK_COUNT);

  ts.shutdown();
  std::cout << "Task System shutdown." << std::endl;

  return 0;
}
