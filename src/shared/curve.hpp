#pragma once

#include "linalg.hpp"
#include "span.hpp"

#include <vector>

namespace shared
{

// The centripetal Catmull-Rom spline through every control point, sampled
// `samples_per_segment` times per segment plus the last point, so the result
// holds (points - 1) * samples_per_segment + 1 positions and the first sample
// of every segment IS its control point. The missing neighbour at each end is
// the mirror of the one inside, so the curve leaves and arrives straight.
// Centripetal, so unevenly spaced points never overshoot or loop. Fewer than
// two points or no samples is a caller bug.
std::vector<linalg::vec3> sample_catmull_rom(Span<const linalg::vec3> control_points,
                                             int samples_per_segment);

} // namespace shared
