#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "../shared/rng.hpp"

// Simple test runner
#define TEST_ASSERT(cond)                                                      \
  if (!(cond)) {                                                               \
    std::cerr << "Assertion failed: " << #cond << " at " << __FILE__ << ":"    \
              << __LINE__ << std::endl;                                        \
    std::exit(1);                                                              \
  }

void test_determinism() {
  std::cout << "Testing RNG determinism..." << std::endl;

  game::seed_rng(12345);
  std::vector<uint64_t> sequence_a;
  for (int i = 0; i < 100; ++i) {
    sequence_a.push_back(game::random_uint64());
  }

  game::seed_rng(12345);
  std::vector<uint64_t> sequence_b;
  for (int i = 0; i < 100; ++i) {
    sequence_b.push_back(game::random_uint64());
  }

  TEST_ASSERT(sequence_a.size() == sequence_b.size());
  for (size_t i = 0; i < sequence_a.size(); ++i) {
    TEST_ASSERT(sequence_a[i] == sequence_b[i]);
  }

  std::cout << "Determinism passed." << std::endl;
}

void test_state_saving() {
  std::cout << "Testing RNG state saving/loading..." << std::endl;

  game::seed_rng(9876);
  // Burn some numbers
  for (int i = 0; i < 10; ++i)
    game::random_uint64();

  uint64_t saved_state = game::get_rng_state();
  uint64_t next_val = game::random_uint64();

  // Reset and restore
  game::seed_rng(123); // Different seed
  game::set_rng_state(saved_state);

  uint64_t restored_next_val = game::random_uint64();

  TEST_ASSERT(next_val == restored_next_val);
  std::cout << "State saving passed." << std::endl;
}

void test_float_range() {
  std::cout << "Testing float range..." << std::endl;
  game::seed_rng(42);
  for (int i = 0; i < 1000; ++i) {
    float f = game::random_float();
    TEST_ASSERT(f >= 0.0f && f <= 1.0f);
  }
  std::cout << "Float range passed." << std::endl;
}

int main() {
  test_determinism();
  test_state_saving();
  test_float_range();

  std::cout << "All RNG tests passed!" << std::endl;
  return 0;
}
