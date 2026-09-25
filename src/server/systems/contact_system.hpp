#pragma once

#include "../../shared/predicted_world.hpp"

namespace server
{

struct server_context_t;

// tick_def.md step 4, the one place a shot ACTS. Inputs push a pending_contact_t
// for every hitscan that arrived; the things they launched fly and push theirs;
// then this runs the row's contact_t for each, every non-Damage effect in push
// order and Damage last as one batch, then the reloads that came due.
//
// Separate from the pushers for one reason -- two players who kill each other
// in one tick must both succeed. Nothing before this mutates health, so every
// `is_dead` gate up to here reads start-of-tick health.
void update_contacts(server_context_t& context, const shared::predicted_world_storage_t& world);

} // namespace server
