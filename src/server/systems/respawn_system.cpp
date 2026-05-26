#include "respawn_system.hpp"

#include "../../shared/entities/entity_list.hpp"
#include "../../shared/entities/player_entity.hpp"
#include "../../shared/log.hpp"
#include "../game_events.hpp"

#include <algorithm>
#include <vector>

namespace server
{

void fire_player_spawned_event(server_context_t &context,
                               shared::entity_uid_t player_uid,
                               vec3f spawn_position,
                               vec3f spawn_orientation)
{
  shared::game_event_t event{};
  event.kind = shared::game_event_kind_t::PLAYER_SPAWNED;
  event.player_spawned.player_id         = player_uid;
  event.player_spawned.spawn_position    = spawn_position;
  event.player_spawned.spawn_orientation = spawn_orientation;
  fire_game_event(context, event);
}

void schedule_respawn(server_context_t &context,
                      shared::entity_uid_t player_uid,
                      uint32_t death_tick)
{
  if (context.death_tick_by_player_uid.contains(player_uid))
    return;

  context.death_tick_by_player_uid[player_uid] = death_tick;
}

static network::Player_Entity *
find_player_by_uid(shared::game_session_t &session, shared::entity_uid_t uid)
{
  auto *players = session.entity_system.get_entities<network::Player_Entity>(
      entity_type::PLAYER);
  if (!players) return nullptr;
  for (auto &p : *players)
    if (static_cast<shared::entity_uid_t>(p.entity_id) == uid) return &p;
  return nullptr;
}

// Pick a spawn marker for this player. Right now we always grab the first
// human spawn (spawn_type == 0); team-based or round-cycled selection lands
// later by changing this function — every (re)spawn path routes through it.
static bool pick_spawn_marker(shared::game_session_t &session,
                              vec3f &out_position,
                              vec3f &out_orientation)
{
  auto *spawns = session.entity_system
                     .get_entities<network::Player_Spawn_Entity>(entity_type::PLAYER_SPAWN);
  if (!spawns)
    return false;
  for (const auto &sp : *spawns)
  {
    if (sp.spawn_type != 0) continue;
    out_position    = sp.position;
    out_orientation = sp.orientation;
    return true;
  }
  return false;
}

void update_respawns(server_context_t &context,
                     uint32_t current_tick,
                     uint32_t tickrate_hz)
{
  if (context.death_tick_by_player_uid.empty())
    return;

  const uint32_t delay_ticks =
      static_cast<uint32_t>(respawn_delay_seconds * static_cast<float>(tickrate_hz));

  // Collect uids to respawn this tick. Two-pass so we can erase from the
  // map without invalidating the iteration over it.
  std::vector<shared::entity_uid_t> ready_uids;
  for (const auto &[uid, death_tick] : context.death_tick_by_player_uid)
  {
    if (current_tick >= death_tick + delay_ticks)
      ready_uids.push_back(uid);
  }

  for (shared::entity_uid_t uid : ready_uids)
  {
    context.death_tick_by_player_uid.erase(uid);

    network::Player_Entity *player = find_player_by_uid(context.session, uid);
    if (!player)
    {
      // Player entity vanished between death and respawn (disconnect, map
      // change, etc.). Not an error; just nothing to respawn.
      log_terminal("update_respawns: player uid {} no longer exists, skipping",
                   static_cast<uint64_t>(uid));
      continue;
    }

    vec3f spawn_position{0.f, 0.f, 0.f};
    vec3f spawn_orientation{0.f, 0.f, 0.f};
    if (!pick_spawn_marker(context.session, spawn_position, spawn_orientation))
    {
      log_error("update_respawns: no Player_Spawn_Entity available, "
                "respawning player uid {} at origin",
                static_cast<uint64_t>(uid));
    }

    // Reset gameplay state. Health back to full, velocity cleared,
    // position/orientation taken from the spawn marker. view_angle_yaw/pitch
    // come from the spawn's Euler orientation (.y = yaw, .x = pitch).
    player->position         = spawn_position;
    player->orientation      = spawn_orientation;
    player->view_angle_yaw   = spawn_orientation.y;
    player->view_angle_pitch = spawn_orientation.x;
    player->health           = 100;
    player->velocity         = {0.f, 0.f, 0.f};

    // Move the kinematic Jolt capsule so subsequent overlap/swept queries
    // this tick (rocket splash, trigger volumes) see the player at the new
    // position, not the death position. Matches the offset
    // register_kinematic_capsule uses at connect time.
    if (context.physics)
    {
      set_kinematic_pose(*context.physics, uid,
                         spawn_position + vec3f{0.f, 38.f, 0.f},
                         vec3f{0.f, 0.f, 0.f});
    }

    fire_player_spawned_event(context, uid, spawn_position, spawn_orientation);
  }
}

} // namespace server
