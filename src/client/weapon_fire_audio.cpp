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
// Every per-weapon client sound is a field on this row rather than a second
// parallel array -- the deploy and reload sounds (knife_deploy1.wav and
// scout_clipin/clipout/bolt.wav are already on disk with no code path) join it
// the same way.
struct weapon_sounds_t
{
  entities::Weapon     weapon;
  assets::sound_asset  fire;
  // A Shot_Impact on static geometry. Scout has no bullet-on-wall file on disk.
  assets::sound_asset  world_impact;
};

constexpr Enum_Array<entities::Weapon, weapon_sounds_t> WEAPON_SOUNDS = {{
    {entities::Weapon::Knife, assets::sound_asset::knife_slash1, assets::sound_asset::knife_hitwall1},
    {entities::Weapon::Scout, assets::sound_asset::scout_fire_1, assets::sound_asset::Missing},
    // No launch sound on disk — rocket_explosion.wav is the detonation, not
    // the firing. Missing is how that content gap is written down now that a
    // sound is an id: there is no path left to misspell, so the row says
    // "nothing yet" rather than naming a file nobody will ever add.
    {entities::Weapon::Rocket_Launcher, assets::sound_asset::Missing, assets::sound_asset::Missing},
    // Same gap, different reason: a dash is a movement ability held in a slot
    // (generalization_def.md §4), so what it wants is a whoosh rather than a
    // gunshot, and there is no file for one. The row exists because the table
    // is keyed by Weapon and every weapon has to answer.
    {entities::Weapon::Dash, assets::sound_asset::Missing, assets::sound_asset::Missing},
    {entities::Weapon::Swapper, assets::sound_asset::Missing, assets::sound_asset::Missing},
}};

static_assert(rows_in_enum_order<&weapon_sounds_t::weapon>(WEAPON_SOUNDS),
              "WEAPON_SOUNDS rows are not in Weapon enum order — the lookup indexes "
              "by enum value, so a row out of place plays the wrong weapon's sound.");

// try_get rather than operator[], and this is the part the switch was quietly
// doing for us. Enum fields are deserialized with no range validation at all
// -- entity_serialization.cpp's FIELD_TYPE_ENUM memcpys the varint straight
// into the field -- so last_fire_weapon holds whatever arrived on the wire, and
// Shot_Impact::weapon is a raw u16. Indexing on either unchecked is an
// out-of-bounds read driven by a packet.
const weapon_sounds_t* try_find_weapon_sounds(entities::Weapon weapon)
{
  const weapon_sounds_t* row = WEAPON_SOUNDS.try_get(weapon);
  if (row == nullptr)
    log_error("weapon id {} is outside the Weapon enum (count {}) -- corrupt or hostile packet",
              (uint32_t)weapon, WEAPON_SOUNDS.size());
  return row;
}

} // namespace

std::optional<assets::sound_asset> try_fire_sound_for(entities::Weapon weapon)
{
  const weapon_sounds_t* row = try_find_weapon_sounds(weapon);
  if (row == nullptr)
    return std::nullopt;
  return row->fire;
}

std::optional<assets::sound_asset> try_world_impact_sound_for(entities::Weapon weapon)
{
  const weapon_sounds_t* row = try_find_weapon_sounds(weapon);
  if (row == nullptr)
    return std::nullopt;
  return row->world_impact;
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
