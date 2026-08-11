#pragma once

// What a hit COSTS, which is not the same question as what a hit volume IS.
//
// Ten volumes map onto three regions (`resources/models/rig.hitboxes` decides
// which), so a forearm can be a separate capsule and still cost Torso damage.
// Keeping the two counts apart is what lets the rig gain volumes without
// touching balance.
//
// Was `player_hitboxes.hpp`, which also held a table of three axis-aligned
// boxes at fixed offsets from the feet. That table is gone: it was the whole
// hitbox system before the volumes followed the pose, and leaving it lying
// around invites hit-testing a column that no longer matches anything drawn.

#include <cstdint>
#include <cstring>
#include <optional>

namespace shared
{

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

// The inverse, for the `.hitboxes` reader. Names are what the file spells, so
// this and to_string above must stay each other's inverse.
[[nodiscard]] inline std::optional<hit_region_t> try_hit_region_from_string(const char* text)
{
    for (hit_region_t region : {hit_region_t::Head, hit_region_t::Torso, hit_region_t::Legs})
        if (std::strcmp(to_string(region), text) == 0)
            return region;
    return std::nullopt;
}

} // namespace shared
