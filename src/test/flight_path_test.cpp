// The pin for guided_rocket_plan.md step 2: which positions of a flight become vertices, and the boxes
// the edges between them become.
#include "flight_path.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using linalg::vec3f;

static int failure_count = 0;

static void check(bool condition, const char* what)
{
  printf(condition ? "  ok   %s\n" : "  FAIL %s\n", what);
  if (!condition)
    ++failure_count;
}

static bool near(float a, float b, float tolerance = 1e-3f)
{
  return std::fabs(a - b) <= tolerance;
}

static bool near(const vec3f& a, const vec3f& b, float tolerance = 1e-3f)
{
  return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}

constexpr shared::flight_path_settings_t SETTINGS = {.turn_degrees     = 10.f,
                                                     .shortest_segment = 32.f,
                                                     .longest_segment  = 192.f,
                                                     .joint_overlap    = 8.f,
                                                     .drop             = 68.f};
constexpr float TRAVEL_PER_TICK = 10.f;

int main()
{
  printf("[pin] a straight flight lays a vertex every longest_segment and one at its end\n");
  {
    const vec3f         heading = {1.f, 0.f, 0.f};
    shared::flight_path_t path  = shared::begin_flight_path({0.f, 100.f, 0.f}, heading);
    vec3f               rocket  = {0.f, 100.f, 0.f};
    for (int tick = 0; tick < 50; ++tick)
    {
      rocket = rocket + heading * TRAVEL_PER_TICK;
      shared::record_flight_path(path, SETTINGS, rocket, heading);
    }
    shared::end_flight_path(path, rocket);

    check(path.vertices.size() == 4, "500 units is the start, two vertices 200 apart and the end");
    check(near(path.vertices[1], {200.f, 100.f, 0.f}), "the first at the first tick past 192");
    check(near(path.vertices.back(), rocket), "the last where the flight ended");

    const std::vector<shared::flight_path_segment_t> segments =
        shared::segments_of_flight_path(path.vertices, SETTINGS);
    check(segments.size() == 3, "one segment per edge");
    check(near(segments[0].start, {0.f, 32.f, 0.f}), "set down the drop below the rocket's line");
    check(near(segments[0].length, 208.f), "as long as its edge and the joint overlap");
    check(near(linalg::forward(segments[0].orientation), heading), "facing along its edge");
  }

  printf("[pin] a turn lays a vertex, but never closer than shortest_segment to the last\n");
  {
    const vec3f         east  = {1.f, 0.f, 0.f};
    const vec3f         north = {0.f, 0.f, 1.f};
    shared::flight_path_t path = shared::begin_flight_path({0.f, 0.f, 0.f}, east);
    vec3f               rocket = {0.f, 0.f, 0.f};

    rocket = rocket + north * TRAVEL_PER_TICK;
    shared::record_flight_path(path, SETTINGS, rocket, north);
    check(path.vertices.size() == 1, "a turn 10 units out is too close to be a vertex");

    for (int tick = 0; tick < 3; ++tick)
    {
      rocket = rocket + north * TRAVEL_PER_TICK;
      shared::record_flight_path(path, SETTINGS, rocket, north);
    }
    check(path.vertices.size() == 2, "the same turn is one once the rocket is 32 out");
    check(near(path.vertices[1], {0.f, 0.f, 40.f}), "at the rocket");

    for (int tick = 0; tick < 5; ++tick)
    {
      rocket = rocket + north * TRAVEL_PER_TICK;
      shared::record_flight_path(path, SETTINGS, rocket, north);
    }
    check(path.vertices.size() == 2, "and a heading that holds lays nothing until the segment runs long");
  }

  printf("[pin] a climb is a ramp and a flight that went nowhere is no path\n");
  {
    const std::vector<vec3f> climb = {{0.f, 0.f, 0.f}, {100.f, 100.f, 0.f}};
    const std::vector<shared::flight_path_segment_t> segments =
        shared::segments_of_flight_path(climb, SETTINGS);
    check(segments.size() == 1, "one edge, one segment");
    check(near(linalg::forward(segments[0].orientation), linalg::normalize(vec3f{1.f, 1.f, 0.f})),
          "pitched along the climb");

    shared::flight_path_t still = shared::begin_flight_path({5.f, 5.f, 5.f}, {1.f, 0.f, 0.f});
    shared::end_flight_path(still, {5.f, 5.f, 5.f});
    check(shared::segments_of_flight_path(still.vertices, SETTINGS).empty(),
          "a flight that ended where it began leaves nothing");
  }

  printf(failure_count == 0 ? "\nflight_path_test: all passed\n" : "\nflight_path_test: %d FAILED\n",
         failure_count);
  return failure_count == 0 ? 0 : 1;
}
