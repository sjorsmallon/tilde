#include "entity_lifecycle.hpp"

#include "../shared/log.hpp"

namespace server
{

bool destroy_entity(server_context_t &context, shared::entity_uid_t uid)
{
  if (uid == shared::null_entity_uid)
  {
    log_error("destroy_entity: asked to destroy the null uid — this is a bug at "
              "the call site, which should not have gotten a handle to destroy");
    return false;
  }

  // Physics first, while the entity is still resolvable. Nothing here needs to
  // read it today, but a teardown that does (reading a body's final transform,
  // say) would have exactly one order that works, and this is it.
  //
  // Unconditional, and deliberately not switched on entity type:
  // unregister_physics_body is a documented no-op for a uid with no body, so a
  // Rocket_Entity (which has none) costs one failed map lookup and a type this
  // function has never heard of cannot leak by being left out of a switch.
  if (context.physics)
    unregister_physics_body(*context.physics, uid);

  // Server-side side tables keyed by uid. Same leak as the Jolt body, different
  // container: an entry that outlives the entity it names is only noticed when
  // something tries to resolve it. `death_tick_by_player_uid` recovers on its
  // own (update_respawns logs and drops an entry whose player is gone), so this
  // is not a live bug -- it is the same class of bug, so it gets torn down in
  // the same place rather than relying on each consumer to be forgiving.
  context.death_tick_by_player_uid.erase(uid);

  return context.session.entity_system.destroy(uid);
}

} // namespace server
