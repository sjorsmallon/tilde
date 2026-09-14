#include "weapon_fire_audio.hpp"

#include "../shared/assets/generated/assets_generated.hpp"
#include "../shared/entities/generated/entities_generated.hpp"
#include "../shared/log.hpp"
#include "../shared/weapons.hpp"

namespace client
{

namespace
{

// try_get rather than operator[]. Enum fields are deserialized with no range
// validation at all -- entity_serialization.cpp's FIELD_TYPE_ENUM memcpys the
// varint straight into the field -- so last_fire_weapon holds whatever arrived
// on the wire, and Shot_Impact::weapon is a raw u16. Indexing on either
// unchecked is an out-of-bounds read driven by a packet.
const shared::weapon_sounds_t* try_find_weapon_sounds(entities::Weapon weapon)
{
  const shared::weapon_definition_t* row = shared::WEAPON_DEFINITIONS.try_get(weapon);
  if (row == nullptr)
  {
    log_error("weapon id {} is outside the Weapon enum (count {}) -- corrupt or hostile packet",
              (uint32_t)weapon, shared::WEAPON_DEFINITIONS.size());
    return nullptr;
  }
  return &row->sounds;
}

} // namespace

std::optional<assets::sound_asset> try_fire_sound_for(entities::Weapon weapon)
{
  const shared::weapon_sounds_t* row = try_find_weapon_sounds(weapon);
  if (row == nullptr)
    return std::nullopt;
  return row->fire;
}

std::optional<assets::sound_asset> try_world_impact_sound_for(entities::Weapon weapon)
{
  const shared::weapon_sounds_t* row = try_find_weapon_sounds(weapon);
  if (row == nullptr)
    return std::nullopt;
  return row->world_impact;
}

} // namespace client
