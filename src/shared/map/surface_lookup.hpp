#pragma once

// The one resolver from a ground contact to the map's material index: the
// server fills Footstep and Land with it, the client plays its own predicted
// step from it. 0 is the map default and the answer for everything that is not
// a brush face: a static mesh, a mover (material_sound_plan.md step 3), a
// spawned slab, no ground at all.

#include "game_session.hpp"
#include "movement/ground_contact.hpp"

#include <cstdint>

namespace shared
{

[[nodiscard]] uint16_t material_index_under(const game_session_t& session,
                                            const ground_contact_t& contact);

} // namespace shared
