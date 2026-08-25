#include "../../shared/entities/entity_reflection.hpp"
#include "respawn_system.hpp"

#include "inventory_system.hpp"

#include "../../shared/events/generated/events_generated.hpp"
#include "../../shared/log.hpp"
#include "../../shared/player_constants.hpp"
#include "../../shared/weapons.hpp"

#include <algorithm>
#include <vector>

namespace server
{

void fire_player_spawned_event(server_context_t &context,
                               const entities::Player_Entity &player)
{
  shared::Player_Spawned spawned{};
  spawned.player_id         = player.entity_id;
  spawned.spawn_position    = player.position;
  spawned.spawn_orientation = player.orientation;
  shared::fire_player_spawned(context.outgoing.events, spawned);
}

const entities::Player_Spawn_Entity& origin_fallback_spawn()
{
  static const entities::Player_Spawn_Entity fallback{};
  return fallback;
}

// Declared in respawn_system.hpp — see there for why this takes the marker.
void place_player_at_spawn(shared::game_session_t &session, entities::Player_Entity &player,
                           const entities::Player_Spawn_Entity &marker)
{
  player.position    = marker.position;
  player.orientation = marker.orientation;
  // The marker's orientation is the MODEL euler the editor's rotation gizmo
  // writes, not a yaw/pitch pair. Copying .y and .x straight across mirrored the
  // yaw and read the roll as the pitch -- the editor showed no facing at all
  // then, so it went unnoticed. Going through the direction is what makes the
  // spawned player look where the editor's arrow points.
  const linalg::view_angles_t facing = linalg::view_angles_from_direction(
      linalg::forward_from_model_euler(marker.orientation));
  player.view_angle_yaw   = facing.yaw_degrees;
  player.view_angle_pitch = facing.pitch_degrees;
  // The feet are an accumulator, so they have to be PLACED, not left: a corpse
  // froze them wherever it died, and a spawn that only writes the view yaw
  // makes the fresh player spin their legs around to catch up while the server
  // hit-tests the twist. A first spawn has the same problem from zero.
  player.body_yaw = facing.yaw_degrees;

  player.health   = 100;
  player.velocity = {0.f, 0.f, 0.f};
  // Back to alive: this is what stops clients drawing the death clip.
  player.death_tick = 0;

  // Fresh magazines and no reload in flight. Both are accumulators like
  // body_yaw above: a corpse dies mid-reload, and a spawn that leaves the
  // deadline standing hands the new body a reload it never started -- or, worse,
  // one whose deadline has already passed, which the next shot silently
  // completes into a full magazine.
  //
  // Magazines are plural now: one per carried weapon, plus each one's fire
  // clock and the deploy gate, all of which refill_inventory clears.
  refill_inventory(session, player);
  player.reload_complete_time = 0;
}

void schedule_respawn(server_context_t &context,
                      shared::entity_uid_t player_uid,
                      uint32_t death_tick)
{
  if (context.world.death_tick_by_player_uid.contains(player_uid))
    return;

  context.world.death_tick_by_player_uid[player_uid] = death_tick;
}

// Declared in respawn_system.hpp — see there for the contract.
const entities::Player_Spawn_Entity*
try_pick_human_spawn(shared::game_session_t &session, uint32_t rotation_index)
{
  Span<entities::Player_Spawn_Entity> spawns =
      session.entity_system.entities_of<entities::Player_Spawn_Entity>();

  // Two passes rather than a collected vector: this runs on player join and on
  // every respawn, and the pool is small enough that counting is cheaper than
  // allocating.
  uint32_t human_count = 0;
  for (const entities::Player_Spawn_Entity &spawn : spawns)
    if (spawn.spawn_type == entities::Spawn_Type::Human) ++human_count;

  if (human_count == 0)
    return nullptr;

  const uint32_t wanted = rotation_index % human_count;
  uint32_t       seen   = 0;
  for (const entities::Player_Spawn_Entity &spawn : spawns)
  {
    if (spawn.spawn_type != entities::Spawn_Type::Human) continue;
    if (seen == wanted) return &spawn;
    ++seen;
  }

  fatal_error("try_pick_human_spawn: counted %u human spawns then failed to "
              "reach index %u — the pool changed under us",
              human_count, wanted);
}

void update_respawns(server_context_t &context,
                     uint32_t current_tick,
                     uint32_t tickrate_hz,
                     const float respawn_delay_seconds)
{
  if (context.world.death_tick_by_player_uid.empty())
    return;

  // Clamped because the value comes from a cvar, and a map or a console can
  // hand us a negative one: a negative delay means "as soon as possible", and
  // casting it to uint32_t would mean the opposite by a wide margin.
  const uint32_t delay_ticks = static_cast<uint32_t>(
      std::max(0.f, respawn_delay_seconds) * static_cast<float>(tickrate_hz));

  // Collect uids to respawn this tick. Two-pass so we can erase from the
  // map without invalidating the iteration over it.
  std::vector<shared::entity_uid_t> ready_uids;
  for (const auto &[uid, death_tick] : context.world.death_tick_by_player_uid)
  {
    if (current_tick >= death_tick + delay_ticks)
      ready_uids.push_back(uid);
  }

  for (shared::entity_uid_t uid : ready_uids)
  {
    context.world.death_tick_by_player_uid.erase(uid);

    entities::Player_Entity *player =
        context.world.session.entity_system.get<entities::Player_Entity>(uid);
    if (!player)
    {
      // Player entity vanished between death and respawn (disconnect, map
      // change, etc.). Not an error; just nothing to respawn.
      log_terminal("update_respawns: player uid {} no longer exists, skipping",
                   static_cast<uint64_t>(uid));
      continue;
    }

    // 0: the respawn drain always takes the first marker. Cycling here is a
    // gameplay decision, not a mechanism one — change the argument, not this.
    const entities::Player_Spawn_Entity *marker =
        try_pick_human_spawn(context.world.session, 0);
    if (marker == nullptr)
      log_error("update_respawns: no Player_Spawn_Entity available, "
                "respawning player uid {} at origin",
                static_cast<uint64_t>(uid));

    place_player_at_spawn(context.world.session, *player,
                          marker ? *marker : origin_fallback_spawn());

    // Move the kinematic Jolt capsule so subsequent overlap/swept queries
    // this tick (rocket splash, trigger volumes) see the player at the new
    // position, not the death position. Matches the offset
    // register_kinematic_capsule uses at connect time.
    set_kinematic_pose(*context.world.physics, uid,
                       player->position +
                           vec3f{0.f, shared::player_capsule_center_offset, 0.f},
                       vec3f{0.f, 0.f, 0.f});

    fire_player_spawned_event(context, *player);
  }
}

} // namespace server
