#pragma once

// The path a piloted rocket leaves (guided_rocket_plan.md step 2): a polyline recorded while it flies,
// and the oriented boxes its edges become when the flight ends.

#include "linalg.hpp"
#include "span.hpp"

#include <vector>

namespace shared
{

struct flight_path_settings_t
{
  // A vertex is laid when the heading has turned this far since the last one,
  float turn_degrees;
  // but never closer to it than this,
  float shortest_segment;
  // and always once the rocket is this far from it.
  float longest_segment;
  // How far each segment reaches past its end, so a bend leaves no gap on its outside.
  float joint_overlap;
  // How far below the rocket's line the segments lie.
  float drop;
  // Half of how wide each segment is, across its edge.
  float half_width;
  // The colour map (color_map.hpp) the path runs through, from its first segment to its last.
  Span<const linalg::vec3f> colors;
};

struct flight_path_t
{
  std::vector<linalg::vec3f> vertices;
  linalg::vec3f              heading_at_last_vertex;
};

struct flight_path_segment_t
{
  linalg::vec3f start;
  linalg::quatf orientation;
  float         length;
  float         half_width;
  linalg::vec3f color;
};

[[nodiscard]] flight_path_t begin_flight_path(const linalg::vec3f& position, const linalg::vec3f& heading);

// Once per tick while the flight is live; `heading` is normalized.
void record_flight_path(flight_path_t& path, const flight_path_settings_t& settings,
                        const linalg::vec3f& position, const linalg::vec3f& heading);

// The flight's last position is always a vertex.
void end_flight_path(flight_path_t& path, const linalg::vec3f& position);

// One segment per edge longer than nothing, in flight order.
[[nodiscard]] std::vector<flight_path_segment_t>
segments_of_flight_path(Span<const linalg::vec3f> vertices, const flight_path_settings_t& settings);

} // namespace shared
