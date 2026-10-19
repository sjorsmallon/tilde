#pragma once

#include "../../linalg.hpp"
#include "../../span.hpp"
#include "brush.hpp"
#include "primitive_recipe.hpp"

#include <vector>

namespace shared
{

// A primitive is point sets dropped to plain brushes with no link back, exactly as
// a placed prefab. geometry_def.md Track I.

inline constexpr int MIN_PRISM_SIDES = 3;
inline constexpr int MAX_PRISM_SIDES = 32;
inline constexpr int MIN_ARCH_SEGMENTS = 1;
inline constexpr int MAX_ARCH_SEGMENTS = 64;
inline constexpr int MIN_STAIR_STEPS = 1;
inline constexpr int MAX_STAIR_STEPS = 64;
inline constexpr float MIN_SPIRAL_DEGREES_PER_STEP = 1.f;
inline constexpr float MAX_SPIRAL_DEGREES_PER_STEP = 90.f;

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

// A flight of `steps` boxes climbing along `climb` from `base`, each box standing
// on the base plane and `rise` higher than the one before, so the flight is solid
// with a flat underside. `tread_depth` along the climb, `width` centred across
// the frame's side, the rise along world up. A vertical climb is a ladder and a
// caller bug.
std::vector<std::vector<linalg::vec3>>
make_straight_stair_brush_point_sets(const linalg::vec3 &base, const linalg::vec3 &climb,
                                     int steps, float rise, float tread_depth, float width);

// A helix of `steps` slabs around world Y through `base`, each one wedge of
// `degrees_per_step` from `inner_radius` to `inner_radius + tread_width`, one
// `rise` thick and lifted one rise above the one before. The first step starts
// on world X. Neighbouring slabs share the two rib points where they meet.
std::vector<std::vector<linalg::vec3>>
make_spiral_stair_brush_point_sets(const linalg::vec3 &base, int steps, float rise,
                                   float inner_radius, float tread_width,
                                   float degrees_per_step, bool clockwise);

// Every brush of a recipe, laid out at the origin along the recipe's axis and
// turned its quarter turns: the one builder the panel's ghost, a placement and
// a regeneration share. The recipe is clamped first.
std::vector<std::vector<linalg::vec3>>
make_primitive_point_sets(const primitive_recipe_t &recipe);

// Whether a face of a hulled primitive is one of its curved faces: a prism's sides,
// an arch's inner and outer rim, never a cap or a wedge's radial end. The face
// lies along `axis` and looks straight out from, or in at, `center`.
bool primitive_face_is_curved(const brush_polyhedron_t &hull, const brush_face_t &face,
                          const linalg::vec3 &center, const linalg::vec3 &axis);

} // namespace shared
