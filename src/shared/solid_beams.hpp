#pragma once

// A spot with `solid_beam` set is its own cone made solid: a pyramid from the fixture to its range,
// cut as a mover whose two poses are equal, as a landed platform is. It follows the light, so a
// light that rides a mover carries its beam. The shadow volumes the spot's own light throws are
// subtracted from it, so it is solid exactly where beam.frag draws it lit: a wall ends it, a crate
// in it leaves a hole behind itself. The lit part of a pyramid less one volume is not convex, so
// it is carved into disjoint convex pieces, each the pyramid plus the planes that cut it. A beam
// casts no shadow and stops none (collect_shadow_volumes skips it). It carries nobody and crushes
// nobody.

#include "movers.hpp"
#include "shadow_volume.hpp"
#include "span.hpp"

#include <vector>

namespace shared
{

struct Entity_System;

inline constexpr uint32_t SOLID_BEAM_SIDE_COUNT              = 12;
inline constexpr float    MAX_SOLID_BEAM_HALF_ANGLE_DEGREES = 80.f;
// Past this many pieces a beam keeps what it has, skips the volumes left and logs the spot once: a
// level that puts this many casters in one beam is a level to simplify.
inline constexpr uint32_t MAX_SOLID_BEAM_PIECES = 64;

// APPENDS, after collect_movers and collect_shadow_volumes: a riding light is read at the pose of the
// movers already in `out`, and the volumes are those the beam's own light threw this tick.
void collect_solid_beams(const Entity_System& system, const mover_rests_t& rests,
                         Span<const shadow_volume_t> shadow_volumes, std::vector<mover_t>& out);

} // namespace shared
