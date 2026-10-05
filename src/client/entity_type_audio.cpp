#include "entity_type_audio.hpp"

namespace client
{

namespace
{

// PLACEHOLDER CONTENT: these are the knife hit sounds, standing in until there
// are bullet-flesh ones. They are the only wet impacts in resources/sounds.
constexpr assets::sound_asset FLESH_IMPACT_SOUNDS[] = {
    assets::sound_asset::knife_hit1,
    assets::sound_asset::knife_hit2,
    assets::sound_asset::knife_hit3,
    assets::sound_asset::knife_hit4,
};

constexpr assets::sound_asset TARGET_BREAK_SOUNDS[] = {
    assets::sound_asset::target_break,
};


constexpr assets::sound_asset NO_IMPACT_SOUND_ON_DISK_YET[] = {assets::sound_asset::Missing};

// A type with NO ROW is a type no shot can land on; a shot on a brush or a mover lands on world geometry.
struct entity_type_sounds_t
{
  entities::entity_type           type;
  Span<const assets::sound_asset> impact;
  assets::sound_asset             break_sound;
};

constexpr entity_type_sounds_t ENTITY_TYPE_SOUNDS[] = {
    {entities::entity_type::Player_Entity, FLESH_IMPACT_SOUNDS, assets::sound_asset::Missing},
    {entities::entity_type::Physics_Body_Entity, NO_IMPACT_SOUND_ON_DISK_YET, assets::sound_asset::Missing},
    {entities::entity_type::Damageable_Entity, TARGET_BREAK_SOUNDS, assets::sound_asset::target_break},
    {entities::entity_type::Jump_Pad_Entity, NO_IMPACT_SOUND_ON_DISK_YET, assets::sound_asset::Missing},
    {entities::entity_type::Logic_Counter_Entity, NO_IMPACT_SOUND_ON_DISK_YET, assets::sound_asset::Missing},
};

constexpr entity_type_sounds_t SOUNDS_OF_A_TYPE_WITH_NO_ROW = {
    entities::entity_type::Invalid, {}, assets::sound_asset::Missing};

constexpr bool every_row_names_a_different_type()
{
  for (const entity_type_sounds_t& row : ENTITY_TYPE_SOUNDS)
  {
    uint32_t rows_naming_this_type = 0;
    for (const entity_type_sounds_t& other : ENTITY_TYPE_SOUNDS)
      rows_naming_this_type += other.type == row.type ? 1 : 0;
    if (rows_naming_this_type != 1)
      return false;
  }
  return true;
}

static_assert(every_row_names_a_different_type(),
              "ENTITY_TYPE_SOUNDS names one entity_type twice -- the lookup returns the first "
              "row, so the second is never played.");

const entity_type_sounds_t& get_sounds_for_entity_type(entities::entity_type type)
{
  for (const entity_type_sounds_t& row : ENTITY_TYPE_SOUNDS)
    if (row.type == type)
      return row;
  return SOUNDS_OF_A_TYPE_WITH_NO_ROW;
}

} // namespace

Span<const assets::sound_asset> impact_sounds_for(entities::entity_type type)
{
  return get_sounds_for_entity_type(type).impact;
}

assets::sound_asset break_sound_for(entities::entity_type type)
{
  return get_sounds_for_entity_type(type).break_sound;
}

} // namespace client
