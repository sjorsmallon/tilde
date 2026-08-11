#include "entity_lifecycle.hpp"

#include "../shared/log.hpp"
#include "../shared/player_constants.hpp"

namespace server
{

void initialize_player_body(entities::Player_Entity &player)
{
  // Coarse whole-body capsule, sized to the movement hull. It is NOT what
  // hitscan resolves against -- that is the per-region table in
  // `player_hitboxes.hpp`. This one exists so rockets and overlap queries have
  // something cheap to find the player with.
  // x/z = radius, y = CYLINDER half-height -- the same convention Jolt's
  // CapsuleShape and the debug renderer both use (caps sit at +/- y and add
  // radius beyond), so all three read these bytes the same way.
  player.hitbox.shape  = entities::Shape_Kind::Capsule;
  player.hitbox.size   = {shared::player_capsule_radius,
                          shared::player_capsule_cylinder_half_height,
                          shared::player_capsule_radius};
  player.hitbox.offset = {0.f, shared::player_capsule_center_offset, 0.f};

  // The model. @Networked and server-assigned because which body a player wears
  // is a CHOICE (a class pick, in the end), and a choice cannot be derived from
  // anything the client already has -- unlike, say, the hull dimensions above,
  // which are constants both sides compile in. Delta compression means this
  // costs its bytes once at spawn and never again.
  //
  // When there is a real class roster, the wire field should become the class
  // and the mesh a lookup off it: a class is a mesh AND a hitbox profile AND a
  // move speed, none of which derive from a mesh id.
  player.render.mesh    = entities::mesh_asset::Leet_Full;
  player.render.visible = true;
  player.render.scale   = {1.f, 1.f, 1.f};
  // The exporter puts the model's feet at the origin, which is the same
  // convention as the player position, `player_eye_height` and the hitbox
  // table. So no offset -- if one is ever needed, the export is wrong.
  player.render.offset  = {0.f, 0.f, 0.f};
}

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
