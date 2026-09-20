#pragma once

namespace server
{

struct server_context_t;

// tick_def.md step 4, the consequences of this tick's inputs: the swaps, then
// every pending hit turned into damage, then the reloads that came due.
//
// Separate from the input loop for one reason -- two players who kill each
// other in one tick must both succeed. Nothing mutates health while the inputs
// run, so their `is_dead` gate reads start-of-tick health.
void update_hit_resolution(server_context_t& context);

} // namespace server
