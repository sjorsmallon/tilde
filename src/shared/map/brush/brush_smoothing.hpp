#pragma once

#include "../map_geometry.hpp"
#include "brush.hpp"

#include <cstdint>
#include <vector>

namespace shared
{

// The consumer of face_surface_t::smoothing_group. A corner of a face in a
// non-zero group draws with the area-weighted average normal of every face at
// that position in the same group, in this brush and in every drawing brush
// of the neighbourhood whose bounds touch it; group zero keeps its plane.
// Collision and the bake read planes and never come here. geometry_def.md
// Track J.

// Per face of `polyhedron`, the group of the surface it resolves to; a face
// matching no stored surface is in group zero.
std::vector<int32_t> face_smoothing_groups(const brush_geometry_t &brush,
                                           const brush_polyhedron_t &polyhedron);

bool brush_has_smoothed_face(const brush_geometry_t &brush);

// One past the highest group any face of `geometry` is in, so a primitive's faces
// never smooth into a neighbour that was authored by hand.
int32_t next_free_smoothing_group(Span<const map_geometry_t> geometry);

// Another brush's faces as they take part in this brush's average.
struct smoothing_source_t
{
  brush_polyhedron_t polyhedron;
  std::vector<int32_t> groups;
};

// The drawing brushes of the neighbourhood other than `self` that have a face
// in one of `groups` and whose bounds touch `brush`'s. A hole draws nothing and
// its outward normals point into its wall's ring, so it is never a source.
std::vector<smoothing_source_t> collect_smoothing_sources(const brush_geometry_t &brush,
                                                          Span<const int32_t> groups,
                                                          brush_neighbourhood_t neighbourhood);

// Parallel to polyhedron.faces, each parallel to the face's vertex_indices.
std::vector<std::vector<linalg::vec3>>
compute_smoothed_corner_normals(const brush_polyhedron_t &polyhedron, Span<const int32_t> groups,
                                Span<const smoothing_source_t> sources);

} // namespace shared
