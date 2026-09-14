#pragma once

#include "../shared/assets/generated/assets_generated.hpp"
#include "../shared/entities/generated/entities_generated.hpp"

#include <optional>

namespace client
{

struct client_context_t;

// The weapon's sounds, read off its WEAPON_DEFINITIONS row (weapons.hpp).
// Shared with Play_State's predicted local shot deliberately: two lookups
// would let your own gun and everyone else's drift apart, which is the same
// class of bug last_fire_weapon exists to prevent.
//
// Fallible, and the caller is what makes it so: last_fire_weapon comes off the
// wire with no range check, so a weapon id outside the enum is a hostile or
// corrupt snapshot rather than a missing asset. Empty optional, log_error'd.
[[nodiscard]] std::optional<assets::sound_asset> try_fire_sound_for(entities::Weapon weapon);

// What a shot from `weapon` sounds like on static geometry; fallible for the same reason.
[[nodiscard]] std::optional<assets::sound_asset> try_world_impact_sound_for(entities::Weapon weapon);

} // namespace client
