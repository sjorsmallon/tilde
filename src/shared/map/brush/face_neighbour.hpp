#pragma once

#include "brush.hpp"
#include <optional>

namespace shared
{

// The face of `hull` other than `face_index` whose loop holds both `a` and `b`
// (within BRUSH_WELD_EPSILON): the face across that edge. Empty when no other
// face holds both, which a closed hull never produces for one of its own edges.
[[nodiscard]] std::optional<size_t> try_find_face_across_edge(const brush_polyhedron_t &hull,
                                                              size_t face_index,
                                                              const linalg::vec3 &a,
                                                              const linalg::vec3 &b);

} // namespace shared
