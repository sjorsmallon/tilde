#include "weapon_fire_audio.hpp"

#include "../shared/array.hpp"
#include "../shared/assets/generated/assets_generated.hpp"
#include "../shared/entities/generated/entities_generated.hpp"
#include "../shared/log.hpp"
#include "../shared/player_constants.hpp"
#include "audio/audio_system.hpp"
#include "client_context.hpp"

namespace client
{

namespace
{

// A shot older than this when we see it is not worth playing: after a hitch, a
// long stall or a map load, the stamps we were never shown all arrive at once
// and would fire as a burst of backdated gunfire. 12 ticks is ~200ms at the
// default 60Hz — long enough that ordinary loss (one or two re-sends) still
// plays, short enough that a stall stays quiet.
constexpr uint32_t max_fire_stamp_age_ticks = 12;

// One entry per Weapon, indexed by the enum. Which sound a weapon makes is a
// CLIENT fact, so it stays here rather than growing a column on the shared
// weapon table that a dedicated server would carry and never read.
//
// An array rather than a switch because a switch only WARNS when a new
// enumerator appears (-Wswitch, and nothing here is built -Werror) — it would
// compile and fail at runtime instead.
//
// Each row NAMES its weapon, and the static_assert below checks that row i is
// weapon i. Enum_Array covers the count on its own; it cannot see a REORDER,
// and reordering the enum in entities.def or inserting a weapon in the middle
// keeps the count right while silently handing the knife the scout's sound.
// The name in the row is what makes that a build error instead of a wrong
// noise.
//
// When a second per-weapon client asset shows up — view model, deploy or
// reload sound (knife_deploy1.wav and scout_clipin/clipout/bolt.wav are
// already on disk with no code path) — this row grows a field rather than the
// file growing a second parallel array.
struct weapon_fire_sound_t
{
  entities::Weapon     weapon;
  assets::sound_asset  sound;
};

constexpr Enum_Array<entities::Weapon, weapon_fire_sound_t> WEAPON_FIRE_SOUNDS = {{
    {entities::Weapon::Knife, assets::sound_asset::knife_slash1},
    {entities::Weapon::Scout, assets::sound_asset::scout_fire_1},
    // No launch sound on disk — rocket_explosion.wav is the detonation, not
    // the firing. Missing is how that content gap is written down now that a
    // sound is an id: there is no path left to misspell, so the row says
    // "nothing yet" rather than naming a file nobody will ever add.
    {entities::Weapon::Rocket_Launcher, assets::sound_asset::Missing},
}};

static_assert(rows_in_enum_order<&weapon_fire_sound_t::weapon>(WEAPON_FIRE_SOUNDS),
              "WEAPON_FIRE_SOUNDS rows are not in Weapon enum order — the lookup indexes "
              "by enum value, so a row out of place plays the wrong weapon's sound.");

} // namespace

std::optional<assets::sound_asset> try_fire_sound_for(entities::Weapon weapon)
{
  // try_get rather than operator[], and this is the part the switch was quietly
  // doing for us. Enum fields are deserialized with no range validation at all
  // -- entity_serialization.cpp's FIELD_TYPE_ENUM memcpys the varint straight
  // into the field -- so last_fire_weapon holds whatever arrived on the wire.
  // Indexing on that unchecked is an out-of-bounds read driven by a packet.
  const weapon_fire_sound_t* row = WEAPON_FIRE_SOUNDS.try_get(weapon);
  if (row == nullptr)
  {
    log_error("try_fire_sound_for: weapon id {} is outside the Weapon enum "
              "(count {}) -- corrupt or hostile snapshot",
              (uint32_t)weapon, WEAPON_FIRE_SOUNDS.size());
    return std::nullopt;
  }
  return row->sound;
}

void update_weapon_fire_audio(client_context_t &context)
{
  for (const auto &[slot_index, player] : context.replication.latest_player_entities)
  {
    // First sight seeds the baseline and never plays. Without this, every
    // player already in the world when we join arrives with a non-zero stamp
    // that reads as "just fired", and connecting sets off a volley.
    auto [entry, inserted] =
        context.replication.last_seen_fire_tick_per_player.try_emplace(player.entity_id,
                                                player.last_fire_tick);
    if (inserted)
      continue;

    if (player.last_fire_tick <= entry->second)
      continue;
    entry->second = player.last_fire_tick;

    // Our own shot already played off prediction in Play_State.
    if (player.entity_id == context.connection.my_entity_uid)
      continue;

    // Guard the subtraction as well as the age: a stamp ahead of the snapshot
    // tick would wrap and read as ancient.
    if (player.last_fire_tick > context.replication.latest_processed_tick)
      continue;
    if (context.replication.latest_processed_tick - player.last_fire_tick >
        max_fire_stamp_age_ticks)
      continue;

    if (!context.audio)
      continue;

    const std::optional<assets::sound_asset> sound =
        try_fire_sound_for(player.last_fire_weapon);
    if (!sound)
      continue;

    // The muzzle is at the eye, matching where the server casts the shot from.
    const vec3f muzzle = player.position +
                         vec3f{0.f, shared::player_eye_height, 0.f};
    context.audio->play_3d(*sound, muzzle);
  }

  // Drop players who left, or the map grows for the life of the session and a
  // slot's new occupant inherits the old one's stamp.
  for (auto it = context.replication.last_seen_fire_tick_per_player.begin();
       it != context.replication.last_seen_fire_tick_per_player.end();)
  {
    bool still_present = false;
    for (const auto &[slot_index, player] : context.replication.latest_player_entities)
    {
      if (player.entity_id == it->first)
      {
        still_present = true;
        break;
      }
    }
    it = still_present ? std::next(it) : context.replication.last_seen_fire_tick_per_player.erase(it);
  }
}

} // namespace client
