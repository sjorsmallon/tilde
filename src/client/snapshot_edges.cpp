#include "snapshot_edges.hpp"

#include "../shared/log.hpp"
#include "../shared/player_constants.hpp"
#include "audio/audio_system.hpp"
#include "client_context.hpp"
#include "entity_type_audio.hpp"
#include "weapon_fire_audio.hpp"

#include <iterator>

namespace client
{

namespace
{

// A stamp older than this when we first see it advance is not worth playing:
// after a hitch, a long stall or a map load, every advance we were never shown
// lands in one frame and would fire as a burst of backdated sound pointing at
// the wrong moment. 12 ticks is ~200ms at the default 60Hz -- long enough that
// ordinary loss (one or two re-sends) still plays, short enough that a stall
// stays quiet.
constexpr uint32_t MAX_STAMP_AGE_TICKS = 12;

constexpr assets::sound_asset HEADSHOT_SOUNDS[] = {
    assets::sound_asset::headshot1,
    assets::sound_asset::headshot2,
    assets::sound_asset::headshot3,
};

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
       current.entities.entities_of<entities::Player_Entity>())
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
    context.audio->play_3d(*sound, muzzle);
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
       current.entities.entities_of<entities::Player_Entity>())
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
  const assets::sound_asset sound = HEADSHOT_SOUNDS[next_variant % std::size(HEADSHOT_SOUNDS)];
  ++next_variant;

  context.audio->play_2d(sound);
}

void play_breaks(client_context_t& context, const ::network::snapshot_frame_t& previous,
                 const ::network::snapshot_frame_t& current)
{
  for (const entities::Damageable_Entity& damageable :
       current.entities.entities_of<entities::Damageable_Entity>())
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
      context.audio->play_3d(break_sound_for(local->type), local->position);
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
       current.entities.entities_of<entities::Sound_Emitter_Entity>())
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
    if (emitter.playback.play_count == before->playback.play_count || !emitter.switch_state.value)
      continue;

    if (local->spatial)
      context.audio->play_3d_within(local->sound, local->position, local->range, local->volume);
    else
      context.audio->play_2d(local->sound, local->volume);
  }
}

} // namespace

void play_snapshot_edge_audio(client_context_t& context,
                              const ::network::snapshot_frame_t* previous,
                              const ::network::snapshot_frame_t& current)
{
  if (previous == nullptr || !context.audio)
    return;

  play_gunshots(context, *previous, current);
  play_hitmarker(context, *previous, current);
  play_breaks(context, *previous, current);
  play_emitters(context, *previous, current);
}

} // namespace client
