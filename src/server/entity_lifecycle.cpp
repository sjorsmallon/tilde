#include "entity_lifecycle.hpp"

#include "../shared/log.hpp"

namespace server
{

// A player's body used to be assembled here: a capsule hitbox nothing read, and
// four render fields of which three restated Render's own defaults. Both now
// come out of entities.def -- `render: Render = { mesh = .Leet_Full }` -- so
// there is nothing left to initialize and no function to forget to call.

bool destroy_entity(server_context_t &context, shared::entity_uid_t uid)
{
  if (uid == shared::null_entity_uid)
  {
    log_error("destroy_entity: asked to destroy the null uid — this is a bug at "
              "the call site, which should not have gotten a handle to destroy");
    return false;
  }

  unregister_physics_body(*context.world.physics, uid);

  // Server-side side tables keyed by uid. Same leak as the Jolt body, different
  // container: an entry that outlives the entity it names is only noticed when
  // something tries to resolve it. `death_tick_by_player_uid` recovers on its
  // own (update_respawns logs and drops an entry whose player is gone), so this
  // is not a live bug -- it is the same class of bug, so it gets torn down in
  // the same place rather than relying on each consumer to be forgiving.
  context.world.death_tick_by_player_uid.erase(uid);

  return context.world.session.entity_system.destroy(uid);
}

} // namespace server
