#pragma once

// A spot with `solid_beam` set is its own cone made solid: a pyramid from the fixture to its range,
// cut as a mover whose two poses are equal, as a landed platform is. It follows the light, so a
// light that rides a mover carries its beam. Nothing stops it short: it passes through walls and
// casters, and the author shortens `range`. It carries nobody and crushes nobody.

#include "movers.hpp"

#include <vector>

namespace shared
{

struct Entity_System;

inline constexpr uint32_t SOLID_BEAM_SIDE_COUNT              = 12;
inline constexpr float    MAX_SOLID_BEAM_HALF_ANGLE_DEGREES = 80.f;

// APPENDS, after collect_movers: a riding light is read at the pose of the movers already in `out`.
void collect_solid_beams(const Entity_System& system, const mover_rests_t& rests, std::vector<mover_t>& out);

} // namespace shared
