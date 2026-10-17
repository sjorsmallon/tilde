#pragma once

#include "../../aabb.hpp"
#include "../map_geometry.hpp"
#include "brush.hpp"

#include <optional>
#include <vector>

namespace shared
{

// The face of `hull` other than `face_index` whose loop holds both `a` and `b`
// (within BRUSH_WELD_EPSILON): the face across that edge. Empty when no other
// face holds both, which a closed hull never produces for one of its own edges.
[[nodiscard]] std::optional<size_t> try_find_face_across_edge(const brush_polyhedron_t &hull,
                                                              size_t face_index,
                                                              const linalg::vec3 &a,
                                                              const linalg::vec3 &b);

bool brush_has_subdivided_face(const brush_geometry_t &brush);

// A brush that may share a seam with the one being read, hulled once per read.
struct seam_candidate_t
{
  const brush_geometry_t *brush = nullptr;
  brush_polyhedron_t hull;
};

// Every brush of the neighbourhood other than its own that has a subdivided
// face and whose bounds meet `bounds`. A brush that does not hull is left out;
// wherever it is built it already says so.
std::vector<seam_candidate_t> collect_seam_candidates(brush_neighbourhood_t neighbourhood,
                                                      const aabb_bounds_t &bounds);

// The subdivision level of a candidate's face coplanar with `plane` whose loop
// holds both `a` and `b`: the face across the seam. Empty when no candidate has
// such a face, or the one it has is flat.
[[nodiscard]] std::optional<int>
try_find_subdivision_level_across_seam(Span<const seam_candidate_t> candidates,
                                       const Plane &plane, const linalg::vec3 &a,
                                       const linalg::vec3 &b);

} // namespace shared
