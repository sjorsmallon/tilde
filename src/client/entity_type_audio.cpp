#include "entity_type_audio.hpp"

namespace client
{

namespace
{

// PLACEHOLDER CONTENT: these are the knife hit sounds, standing in until there
// are bullet-flesh ones. They are the only wet impacts in resources/sounds.
constexpr assets::asset_name_t FLESH_IMPACT_SOUNDS[] = {
    "knife_hit1",
    "knife_hit2",
    "knife_hit3",
    "knife_hit4",
};

constexpr assets::asset_name_t TARGET_BREAK_SOUNDS[] = {"target_break"};

constexpr assets::asset_name_t NO_IMPACT_SOUND_ON_DISK_YET[] = {assets::NO_ASSET_NAME};

// A type with NO ROW is a type no shot can land on; a shot on a brush or a mover lands on world geometry.
struct entity_type_sounds_t
{
  entities::entity_type            type;
  Span<const assets::asset_name_t> impact;
  assets::asset_name_t             break_sound;
};

constexpr entity_type_sounds_t ENTITY_TYPE_SOUNDS[] = {
    {entities::entity_type::Player_Entity, FLESH_IMPACT_SOUNDS, assets::NO_ASSET_NAME},
    {entities::entity_type::Physics_Body_Entity, NO_IMPACT_SOUND_ON_DISK_YET, assets::NO_ASSET_NAME},
    {entities::entity_type::Damageable_Entity, TARGET_BREAK_SOUNDS, "target_break"},
    {entities::entity_type::Jump_Pad_Entity, NO_IMPACT_SOUND_ON_DISK_YET, assets::NO_ASSET_NAME},
    {entities::entity_type::Logic_Counter_Entity, NO_IMPACT_SOUND_ON_DISK_YET, assets::NO_ASSET_NAME},
};

constexpr entity_type_sounds_t SOUNDS_OF_A_TYPE_WITH_NO_ROW = {
    entities::entity_type::Invalid, {}, assets::NO_ASSET_NAME};

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

Span<const assets::asset_name_t> impact_sounds_for(entities::entity_type type)
{
  return get_sounds_for_entity_type(type).impact;
}

assets::asset_name_t break_sound_for(entities::entity_type type)
{
  return get_sounds_for_entity_type(type).break_sound;
}

} // namespace client
