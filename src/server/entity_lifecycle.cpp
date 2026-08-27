#include "entity_lifecycle.hpp"

#include "../shared/log.hpp"
#include "../shared/player_constants.hpp"
#include "systems/game_rules_system.hpp"
#include "systems/inventory_system.hpp"
#include "systems/respawn_system.hpp"

namespace server
{

// A player's body used to be assembled here: a capsule hitbox nothing read, and
// four render fields of which three restated Render's own defaults. Both now
// come out of entities.def -- `render: Render = { mesh = .Leet_Full }` -- so
// there is nothing left to initialize and no function to forget to call.

shared::entity_uid_t spawn_player_entity_for_client_slot(server_context_t &context, int32_t slot)
{
  if (!is_valid_client_slot(slot))
  {
    log_error("spawn_player_entity_for_client_slot: slot {} is out of range", slot);
    return shared::null_entity_uid;
  }

  const shared::entity_uid_t player_uid =
      context.world.session.entity_system.spawn<entities::Player_Entity>();

  entities::Player_Entity* player =
      context.world.session.entity_system.get<entities::Player_Entity>(player_uid);

  if (!player)
  {
    log_error("spawned a player entity and could not find it.");
    return shared::null_entity_uid;
  }

  context.clients[slot].player_uid = player_uid;

  // Same tick as the player, so the two ride one snapshot frame -- see
  // grant_default_inventory for why that is load-bearing rather than tidy.
  // It spawns into the Weapon_Entity pool, which cannot move `player`.
  grant_default_inventory(context.world.session, player_uid);

  player->client_slot_index = slot;
  player->name              = context.clients[slot].player_name;

  // BEFORE the marker is picked, because Team_Markers picks by it. Free_For_All
  // in a mode that does not assign teams, which is every marker's default too,
  // so the spawn policy and the team agree without either knowing the mode.
  player->team_allegiance = pick_team_for_new_player(context);

  // Cycled by slot so two players joining an empty server don't stack.
  const entities::Player_Spawn_Entity* marker =
      try_pick_human_spawn(context.world.session, current_mode(context).spawn_policy,
                           player->team_allegiance, static_cast<uint32_t>(slot));
  if (marker == nullptr)
    log_error("spawn_player: map '{}' declares no Spawn_Type::Human marker — "
              "spawning slot {} at origin",
              context.world.session.map_name, slot);

  place_player_at_spawn(context.world.session, *player,
                        marker ? *marker : origin_fallback_spawn());

  log_terminal("Spawned player at slot {} with entity_id {} at position ({}, {}, {})",
               slot, player->entity_id, player->position.x, player->position.y,
               player->position.z);

  // Kinematic Jolt body so rockets and overlap queries can find this player.
  register_kinematic_capsule(*context.world.physics,
                             player_uid,
                             player->position +
                                 vec3f{0.f, shared::player_capsule_center_offset, 0.f},
                             shared::player_capsule_radius,
                             shared::player_capsule_cylinder_half_height);

  fire_player_spawned_event(context, *player);
  return player_uid;
}

void try_admit_player(server_context_t &context, int32_t slot)
{
  if (!is_valid_client_slot(slot))
  {
    log_error("try_admit_player: slot {} is out of range", slot);
    return;
  }

  // Recorded whether or not a body follows: this is the client's ANSWER to
  // "spectator or player", and it outlives the round that refused it.
  context.clients[slot].wants_to_play = true;

  if (context.clients[slot].player_uid != shared::null_entity_uid)
    return;

  // The gate is the LIVE phase, not "a round exists": warmup, the freeze and
  // the post-round settle all spawn you immediately in every mode, because none
  // of them is a round anyone can be reinforced in the middle of.
  if (!current_mode(context).join_in_progress && is_round_live(context))
  {
    log_terminal("slot {} joined mid-round; spawning at the start of round {}",
                 slot, context.world.rules.round_number + 1);
    return;
  }

  spawn_player_entity_for_client_slot(context, slot);
}

void admit_waiting_players(server_context_t &context)
{
  for (int32_t slot = 0; slot < network::sv_max_client_count; ++slot)
  {
    if (!context.transport_layer.slot_occupied[slot])
      continue;
    if (!context.clients[slot].wants_to_play)
      continue;
    if (context.clients[slot].player_uid != shared::null_entity_uid)
      continue;

    spawn_player_entity_for_client_slot(context, slot);
  }
}

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
