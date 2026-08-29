#include "held_snapshot.hpp"

#include "../shared/cvars/generated/cvars_generated.hpp"
#include "../shared/entity_uid.hpp"
#include "../shared/log.hpp"
#include "client_context.hpp"
#include "hit_confirm_audio.hpp"
#include "remote_interpolation.hpp"
#include "weapon_fire_audio.hpp"

#include "game.pb.h"

#include <algorithm>
#include <format>

namespace client
{

namespace
{

constexpr uint32_t SNAPSHOT_DEBUG_TICK_INTERVAL = 120;

// How far into the death clip a corpse stamped at `death_tick` already is, seen
// from `server_tick`. Non-zero only for a death that happened BEFORE this
// snapshot, which is what lets a client connecting mid-corpse -- or one that
// dropped the packet the death landed in -- pick the clip up where it is rather
// than restarting the fall. A living player (stamp 0), a death stamped this very
// tick and a stamp somehow ahead of the snapshot all start at zero; that last
// case is also what keeps the unsigned subtract from wrapping.
float death_clip_seconds_elapsed(uint32_t death_tick, uint32_t server_tick,
                                 float server_tickrate)
{
  if (death_tick == 0 || death_tick >= server_tick)
    return 0.f;

  return static_cast<float>(server_tick - death_tick) / server_tickrate;
}

} // namespace

std::optional<decoded_snapshot_t>
try_decode_snapshot(const client_context_t& context, const game::S2C_EntityPackage& package)
{
  if (!package.has_entity_data() || package.entity_data().empty())
    return std::nullopt;

  // the tick that the server ran and produced this data.
  const uint32_t server_tick = package.has_server_tick() ? package.server_tick() : 0;

  // if it's one that's older than the one we have, skip it.
  if (server_tick <= context.replication.latest_processed_tick &&
      context.replication.latest_processed_tick != 0)
    return std::nullopt;

  // is this a baseline tick or a delta tick?
  const std::optional<uint32_t> baseline_tick = ::network::snapshot_baseline_tick(package);
  const ::network::snapshot_frame_t* baseline = nullptr;
  if (baseline_tick.has_value())
  {
    baseline = context.replication.snapshot_history.find(*baseline_tick);
    if (baseline == nullptr)
    {
      // the server deltaed against a tick we no longer hold.
      // not anything meaningful to do, drop the packet and
      // wait for a new baseline.
      log_error("[CLIENT] Snapshot {} is a delta from tick {}, which is not in our history "
                "(acked {}). Dropping the packet; the server will re-baseline.",
                server_tick, *baseline_tick, context.replication.snapshot_history.acked_tick);
      return std::nullopt;
    }
  }

  const auto* data = reinterpret_cast<const ::network::uint8*>(package.entity_data().data());
  const size_t data_size = package.entity_data().size();
  ::network::Bit_Reader reader(data, data_size);

  // latest snapshot starts from the baseline that's attached to the incoming
  // message, whether the incoming message _is_ the baseline, or a delta.
  auto decoded = decoded_snapshot_t{};
  if (!::network::deserialize_snapshot(reader, baseline, decoded.frame))
    return std::nullopt;

  decoded.frame.tick = server_tick;

  if (package.has_latest_processed_input_number())
    decoded.latest_processed_input_number = package.latest_processed_input_number();

  if (context.cvars->net_snapshot_debug && server_tick % SNAPSHOT_DEBUG_TICK_INTERVAL == 0)
  {
    log_terminal("[net] snapshot {}: {}, {} players // {} bodies, {} bytes", server_tick,
                 baseline_tick.has_value() ? std::format("delta from {}", *baseline_tick)
                                           : std::string("full update"),
                 decoded.frame.players.size(), decoded.frame.physics_bodies.size(), data_size);
  }

  return decoded;
}

void advance_newest_held_snapshot(client_context_t& context, decoded_snapshot_t&& decoded)
{
  const uint32_t server_tick = decoded.frame.tick;

  // --- 1. Replication: the raw truth, nothing derived from it yet ---
  //@NOTE(SJM): should we just  overwrite?
  context.replication.latest_player_entities.clear();
  for (const auto& [uid, player] : decoded.frame.players)
    context.replication.latest_player_entities[player.client_slot_index] = player;

  context.replication.latest_weapon_entities = decoded.frame.weapons;
  context.replication.remote_rockets = decoded.frame.rockets;
  context.replication.remote_physics_bodies = decoded.frame.physics_bodies;
  context.replication.latest_processed_tick = server_tick;

  // A damageable is MAP-PLACED, so unlike the four above it is not kept in a
  // replication map and drawn from there -- the client already has the object,
  // with its mesh and its position, out of the map it loaded. What it cannot
  // have is the two fields that change at runtime, so those are written onto
  // the session copy the draw loop already walks.
  //
  // Uids line up because both sides get them from the same place: the map file
  // carries a uid per entry and Entity_System::populate_from_map uses it
  // verbatim rather than minting a new one. A uid with no local entity is
  // therefore a real disagreement about the map -- reported, not skipped,
  // because the alternative is a target that is invulnerable on one screen.
  for (const auto& [uid, damageable] : decoded.frame.damageables)
  {
    entities::Damageable_Entity* local =
        context.world.session.entity_system.get<entities::Damageable_Entity>(uid);
    if (local == nullptr)
    {
      log_error("snapshot names damageable uid {}, which this client's map does not have -- the "
                "two sides disagree about what is in the level",
                uid);
      continue;
    }

    local->health         = damageable.health;
    local->render.visible = damageable.render.visible;
  }

  // --- 2. Connection facts derived from step 1 ---
  //@NOTE(SJM): this is not a particularly elegant way to do spectating. should it be a different team?
  context.connection.spectating =
      !context.replication.latest_player_entities.contains(context.connection.my_slot);
  if (context.connection.spectating)
  {
    // Or the stale uid would keep suppressing our own cosmetic effects for a
    // body we no longer have.
    context.connection.my_entity_uid = shared::null_entity_uid;
  }

  // --- 3. The stamp watchers ---
  // Their headers say "once per received snapshot, right after
  // latest_player_entities is refreshed", and both also read my_entity_uid to
  // decide whose shot to skip -- so they go after step 2 settles it, not
  // between the two. That is the ordering the old loop had; it is written down
  // here because the two steps read as independent and are not.
  update_weapon_fire_audio(context);
  play_hitmarker_audio_and_update_hit_tick_state(context);

  // --- 4. The server's word on our input stream ---
  if (decoded.latest_processed_input_number.has_value())
    context.prediction.latest_input_number_processed_by_server =
        *decoded.latest_processed_input_number;

  // erase any input number lower than the latest acknowledged.
  std::erase_if(context.prediction.unacked_inputs,
                [&](const game::C2S_ClientInput& input)
                {
                  return input.input_number() <=
                         context.prediction.latest_input_number_processed_by_server;
                });

  // --- 5. Seed prediction from our own body; feed the remotes' rings ---
  for (const auto& [slot_index, player] : context.replication.latest_player_entities)
  {
    if (slot_index == context.connection.my_slot)
    {
      context.connection.my_entity_uid = player.entity_id;

      context.prediction.local_player_health = player.health;
      context.prediction.latest_server_position = player.position;
      context.prediction.latest_server_velocity = player.velocity;
      context.prediction.latest_server_movement = player.movement;
      context.prediction.received_server_update = true;

      if (!context.connection.logged_first_server_update)
      {
        log_terminal("[CLIENT] First server update: position ({:.1f}, {:.1f}, {:.1f}), entity_id {}",
                     player.position.x, player.position.y, player.position.z, player.entity_id);
        context.connection.logged_first_server_update = true;
      }
    }
    else
    {
      auto& remote_player = context.replication.remote_players[slot_index];
      // don't lerp positions if the entity id changed (e.g. player disconnected and rejoined)
      if (remote_player.active && remote_player.entity_uid != player.entity_id)
      {
        remote_player = {};
      }

      remote_player.active = true;
      remote_player.slot_index = slot_index;
      remote_player.entity_uid = player.entity_id;

      push_snapshot_pose(remote_player.interpolation,
                         {player.position, player.view_angle_yaw, player.view_angle_pitch,
                          player.body_yaw, server_tick});

      // death_tick only moves on a death or a respawn, so this seeds the timer
      // that the render loop advances -- once per transition, not per snapshot.
      // The SEED is here and the ADVANCE is in update() precisely because they
      // run on different clocks; this is the dt-free half.
      if (player.death_tick != remote_player.death_tick)
      {
        remote_player.death_tick = player.death_tick;
        remote_player.death_animation_seconds = death_clip_seconds_elapsed(
            player.death_tick, server_tick,
            static_cast<float>(context.connection.server_tickrate));
      }
    }
  }

  // record the live edge tick (as like a horizon to interpolate against)
  client::record_snapshot_tick(
      context.replication.interpolation_cursor, server_tick,
      client::interpolation_delay_in_ticks_from_cvar(context.cvars->cl_interpolation_delay_ticks));

  // remove disconnected players.
  for (auto it = context.replication.remote_players.begin();
       it != context.replication.remote_players.end();)
    it = context.replication.latest_player_entities.count(it->first) == 0
             ? context.replication.remote_players.erase(it)
             : std::next(it);

  // --- 6. The line the function is named for ---
  // N joins the ring and becomes the newest we hold: the next C2S reports it,
  // and the 31 older ones stay decodable. Last, because it claims every step
  // above succeeded -- and
  // unreachable from any path that dropped the packet, which is now a property
  // of the signature rather than of where the `continue`s happened to sit.
  context.replication.snapshot_history.slot_for(server_tick) = std::move(decoded.frame);
  context.replication.snapshot_history.acknowledge(server_tick);
}

} // namespace client
