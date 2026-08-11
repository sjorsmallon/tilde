// Array<T, N> / Enum_Array<Enum_T, T> — src/shared/array.hpp.
//
// Most of what these types promise is a COMPILE-time promise, so most of this
// file is static_assert and the runtime half is short on purpose. If it builds,
// the constexpr story holds; the asserts below cover the parts that only show
// up when you actually index and iterate.

#include "shared/array.hpp"
#include "shared/span.hpp"

#include <cassert>
#include <cstdio>
#include <type_traits>

// --- Array ------------------------------------------------------------------

constexpr Array<int, 3> NUMBERS = {{10, 20, 30}};

static_assert(NUMBERS.size() == 3);
static_assert(NUMBERS[0] == 10 && NUMBERS[2] == 30);
static_assert(NUMBERS.front() == 10 && NUMBERS.back() == 30);

// The whole reason this is an aggregate over T[N] and nothing else.
static_assert(std::is_trivially_copyable_v<Array<int, 3>>);
static_assert(std::is_aggregate_v<Array<int, 3>>);
static_assert(sizeof(Array<int, 3>) == sizeof(int[3]), "Array must not add a byte");

// uint32_t indexing, not size_t: the point of not using std::array.
static_assert(std::is_same_v<decltype(NUMBERS.size()), uint32_t>);

// Trivial copyability TRACKS the element type rather than being claimed.
struct non_trivial_t
{
  non_trivial_t() {}
  non_trivial_t(const non_trivial_t&) { }
};
static_assert(!std::is_trivially_copyable_v<Array<non_trivial_t, 2>>);

// Deduction guide.
static_assert(std::is_same_v<decltype(Array{1, 2, 3}), Array<int, 3>>);

constexpr int sum_of(Span<const int> values)
{
  int total = 0;
  for (int value : values)
    total += value;
  return total;
}

// Implicit conversion to Span, in a constant expression.
static_assert(sum_of(NUMBERS) == 60);

// --- Enum_Array -------------------------------------------------------------
//
// A hand-written enum specializing enum_traits. The generated enums get the
// same specialization from def_gen -- which is why every enum in the codebase
// that keys an Enum_Array is declared in a .def today, `Aim_Pose` included.
// This one stays hand-written so the test does not depend on the generator.

enum class colour_t : uint8_t
{
  Red,
  Green,
  Blue
};

template <> struct enum_traits<colour_t>
{
  static constexpr uint32_t count = 3;
};

struct colour_row_t
{
  colour_t    colour;
  const char* name;
  int         weight;
};

constexpr Enum_Array<colour_t, colour_row_t> COLOURS = {{
    {.colour = colour_t::Red, .name = "red", .weight = 1},
    {.colour = colour_t::Green, .name = "green", .weight = 2},
    {.colour = colour_t::Blue, .name = "blue", .weight = 4},
}};

static_assert(COLOURS.size() == 3);
static_assert(COLOURS[colour_t::Blue].weight == 4, "indexes by the enum, no cast");
static_assert(std::is_trivially_copyable_v<decltype(COLOURS)>);
static_assert(sizeof(COLOURS) == sizeof(colour_row_t[3]));

// The order check. Rows carry their own key, so the table is compared against
// itself: this is what a reorder trips.
static_assert(rows_in_enum_order<&colour_row_t::colour>(COLOURS),
              "COLOURS rows are not in colour_t order");

// And it must actually FAIL on a reorder, or the assert above proves nothing.
constexpr Enum_Array<colour_t, colour_row_t> SHUFFLED = {{
    {.colour = colour_t::Green, .name = "green", .weight = 2},
    {.colour = colour_t::Red, .name = "red", .weight = 1},
    {.colour = colour_t::Blue, .name = "blue", .weight = 4},
}};
static_assert(!rows_in_enum_order<&colour_row_t::colour>(SHUFFLED));

// The SHORT LIST, which is the case that motivates the check as much as a
// reorder does and is easy to assume the type handles. It does not: a short
// initializer value-initializes the tail like any aggregate, so the table
// compiles with a zeroed row instead of failing. Pinned here so that stays a
// known property rather than a discovery.
constexpr Enum_Array<colour_t, colour_row_t> SHORT = {{
    {.colour = colour_t::Red, .name = "red", .weight = 1},
    {.colour = colour_t::Green, .name = "green", .weight = 2},
}};
static_assert(SHORT.size() == 3, "storage is still enum-sized");
static_assert(SHORT[colour_t::Blue].name == nullptr, "the missing row is zeroed, not absent");
static_assert(!rows_in_enum_order<&colour_row_t::colour>(SHORT),
              "the order check is what catches a short list -- if this ever passes, "
              "every enum-indexed data table lost its only guard against a new enum "
              "value arriving as a zeroed row");

// try_get: in range yields the row, out of range yields nullptr rather than an
// out-of-bounds read. The cast is the point -- this is what an enum off the
// wire looks like.
static_assert(COLOURS.try_get(colour_t::Green) == &COLOURS.values[1]);
static_assert(COLOURS.try_get((colour_t)3) == nullptr);
static_assert(COLOURS.try_get((colour_t)200) == nullptr);

// Default-initialization ZEROES, unlike the raw `T buffer[N]` these replace.
// aim_pose_clips_t leans on this: `assets::aim_pose_clips_t clips;` must give
// five null pointers, not five indeterminate ones.
constexpr bool default_is_zeroed()
{
  Array<int, 3>             numbers;
  Enum_Array<colour_t, int> tally;
  return numbers[0] == 0 && numbers[2] == 0 && tally[colour_t::Blue] == 0;
}
static_assert(default_is_zeroed());

// ...and the default initializer does not cost the aggregate anything.
static_assert(std::is_aggregate_v<Enum_Array<colour_t, int>>);
static_assert(std::is_trivially_copyable_v<Enum_Array<colour_t, int>>);

void test_runtime()
{
  Array<int, 4> counters;
  for (int& counter : counters)
    assert(counter == 0);

  counters[2] = 7;
  assert(counters[2] == 7);
  assert(counters.back() == 0);

  // Mutable Span view over an owning Array, written through.
  Span<int> view = counters;
  assert(view.size() == 4);
  view[3] = 9;
  assert(counters[3] == 9);

  Enum_Array<colour_t, int> tally = {};
  tally[colour_t::Red] += 5;
  assert(tally[colour_t::Red] == 5);
  assert(tally[colour_t::Blue] == 0);

  int* green = tally.try_get(colour_t::Green);
  assert(green != nullptr);
  *green = 3;
  assert(tally[colour_t::Green] == 3);

  // The wire case: a byte that is not a valid colour.
  assert(tally.try_get((colour_t)9) == nullptr);

  int total = 0;
  for (int value : tally)
    total += value;
  assert(total == 8);

  std::printf("test_runtime passed\n");
}

int main()
{
  test_runtime();
  std::printf("array_test passed\n");
  return 0;
}
