#pragma once

namespace client
{

struct client_context_t;

// Plays the shooter's hitmarker when OUR OWN Player_Entity::last_hit_tick
// advances. Call once per received snapshot, right after
// latest_player_entities is refreshed — the same slot as
// update_weapon_fire_audio, and for the same reason.
// Only headshots ding. An ordinary hit already produces FLESH_IMPACT at the
// victim, and a second sound for every body shot turns the common case into
// noise -- if you want a body hitmarker too, it is one branch below.
void play_hitmarker_audio_and_update_hit_tick_state(client_context_t &context);

} // namespace client
