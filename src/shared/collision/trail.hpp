#pragma once

// The trail a player leaves (Trail_Entity): points laid from the owner's feet by the flight path rule, and a box
// standing on every edge between two points, `half_width` across the edge and `height` up from the line, which
// runs at the feet. The server hurts with these boxes (server/systems/trail_system.cpp) and the client draws them
// (client/drawn_entity.cpp), so what is seen is what hurts.

#include "../aabb.hpp"
#include "../linalg.hpp"
#include "../span.hpp"
#include "flight_path.hpp"

#include <optional>

namespace shared
{

struct trail_settings_t
{
  float half_width;
  float height;
};

// A point every longest_segment of straight walking, sooner on a turn, never closer than the shortest.
constexpr flight_path_rule_t TRAIL_RULE = {.turn_degrees = 15.f, .shortest_segment = 16.f, .longest_segment = 128.f};

// An edge shorter than this is no wall.
constexpr float SHORTEST_TRAIL_EDGE = 1.f;

// The box standing on the edge from `start` to `end`; empty for an edge shorter than SHORTEST_TRAIL_EDGE.
[[nodiscard]] std::optional<oriented_box_t> try_compute_trail_segment_box(const linalg::vec3f& start,
                                                                          const linalg::vec3f& end,
                                                                          const trail_settings_t& settings);

// Edge `index` of a trail runs from points[index] to points[index + 1], or to the owner's feet for the last one
// (the live edge). Walking back from the feet along the trail, the first edge at which `lag_distance` has been
// covered: that edge and every one after it are within the owner's lag and do not hurt them.
[[nodiscard]] uint32_t find_first_edge_within_lag_distance(Span<const linalg::vec3f> points,
                                                           const linalg::vec3f& owner_feet, float lag_distance);

// The direction of `vector`, or empty when it is too short to have one: a player standing still has no heading.
[[nodiscard]] std::optional<linalg::vec3f> try_compute_direction(const linalg::vec3f& vector);

} // namespace shared
