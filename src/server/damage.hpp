#pragma once

#include "../shared/linalg.hpp"
#include "../shared/entity_uid.hpp"
#include "../shared/span.hpp"
// damage_type_t / damage_info_t. They live in their own header because the
// context holds a list of them -- see damage_types.hpp.
#include "damage_types.hpp"
#include "server_context.hpp"

#include <cstdint>

namespace server
{

// Apply damage to whatever entity `info.victim_uid` resolves to. Owns:
//   - pre-checks (victim exists? still alive? — corpses stop taking damage)
//   - HP subtract (Player_Entity) or impulse application (Physics_Body_Entity)
//   - the >0 → <=0 crossing detection + PLAYER_DIED + schedule_respawn
//   - knockback velocity write
//
// Unknown entity types (anything not in the dispatch switch) log_error and
// do nothing — per the project's no-silent-failures rule.
void inflict_damage(server_context_t &context, const damage_info_t &info);

// Resolve a set of hits that all landed in the SAME tick, so that neither the
// outcome nor the kill credit depends on the order they were recorded in.
//
// Damage is summed per victim and applied once. Calling inflict_damage in a loop
// instead is not merely unlabeled — it is lossy: the first lethal hit turns the
// victim into a corpse, and every later hit on them is then discarded whole, so
// the second shooter's damage never registers (no assist, no stat) and their
// knockback never lands. Whoever the caller's list happened to put first won.
//
// Kill credit goes to the largest single contributor, ties to the lower attacker
// uid. Depth is 1 by construction: within a tick nothing observes a death, so
// A→B→C→A kills all three and needs no chain resolution — see the ordering notes
// in lag_compensation_def.md.
//
// Non-player victims have nothing to contend over and fall through to
// inflict_damage per hit.
void inflict_damage_batch(server_context_t &context, Span<const pending_hit_t> hits);

} // namespace server
