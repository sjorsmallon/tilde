#include "hit_confirm_audio.hpp"

#include "audio/audio_system.hpp"
#include "client_context.hpp"

#include <iterator>

namespace client
{

namespace
{

// Same reasoning as weapon_fire_audio's stamp age: after a hitch or a map load
// the stamps we were never shown all arrive at once, and a backdated ding is
// worse than a missing one because it points at the wrong moment.
constexpr uint32_t max_hit_stamp_age_ticks = 12;

constexpr const char *HEADSHOT_SOUNDS[] = {
    "resources/sounds/headshot1.wav",
    "resources/sounds/headshot2.wav",
    "resources/sounds/headshot3.wav",
};

} // namespace

void play_hitmarker_audio_and_update_hit_tick_state(client_context_t &context)
{
  
  // should this actually be an assert?
  if (!context.audio) return;

  auto my_entity = context.latest_player_entities.find(context.my_slot);

  if (my_entity == context.latest_player_entities.end())
  {
    log_error("tried to play hit confirm audio but our own entity is missing from the latest snapshot.");
    return;
  }

  const entities::Player_Entity &player_entity = my_entity->second;

  // First sight seeds and never plays: joining with a non-zero stamp already
  // on our entity would read as "you just hit someone" the moment we connect.
  if (!context.snapshot_state.hit_tick_seeded)
  {
    context.snapshot_state.last_seen_hit_tick = player_entity.last_hit_tick;
    context.snapshot_state.hit_tick_seeded    = true;
    return;
  }

  if (player_entity.last_hit_tick <=context.snapshot_state.last_seen_hit_tick) return;


  context.snapshot_state.last_seen_hit_tick = player_entity.last_hit_tick;

  // Guard the subtraction as well as the age: a stamp ahead of the snapshot
  // tick would wrap and read as ancient.
  if (player_entity.last_hit_tick > context.snapshot_state.latest_processed_tick) return;
  
  if (context.snapshot_state.latest_processed_tick - player_entity.last_hit_tick > max_hit_stamp_age_ticks) return;

  // body hit is a different sound, and is already played at the victim's location. We only want to play a hitmarker for headshots.
  if (!player_entity.last_hit_was_headshot) return;

  static uint32_t next_variant_idx = 0;
  const char     *sound = HEADSHOT_SOUNDS[next_variant_idx % std::size(HEADSHOT_SOUNDS)];
  ++next_variant_idx;

  // play non-diegetic headshot sound for the shooter. 
  context.audio->play_2d(sound);
}

} // namespace client
