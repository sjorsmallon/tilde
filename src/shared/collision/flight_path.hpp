#pragma once

// The path a piloted rocket leaves (piloted_rocket_plan.md step 2): a polyline recorded while it flies,
// and the oriented boxes its edges become when the flight ends. The rule that lays the vertices is also
// how a trail (trail.hpp) lays its points.

#include "linalg.hpp"
#include "span.hpp"

#include <optional>
#include <vector>

namespace shared
{

// When a moving thing's position becomes the next vertex of its path.
struct flight_path_rule_t
{
  // A vertex is laid when the heading has turned this far since the last one,
  float turn_degrees;
  // but never closer to it than this,
  float shortest_segment;
  // and always once the thing is this far from it.
  float longest_segment;
};

struct flight_path_settings_t
{
  flight_path_rule_t rule;
  // How far each segment reaches past its end, so a bend leaves no gap on its outside.
  float joint_overlap;
  // How far below the rocket's line the segments lie.
  float drop;
  // Half of how wide each segment is, across its edge.
  float half_width;
  // How long each segment stays once it has grown.
  float solid_seconds;
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

// `position` as the next vertex after `last_vertex` when the rule says so, else empty. Both headings are
// normalized.
[[nodiscard]] std::optional<linalg::vec3f>
try_next_flight_path_vertex(const linalg::vec3f& last_vertex, const linalg::vec3f& heading_at_last_vertex,
                            const flight_path_rule_t& rule, const linalg::vec3f& position,
                            const linalg::vec3f& heading);

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
