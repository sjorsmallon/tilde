#pragma once

#include "mesh.hpp"

#include <optional>

namespace shared
{

// The half the plane's normal points into, and the other one. The caps are
// the last `cap_face_count` faces of each half, one per loop of the cut. The
// source lists are parallel to each half's faces: the input face a face came
// from, whole or clipped, or MESH_SPLIT_CAP for a cap.
inline constexpr uint32_t MESH_SPLIT_CAP = UINT32_MAX;

struct mesh_split_t
{
  mesh_geometry_t front;
  mesh_geometry_t back;
  uint32_t cap_face_count;
  std::vector<uint32_t> front_source_faces;
  std::vector<uint32_t> back_source_faces;
};

// Cut a closed planar mesh by a plane in the mesh's LOCAL space. A face wholly
// on one side goes there whole, surface and all; a face the plane crosses is
// clipped into both halves, each part keeping the surface; a face lying ON the
// plane goes to the half it bounds. The cut is then closed by one cap face per
// loop of edges left unpaired, taking `cap_surface`. Both halves share the
// crossing points bit for bit. A subdivided face the plane crosses comes out
// FLAT on both sides: its grid was a patch over the quad it had, and a part of
// that quad is a different face (count_faces_crossed_by_plane lets a tool
// refuse first). Refused, and nothing returned, when no vertex lies strictly
// on each side of the plane; a half may come back as more than one shell
// (separate_mesh_shells). geometry_def.md §11.3; the clip tool and the
// decomposition both cut through this.
[[nodiscard]] std::optional<mesh_split_t>
try_split_mesh_by_plane(const mesh_geometry_t &mesh, const Plane &plane,
                        const face_surface_t &cap_surface);

// How many faces have vertices strictly on both sides of the plane.
uint32_t count_faces_crossed_by_plane(const mesh_geometry_t &mesh, const Plane &plane);

// How many of those carry a grid the split would flatten.
uint32_t count_subdivided_faces_crossed_by_plane(const mesh_geometry_t &mesh,
                                                 const Plane &plane);

} // namespace shared
