#pragma once

#include "map_geometry.hpp"

#include <optional>
#include <vector>

// The legacy readers (geometry_def.md §11.8 increment 6): a `brush` and a
// `static_mesh` are read once and become meshes before anything else sees the
// map, and the writer emits neither. Beside map_geometry.cpp, which is past
// its size.

namespace shared
{

// The same pose, the asset path and the scale as the source; no vertices and
// no faces, since the asset's are borrowed.
mesh_geometry_t convert_static_mesh_to_mesh(const static_mesh_geometry_t &static_mesh);

// The brush hulls its points one last time and its plane keys attach the
// stored surfaces one last time; the pose sits at the hull's centre. A wall
// with holes becomes its solid's boundary, the boolean baked in. A subdivided
// face keeps its grid as the weld READ it, re-anchored to the face's ring.
// Nothing, with a line naming `uid`, when the points do not hull or the
// result is not closed and planar.
[[nodiscard]] std::optional<mesh_geometry_t>
try_convert_brush_to_mesh(const brush_geometry_t &brush, entity_uid_t uid,
                          Span<const map_geometry_t> map_geometry);

struct legacy_geometry_report_t
{
  size_t brushes_converted = 0;
  size_t static_meshes_converted = 0;
  size_t holes_removed = 0;
  size_t objects_refused = 0;

  [[nodiscard]] bool converted_anything() const;
};

// Every brush and static mesh of `geometry` becomes a mesh under its own uid,
// in place. A hole brush is consumed by its wall's conversion and removed; an
// object that refuses to convert is removed with a line. A list of meshes
// comes back untouched.
legacy_geometry_report_t convert_legacy_geometry(std::vector<map_geometry_t> &geometry);

} // namespace shared
