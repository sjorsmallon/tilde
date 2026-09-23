#include "entity_lifecycle.hpp"

#include "../shared/log.hpp"
#include "../shared/player_constants.hpp"
#include "systems/game_rules_system.hpp"
#include "systems/inventory_system.hpp"
#include "systems/mover_system.hpp"
#include "systems/respawn_system.hpp"

#include <unordered_map>
#include <vector>

namespace server
{

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

  player->client_slot_index = slot;
  player->display_name      = context.clients[slot].player_name;

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
                 slot, match_of(context).round_number + 1);
    return;
  }

  spawn_player_entity_for_client_slot(context, slot);
}

void admit_waiting_players(server_context_t &context)
{
  for (connected_client_t row : connected_clients(context))
  {
    if (!row.client.wants_to_play)
      continue;
    if (row.client.player_uid != shared::null_entity_uid)
      continue;

    spawn_player_entity_for_client_slot(context, row.slot);
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

  // Server-side side tables keyed by uid: an entry that outlives the entity it
  // names is only noticed when
  // something tries to resolve it. `death_tick_by_player_uid` recovers on its
  // own (update_respawns logs and drops an entry whose player is gone), so this
  // is not a live bug -- it is the same class of bug, so it gets torn down in
  // the same place rather than relying on each consumer to be forgiving.
  context.world.death_tick_by_player_uid.erase(uid);

  return context.world.session.entity_system.destroy(uid);
}

static bool survives_the_round(const entities::Entity &entity)
{
  switch (entity.type)
  {
    case entities::entity_type::Player_Entity:     return true;
    case entities::entity_type::Game_Rules_Entity: return true;
    default:                                       return false;
  }
}

// What is listed is what a player KEEPS across a round; any other member is back at its entities.def default.
static void reset_player_to_construction(entities::Player_Entity &player)
{
  entities::Player_Entity fresh{};
  static_cast<entities::Entity &>(fresh) = player;
  fresh.client_slot_index = player.client_slot_index;
  fresh.display_name      = player.display_name;
  fresh.team_allegiance   = player.team_allegiance;
  fresh.ready             = player.ready;
  fresh.kills             = player.kills;
  fresh.deaths            = player.deaths;
  player = fresh;
}

void restore_level_from_map(server_context_t &context)
{
  shared::Entity_System &entity_system = context.world.session.entity_system;

  for (entities::Player_Entity &player : entity_system.entities_of<entities::Player_Entity>())
    reset_player_to_construction(player);

  std::unordered_map<shared::entity_uid_t, entities::Playback> playback_before_restore;
  for (auto [emitter, playback] : entity_system.entities_with_trait<entities::Playable>())
    playback_before_restore.emplace(emitter.entity_id, playback);

  std::vector<shared::entity_uid_t> discarded;
  for (const shared::Entity_Pool &pool : entity_system.pools)
    for (uint32_t slot = 0; slot < pool.count; ++slot)
      if (!survives_the_round(*pool.at(slot)))
        discarded.push_back(pool.at(slot)->entity_id);

  for (const shared::entity_uid_t uid : discarded)
    destroy_entity(context, uid);

  shared::restore_map_entities(context.world.session, context.world.current_map);

  // play_count and stop_count are edges the client diffs, so a restore carries them and stops.
  for (auto [emitter, playback] : entity_system.entities_with_trait<entities::Playable>())
  {
    const auto before = playback_before_restore.find(emitter.entity_id);
    if (before == playback_before_restore.end())
      continue;
    playback.play_count = before->second.play_count;
    playback.stop_count = before->second.stop_count + 1;
  }

  context.world.pending_actions.clear();
  context.world.previous_tick_trigger_overlaps.clear();
  install_movers(context);
}

} // namespace server
