#include "entity_type_audio.hpp"

#include "../shared/array.hpp"

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

// An EMPTY impact list is a type no shot can land on.
struct entity_type_sounds_t
{
  entities::entity_type           type;
  Span<const assets::sound_asset> impact;
  assets::sound_asset             break_sound;
};

constexpr Enum_Array<entities::entity_type, entity_type_sounds_t> ENTITY_TYPE_SOUNDS = {{
    {entities::entity_type::Invalid, {}, assets::sound_asset::Missing},
    {entities::entity_type::Reflection_Volume_Entity, {}, assets::sound_asset::Missing},
    {entities::entity_type::Player_Spawn_Entity, {}, assets::sound_asset::Missing},
    {entities::entity_type::Player_Spectate_Entity, {}, assets::sound_asset::Missing},
    {entities::entity_type::Player_Entity, FLESH_IMPACT_SOUNDS, assets::sound_asset::Missing},
    {entities::entity_type::Weapon_Entity, {}, assets::sound_asset::Missing},
    {entities::entity_type::Rocket_Entity, {}, assets::sound_asset::Missing},
    {entities::entity_type::Particle_Emitter_Entity, {}, assets::sound_asset::Missing},
    {entities::entity_type::Game_Rules_Entity, {}, assets::sound_asset::Missing},
    {entities::entity_type::Damageable_Entity, TARGET_BREAK_SOUNDS, assets::sound_asset::target_break},
    {entities::entity_type::Trigger_Volume_Entity, {}, assets::sound_asset::Missing},
    {entities::entity_type::Sound_Emitter_Entity, {}, assets::sound_asset::Missing},
    {entities::entity_type::Point_Light_Entity, {}, assets::sound_asset::Missing},
    {entities::entity_type::Spot_Light_Entity, {}, assets::sound_asset::Missing},
    {entities::entity_type::Directional_Light_Entity, {}, assets::sound_asset::Missing},
    {entities::entity_type::Physics_Body_Entity, NO_IMPACT_SOUND_ON_DISK_YET, assets::sound_asset::Missing},
    {entities::entity_type::Logic_Counter_Entity, NO_IMPACT_SOUND_ON_DISK_YET, assets::sound_asset::Missing},
    {entities::entity_type::Jump_Pad_Entity, NO_IMPACT_SOUND_ON_DISK_YET, assets::sound_asset::Missing},
}};

static_assert(rows_in_enum_order<&entity_type_sounds_t::type>(ENTITY_TYPE_SOUNDS),
              "ENTITY_TYPE_SOUNDS rows are not in entity_type order -- the lookup indexes "
              "by enum value, so a row out of place plays another type's sound.");

} // namespace

Span<const assets::sound_asset> impact_sounds_for(entities::entity_type type)
{
  return ENTITY_TYPE_SOUNDS[type].impact;
}

assets::sound_asset break_sound_for(entities::entity_type type)
{
  return ENTITY_TYPE_SOUNDS[type].break_sound;
}

} // namespace client
