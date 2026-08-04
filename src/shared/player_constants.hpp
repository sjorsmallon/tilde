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

// The player's Jolt kinematic body, DERIVED from the hull above rather than
// written out -- writing it out is exactly how it drifted to 36 x 76 while the
// hull was 32 x 72, which made a player hittable by rocket splash two units
// outside the box they collide with.
//
// Jolt's CapsuleShape takes the CYLINDER half-height and adds a hemispherical
// cap of `radius` at each end, so the total height is 2*(half_height + radius)
// -- 2*(20 + 16) = 72, matching the hull. The cylinder half-height is NOT
// `player_half_height`; subtracting the cap radius is the whole point.
constexpr float player_capsule_radius               = player_half_width;
constexpr float player_capsule_cylinder_half_height = player_half_height - player_half_width;

// A Jolt capsule is centered on its body position; the player origin is at the
// FEET. Every register_kinematic_capsule / set_kinematic_pose call for a player
// or bot adds this, so they must all add the same thing.
constexpr float player_capsule_center_offset = player_half_height;

// Eye offset above the player's origin, which sits at the FEET.
//
// This is Source's VEC_VIEW and it must be ONE constant: the client draws the
// camera here and the server casts hitscan from here, so any disagreement means
// shots do not come from where the crosshair is. That was the bug this replaces
// -- the client had a bare `+ 28.f` in two places and the server had nothing.
//
// 28 was GoldSrc's VEC_VIEW, which is measured from a CENTER origin (its hull
// was -36..+36), so it already had 36 units under it. Source moved the origin to
// the feet and the constant became 64. Both put the eye 64 above the feet; this
// codebase had Source's origin convention with GoldSrc's number, which put the
// camera at the top of the legs. 64 also matches CS:GO standing.
//
// Pairs with the 32x32x72 hull above -- HL/Source exact. If a duck stance
// lands, it gets its own constant (Source: 28 ducking) rather than scaling
// this one.
constexpr float player_eye_height = 64.f;

} // namespace shared
