#pragma once

#include <array>
#include <cstdint>

#include "entities/generated/entities_generated.hpp"
#include "linalg.hpp"

namespace shared
{

using linalg::vec3f;

enum class hit_region_t : uint16_t { Head = 0, Torso = 1, Legs = 2 };

inline const char* to_string(hit_region_t region)
{
    switch (region)
    {
        case hit_region_t::Head:  return "Head";
        case hit_region_t::Torso: return "Torso";
        case hit_region_t::Legs:  return "Legs";
        default:                  return "Unknown";
    }
}
// Three regions, identical for every player, so a static table -- NOT schema
// fields. They never change and never need the wire: hit decisions are
// server-side, and lag compensation later rewinds position only, so keeping
// the boxes out of the entity keeps Snapshot_History from having to carry them.
//
// Deliberately NOT yaw-rotated. The head is a sphere on the player's vertical
// axis (rotation is exactly a no-op) and the torso/legs are square columns on
// that same axis (rotation is very nearly one), so posing is a plain add and
// every test below is axis-aligned.
struct player_hitbox_t
{
  hit_region_t         region;
  entities::Shape_Kind shape;   // Head = Sphere, Torso/Legs = Box
  vec3f                offset;  // from the player's FEET, straight up
  vec3f                size;    // half-extents; Sphere uses .x as the radius,
                                // matching entities::Hitbox
};

// Derived from the MOVEMENT HULL -- `player_half_width` 16 and
// `player_half_height` 36 (`player_constants.hpp`), origin at the feet, so the
// player is 32 x 32 x 72, HL/Source exact. The three regions tile that height
// exactly: legs 0..30, torso 30..54, head 54..72 (sphere centered 63, r9).
//
// The hull is what these MUST agree with: it is the volume that collides with
// the world, so a hitbox sticking out of it would be hittable somewhere the
// player cannot stand. Half-widths are inset from the hull's 16 on purpose --
// shoulders and legs are narrower than the box that carries them -- but never
// exceed it. Head r9 lands the crown at exactly 72.
//
// The Jolt kinematic capsule agrees as of 2026-08-04 -- it is derived from the
// same two hull constants (`player_capsule_*` in `player_constants.hpp`), so
// the player is one size to movement, to Jolt and to hitscan. The capsule is
// still a separate coarse shape rather than these three boxes: it is what
// rockets and overlap queries find, while hitscan resolves regions against
// THIS table, which is the only thing that knows a head from a leg.
inline constexpr std::array<player_hitbox_t, 3> player_hitboxes{{
    {hit_region_t::Head,  entities::Shape_Kind::Sphere, {0.f, 63.f, 0.f}, {9.f, 9.f, 9.f}},
    {hit_region_t::Torso, entities::Shape_Kind::Box,    {0.f, 42.f, 0.f}, {14.f, 12.f, 14.f}},
    {hit_region_t::Legs,  entities::Shape_Kind::Box,    {0.f, 15.f, 0.f}, {13.f, 15.f, 13.f}},
}};

} // namespace shared
