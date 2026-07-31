#pragma once

#include <array>
#include <cstdint>

#include "entities/generated/entities_generated.hpp"
#include "linalg.hpp"

namespace shared
{

using linalg::vec3f;

enum class hit_region_t : uint16_t { Head = 0, Torso = 1, Legs = 2 };

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

// Derived from the combat capsule at server_impl.cpp:428 -- radius 18,
// half-height 38, origin at the feet, so the player is ~76 tall and 36 wide.
// The three regions tile that: legs 0..30, torso 30..56, head 56..76.
inline constexpr std::array<player_hitbox_t, 3> player_hitboxes{{
    {hit_region_t::Head,  entities::Shape_Kind::Sphere, {0.f, 66.f, 0.f}, {10.f, 10.f, 10.f}},
    {hit_region_t::Torso, entities::Shape_Kind::Box,    {0.f, 43.f, 0.f}, {14.f, 13.f, 14.f}},
    {hit_region_t::Legs,  entities::Shape_Kind::Box,    {0.f, 15.f, 0.f}, {13.f, 15.f, 13.f}},
}};

} // namespace shared
