#include "entities/generated/entities/bubble_entity_generated.hpp"
#include "entities/generated/entities/damageable_entity_generated.hpp"
#include "entities/generated/entities/game_rules_entity_generated.hpp"
#include "entities/generated/entities/sound_emitter_entity_generated.hpp"
#include "entities/generated/entities/player_entity_generated.hpp"
#include "entities/generated/entities_tables_generated.hpp"
#include "held_snapshot.hpp"

#include "../shared/cvars/generated/cvars_generated.hpp"
#include "../shared/entity_uid.hpp"
#include "../shared/log.hpp"
#include "audio/audio_system.hpp"
#include "client_context.hpp"
#include "remote_interpolation.hpp"
#include "../shared/movement/player_constants.hpp"
#include "entity_type_audio.hpp"
#include "hud.hpp"
#include "weapon_fire_audio.hpp"

#include "game.pb.h"

#include <algorithm>
#include <format>
#include <iterator>

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



// A stamp older than this when we first see it advance is not worth playing:
// after a hitch, a long stall or a map load, every advance we were never shown
// lands in one frame and would fire as a burst of backdated sound pointing at
// the wrong moment. 12 ticks is ~200ms at the default 60Hz -- long enough that
// ordinary loss (one or two re-sends) still plays, short enough that a stall
// stays quiet.
constexpr uint32_t MAX_STAMP_AGE_TICKS = 12;

constexpr assets::asset_name_t HEADSHOT_SOUNDS[] = {"headshot1", "headshot2", "headshot3"};

// Guards the subtraction as well as the age: a stamp ahead of the snapshot
// tick would wrap and read as ancient.
bool stamp_is_recent(uint32_t stamp, uint32_t server_tick)
{
  return stamp <= server_tick && server_tick - stamp <= MAX_STAMP_AGE_TICKS;
}

void play_gunshots(client_context_t& context, const ::network::snapshot_frame_t& previous,
                   const ::network::snapshot_frame_t& current)
{
  for (const entities::Player_Entity& player :
       current.entities.entities_of_type<entities::Player_Entity>())
  {
    // Our own shot already played off prediction in Play_State.
    if (player.client_slot_index == context.connection.my_slot)
      continue;

    const entities::Player_Entity* before =
        previous.entities.get<entities::Player_Entity>(player.entity_id);
    if (before == nullptr || player.last_fire_tick <= before->last_fire_tick)
      continue;
    if (!stamp_is_recent(player.last_fire_tick, current.tick))
      continue;

    const std::optional<assets::sound_asset> sound = try_fire_sound_for(player.last_fire_weapon);
    if (!sound)
      continue;

    // The muzzle is at the eye, matching where the server casts the shot from.
    const vec3f muzzle = player.position + vec3f{0.f, shared::player_eye_height, 0.f};
    context.audio.play_3d(*sound, muzzle);
  }
}

// Only headshots ding. An ordinary hit already produces FLESH_IMPACT at the
// victim, and a second sound for every body shot turns the common case into
// noise -- a body hitmarker is one branch here.
void play_hitmarker(client_context_t& context, const ::network::snapshot_frame_t& previous,
                    const ::network::snapshot_frame_t& current)
{
  const entities::Player_Entity* me = nullptr;
  for (const entities::Player_Entity& player :
       current.entities.entities_of_type<entities::Player_Entity>())
    if (player.client_slot_index == context.connection.my_slot)
      me = &player;

  // A spectator has no body and no hitmarker; routine, not a failure.
  if (me == nullptr)
    return;

  const entities::Player_Entity* before =
      previous.entities.get<entities::Player_Entity>(me->entity_id);
  if (before == nullptr || me->last_hit_tick <= before->last_hit_tick)
    return;
  if (!stamp_is_recent(me->last_hit_tick, current.tick))
    return;
  if (!me->last_hit_was_headshot)
    return;

  static uint32_t next_variant = 0;
  const assets::sound_asset sound =
      assets::sound_id(HEADSHOT_SOUNDS[next_variant % std::size(HEADSHOT_SOUNDS)]);
  ++next_variant;

  context.audio.play_2d(sound);
}

void play_breaks(client_context_t& context, const ::network::snapshot_frame_t& previous,
                 const ::network::snapshot_frame_t& current)
{
  for (const entities::Damageable_Entity& damageable :
       current.entities.entities_of_type<entities::Damageable_Entity>())
  {
    const entities::Damageable_Entity* before =
        previous.entities.get<entities::Damageable_Entity>(damageable.entity_id);
    const bool just_broke = before != nullptr && before->health.current_health > 0 &&
                            damageable.health.current_health <= 0;
    if (!just_broke)
      continue;

    // The session copy for the position: the frame's is the wire's quantized one.
    const entities::Damageable_Entity* local =
        context.world.session.entity_system.get<entities::Damageable_Entity>(damageable.entity_id);
    if (local != nullptr)
      context.audio.play_3d(assets::sound_id(break_sound_for(local->type)), local->position);
  }
}

// Whoever bounced on it already heard the pop off their own predicted step.
void play_bubble_pops(client_context_t& context, const ::network::snapshot_frame_t& previous,
                      const ::network::snapshot_frame_t& current)
{
  for (const entities::Bubble_Entity& bubble :
       current.entities.entities_of_type<entities::Bubble_Entity>())
  {
    const entities::Bubble_Entity* before =
        previous.entities.get<entities::Bubble_Entity>(bubble.entity_id);
    const bool was_whole = before == nullptr || before->popped_tick == 0;
    if (!was_whole || bubble.popped_tick == 0)
      continue;
    if (!stamp_is_recent(bubble.popped_tick, current.tick))
      continue;
    if (bubble.popped_by == context.connection.my_entity_uid)
      continue;

    context.audio.play_3d(assets::sound_id("bubble_pop"), bubble.position);
  }
}

// A change in play_count means Play ran on the server, and the client plays
// once per change. The switch is a mute for a one-shot, so a Play on a
// disabled emitter bumps the counter and plays nothing here. The sound, the
// reach and the volume are @Editable only, so they are read off the session
// copy, which has the map's.
void play_emitters(client_context_t& context, const ::network::snapshot_frame_t& previous,
                   const ::network::snapshot_frame_t& current)
{
  for (const entities::Sound_Emitter_Entity& emitter :
       current.entities.entities_of_type<entities::Sound_Emitter_Entity>())
  {
    const entities::Sound_Emitter_Entity* local =
        context.world.session.entity_system.get<entities::Sound_Emitter_Entity>(emitter.entity_id);
    if (local == nullptr)
      continue;

    if (local->loop)
    {
      if (!context.replication.loop_emitters_unbuilt_reported)
      {
        context.replication.loop_emitters_unbuilt_reported = true;
        log_warning("sound emitter '{}' (uid {}) loops, and looping emitters are not built yet: "
                    "it stays silent",
                    local->name.c_str(), emitter.entity_id);
      }
      continue;
    }

    const entities::Sound_Emitter_Entity* before =
        previous.entities.get<entities::Sound_Emitter_Entity>(emitter.entity_id);
    if (before == nullptr)
      continue;

    std::vector<voice_handle_t>& voices = context.replication.emitter_voices[emitter.entity_id];

    // Stop before play, so a Stop_Playing and a Play in one tick is a restart.
    const bool switched_off = before->switch_state.value && !emitter.switch_state.value;
    if (emitter.playback.stop_count != before->playback.stop_count || switched_off)
    {
      for (const voice_handle_t voice : voices)
        context.audio.stop(voice);
      voices.clear();
    }

    if (emitter.playback.play_count == before->playback.play_count || !emitter.switch_state.value)
      continue;

    std::erase_if(voices,
                  [&](const voice_handle_t voice) { return !context.audio.is_playing(voice); });
    voices.push_back(local->spatial
                         ? context.audio.play_3d_within(local->sound, local->position,
                                                         local->range, local->volume)
                         : context.audio.play_2d(local->sound, local->volume));
  }
}

// The phase banner. Silent on a round the objective or a request ended:
// Objective_Reached already said it, or the player asked.
void announce_match_edges(const ::network::snapshot_frame_t& previous,
                          const ::network::snapshot_frame_t& current)
{
  Span<const entities::Game_Rules_Entity> before =
      previous.entities.entities_of_type<entities::Game_Rules_Entity>();
  Span<const entities::Game_Rules_Entity> after =
      current.entities.entities_of_type<entities::Game_Rules_Entity>();
  if (before.empty() || after.empty())
    return;

  std::string_view frag_leader_name;
  int32_t most_kills = -1;
  for (const entities::Player_Entity& player :
       current.entities.entities_of_type<entities::Player_Entity>())
  {
    if (player.kills <= most_kills)
      continue;
    most_kills       = player.kills;
    frag_leader_name = player.display_name.c_str();
  }

  const std::string text =
      hud::match_announcement_for(before[0].match, after[0].match, frag_leader_name);
  if (!text.empty())
    hud::set_announcement(text);
}

// The one-shot sounds and the match banner derived from replicated STATE crossing an edge between
// the previous held frame and the one just applied: a gunshot when a player's
// last_fire_tick advances, our own hitmarker when last_hit_tick does, a break
// when a damageable's health crosses zero, a clip when an emitter's play_count
// bumps, a banner when the match's phase or round moves. None of these is an effect on the wire, on purpose: an effect can be
// lost and a crate that broke silently is worse than one that broke a tick
// late, while a field rides the delta against the acked baseline and cannot be.
//
// Both frames are read, never the session copy before and after the write, so
// this is a consumer of the replicated state and not a step of applying it --
// and so the first snapshot after a connect (`previous` null) fires nothing,
// with no seeding flag and no retained stamp to clear. Call once per applied
// snapshot, after the frame is in the session (the session copy is what has
// the map's position, sound and reach).
void apply_snapshot_edges(client_context_t& context,
                          const ::network::snapshot_frame_t* previous,
                          const ::network::snapshot_frame_t& current)
{
  if (previous == nullptr)
    return;

  announce_match_edges(*previous, current);

  play_gunshots(context, *previous, current);
  play_hitmarker(context, *previous, current);
  play_breaks(context, *previous, current);
  play_bubble_pops(context, *previous, current);
  play_emitters(context, *previous, current);
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
    log_terminal("[net] snapshot {}: {}, {} players, {} bytes", server_tick,
                 baseline_tick.has_value() ? std::format("delta from {}", *baseline_tick)
                                           : std::string("full update"),
                 decoded.frame.entities.entities_of_type<entities::Player_Entity>().size(),
                 data_size);
  }

  return decoded;
}

void advance_newest_held_snapshot(client_context_t& context, decoded_snapshot_t&& decoded)
{
  const uint32_t server_tick = decoded.frame.tick;

  const ::network::snapshot_frame_t* previous =
      context.replication.snapshot_history.find(context.replication.latest_processed_tick);

  context.replication.latest_processed_tick = server_tick;

  // blit delta directly onto the session.
  shared::Entity_System& session = context.world.session.entity_system;
  for (entities::entity_type type : entities::replicated_entity_types())
  {
    const shared::Entity_Pool& pool = decoded.frame.entities.pools[(uint32_t)type];
    for (uint32_t slot = 0; slot < pool.count; ++slot)
    {
      const entities::Entity* replicated = pool.at(slot);
      entities::Entity* local = session.try_find(replicated->entity_id);
      if (local == nullptr)
        local = session.add_default(type, replicated->entity_id);
      else if (local->type != type)
      {
        log_error("[CLIENT] snapshot {} carries uid {} as a {}, but the session holds it as a {}; "
                  "record ignored",
                  server_tick, replicated->entity_id, entities::entity_info(type).classname,
                  entities::entity_info(local->type).classname);
        continue;
      }
      if (local != nullptr)
        entities::copy_networked_fields(*replicated, *local);
    }
  }

  for (entities::entity_type type : entities::replicated_entity_types())
  {
    const shared::Entity_Pool& pool = session.pools[(uint32_t)type];
    for (uint32_t slot = pool.count; slot-- > 0;)
    {
      const shared::entity_uid_t uid = pool.at(slot)->entity_id;
      if (decoded.frame.entities.try_find(uid) == nullptr)
        session.destroy(uid);
    }
  }

  // --- 2. Connection facts derived from step 1 ---
  //@NOTE(SJM): this is not a particularly elegant way to do spectating. should it be a different team?
  context.connection.spectating = try_find_my_player(context) == nullptr;
  if (context.connection.spectating)
  {
    // Or the stale uid would keep suppressing our own cosmetic effects for a
    // body we no longer have.
    context.connection.my_entity_uid = shared::null_entity_uid;
  }

  // --- 3. The edge watchers: previous frame against this one ---
  apply_snapshot_edges(context, previous, decoded.frame);

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
  for (std::vector<int>* unanswered : {&context.prediction.unanswered_reveal_light_toggles,
                                       &context.prediction.unanswered_reveal_light_overhead_toggles,
                                       &context.prediction.unanswered_held_lens_toggles})
    std::erase_if(*unanswered,
                  [&](int input_number)
                  {
                    return input_number <= context.prediction.latest_input_number_processed_by_server;
                  });

  // --- 5. Seed prediction from our own body; feed the remotes' rings ---
  for (const entities::Player_Entity& player : session.entities_of_type<entities::Player_Entity>())
  {
    const int32_t slot_index = player.client_slot_index;
    if (slot_index == context.connection.my_slot)
    {
      context.connection.my_entity_uid = player.entity_id;

      context.prediction.local_player_health = player.health.current_health;
      context.prediction.latest_server_position = player.position;
      context.prediction.latest_server_velocity = player.velocity;
      context.prediction.latest_server_movement = player.movement;
      context.prediction.latest_server_tick = server_tick;
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
      // A new occupant of the slot is a new ring by uid; what must not carry
      // across is the death timer.
      if (remote_player.active && remote_player.entity_uid != player.entity_id)
      {
        remote_player = {};
      }

      remote_player.active = true;
      remote_player.slot_index = slot_index;
      remote_player.entity_uid = player.entity_id;

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

  // Every entity with a Render, players included; our own body is predicted
  // ahead and gets no ring. After the player loop, which is what resolves
  // my_entity_uid for this frame.
  client::feed_interpolation_rings(context.replication.interpolated_entities, decoded.frame.entities,
                                   server_tick, context.connection.my_entity_uid);

  // record the live edge tick (as like a horizon to interpolate against)
  client::record_snapshot_tick(
      context.replication.interpolation_cursor, server_tick,
      client::interpolation_delay_in_ticks_from_cvar(context.cvars->cl_interpolation_delay_ticks));

  // remove disconnected players.
  for (auto it = context.replication.remote_players.begin();
       it != context.replication.remote_players.end();)
    it = try_find_player_in_slot(context, it->first) == nullptr
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
