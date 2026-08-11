#pragma once

#include "linalg.hpp"
#include "entity_uid.hpp"
#include "network/bitstream.hpp"

#include <cstdint>
#include <vector>

namespace shared
{

// Closed enum of every reliable gameplay event the server can fire. New event
// kinds = new variant in game_event_t + serialize/deserialize case + at least
// one client consumer. Distinct from cosmetic effect_type_t — gameplay events
// are reliable, structured per-kind, and consumed by HUD/score/replay code;
// cosmetic effects are unreliable, fixed-shape, and consumed by FX handlers.
enum class game_event_kind_t : uint16_t
{
  ROCKET_DETONATED,
  PLAYER_DIED,
  PLAYER_SPAWNED,
};

// Per-kind payload structs. Kept trivially copyable; each variant participates
// in the game_event_t union. Sentinel: zero entity_uid_t = "not applicable"
// (e.g. splash-only detonation has no direct victim).
struct rocket_detonated_payload_t
{
  shared::entity_uid_t attacker_id;
  shared::entity_uid_t victim_id;
  uint16_t             weapon_id;
};

// PLAYER_DAMAGED was declared here with a payload and a union member but never
// a codec case and never a consumer, so firing it would have written a bare
// kind id and desynced the rest of the batch. Removed rather than completed:
// per-hit feedback rides the FLESH_IMPACT cosmetic effect (everyone, lossy) and
// Player_Entity::last_hit_tick (the shooter, replicated). Neither needs a
// reliable gameplay event.

// Fired once at the tick a player's health crosses from >0 to <=0. Carries
// enough context for kill feed / score / sound consumers; "world/suicide"
// kills (e.g. trigger volume action_kill) set attacker_id = 0. weapon_id and
// was_headshot are wired through the wire format but unused until the
// underlying gameplay systems exist (no per-weapon ids yet, no headshot
// detection yet).
struct player_died_payload_t
{
  shared::entity_uid_t victim_id;
  shared::entity_uid_t attacker_id;
  uint16_t             weapon_id;
  bool                 was_headshot;
};

// Fired once per player (re)spawn — both initial spawn at connect time and
// timed respawn after death use the same event. Drives death-screen dismiss,
// "you respawned" sound, spawn-in particle effect, future "respawned N times
// this round" counters.
//
// `spawn_position` and `spawn_orientation` carry the authoritative values
// the server just wrote to the player. The schema-replicated position/
// orientation eventually arrives in a snapshot too, but the discrete event
// and the unreliable snapshot ride different channels with different
// ordering — bundling the values inline lets consumers reposition camera /
// particle / sound immediately without waiting for or trusting schema
// arrival. `spawn_orientation` is Euler degrees, matching Entity's
// convention: .y = yaw, .x = pitch, .z = roll (unused for players).
//
// Alternative considered: carry the originating Player_Spawn_Entity's
// entity_id instead of the values, and have the client look up position /
// orientation in its own session. Rejected for now because:
//   - couples consumers to the spawn-entity replication path (we'd have to
//     guarantee Player_Spawn_Entity is on the client at session init; not
//     sure yet whether we want to keep populating those client-side at all,
//     vs. consuming-then-discarding them server-side like map load does)
//   - re-edits to the spawn marker after the server picks it can cause a
//     consumer to read the wrong values
//   - the event grows by one entity_uid_t saved, costs ~24 bytes added —
//     not a meaningful tradeoff
// Revisit if we ever want a "spectator can highlight which spawn was used"
// UI or similar, where the marker identity itself is the signal.
struct player_spawned_payload_t
{
  shared::entity_uid_t player_id;
  linalg::vec3f        spawn_position;
  linalg::vec3f        spawn_orientation;
};

// Tagged union over every event kind. Receivers switch on `kind` and read the
// matching field. Closed at compile time so unknown kinds = log_error+assert,
// never silently dropped (see plan §"Direct client-side dispatch").
struct game_event_t
{
  game_event_kind_t kind;
  union {
    rocket_detonated_payload_t rocket_detonated;
    player_died_payload_t      player_died;
    player_spawned_payload_t   player_spawned;
  };
};

// Wire format mirrors the cosmetic batch: [count:u16] then per-event
// [kind:u16][packed payload]. Reliable transport is the wrapping protobuf
// (S2C_GameEventBatch), not the encoding here.
void serialize_game_event(network::Bit_Writer &writer,
                          const game_event_t &event);

// Aborts (log_error+assert) on unknown kind ids. Server never writes an
// unknown kind and silent drops would hide a bug.
game_event_t deserialize_game_event(network::Bit_Reader &reader);

void serialize_game_event_batch(network::Bit_Writer &writer,
                                const std::vector<game_event_t> &events);

std::vector<game_event_t>
deserialize_game_event_batch(network::Bit_Reader &reader);

} // namespace shared
