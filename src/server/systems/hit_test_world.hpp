#pragma once

namespace server
{

struct server_context_t;

// The present-tick hit-test world: every living Mortal posed into
// context.posed_players, once, before any input runs (tick_def.md step 2).
// Posing after the moves would test a world no client has ever been shown, and
// would make the fallback arm disagree with the rewind arm by one tick.
void pose_all_targets(server_context_t& context);

} // namespace server
