#pragma once

namespace shared
{

// Physics hull half-extents for the standing player.
// Shared by player_move, the editor ghost, spawn placement and map bounds.
//
// These lived on player_entity.hpp until the cutover, where they were the only
// thing in that header that was not schema boilerplate. They are gameplay
// constants, not entity reflection, so they get their own header rather than
// riding along with the generated struct.
constexpr float player_half_width  = 16.f;
constexpr float player_half_height = 36.f;

} // namespace shared
