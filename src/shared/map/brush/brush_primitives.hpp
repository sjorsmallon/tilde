#pragma once

#include "../../linalg.hpp"
#include "../../span.hpp"
#include "brush.hpp"

#include <vector>

namespace shared
{

// A primitive is point sets dropped to plain brushes with no link back, exactly as
// a placed prefab. geometry_def.md Track I.

inline constexpr int MIN_PRISM_SIDES = 3;
inline constexpr int MAX_PRISM_SIDES = 32;
inline constexpr int MIN_ARCH_SEGMENTS = 1;
inline constexpr int MAX_ARCH_SEGMENTS = 64;

// The two directions a primitive is laid out in around `axis`: `up` is world Y
// unless the axis is vertical, then world Z; `side` completes the right-handed
// triple. The same frame for every primitive, so a prism and an arch placed on the
// same axis agree about where their first corner is.
struct primitive_frame_t
{
  linalg::vec3 axis;
  linalg::vec3 up;
  linalg::vec3 side;
};

primitive_frame_t make_primitive_frame(const linalg::vec3 &axis);

// One convex brush: a regular N-gon of `radius` in the frame's up/side plane,
// extruded `half_length` both ways along the axis. The first corner lies on
// `side`. This is also the hole brush for a round hole. `sides` outside
// [MIN_PRISM_SIDES, MAX_PRISM_SIDES] is a caller bug: the hull is capped.
std::vector<linalg::vec3> make_prism_brush_points(const linalg::vec3 &center,
                                                  const linalg::vec3 &axis, float radius,
                                                  float half_length, int sides);

// A half ring of `segments` wedges, one convex brush each: inner radius
// `inner_radius`, outer `inner_radius + thickness`, standing on the plane
// through `center` normal to the frame's up, from `side` over `up` to `-side`,
// extruded `half_depth` both ways along `axis`. Neighbouring wedges share their
// four boundary points exactly.
std::vector<std::vector<linalg::vec3>>
make_arch_brush_point_sets(const linalg::vec3 &center, const linalg::vec3 &axis,
                           float inner_radius, float thickness, float half_depth,
                           int segments);

// Whether a face of a hulled primitive is one of its curved faces: a prism's sides,
// an arch's inner and outer rim, never a cap or a wedge's radial end. The face
// lies along `axis` and looks straight out from, or in at, `center`.
bool primitive_face_is_curved(const brush_polyhedron_t &hull, const brush_face_t &face,
                          const linalg::vec3 &center, const linalg::vec3 &axis);

} // namespace shared
