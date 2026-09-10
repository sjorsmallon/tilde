#include "../../shared/effects/generated/effects_generated.hpp"
#include "../../shared/entities/generated/entities_generated.hpp"
#include "../../shared/hit_region.hpp"
#include "../../shared/log.hpp"
#include "../../shared/span.hpp"
#include "../../shared/weapons.hpp"
#include "../audio/audio_system.hpp"
#include "../client_context.hpp"
#include "../entity_type_audio.hpp"
#include "../weapon_fire_audio.hpp"

#include <optional>

namespace client::effects
{

namespace
{

void play_world_impact(client_context_t& context, const shared::Shot_Impact& data)
{
  const entities::Weapon weapon = static_cast<entities::Weapon>(data.weapon);
  const std::optional<assets::sound_asset> sound = try_world_impact_sound_for(weapon);
  if (!sound)
    return;

  if (shared::WEAPON_DEFINITIONS[weapon].leaves_bullet_impact)
  {
    // The bullet decal, at data.origin facing data.normal: not built yet.
  }

  context.audio->play_3d(*sound, data.origin);
}

void play_type_impact(client_context_t& context, const shared::Shot_Impact& data,
                      entities::entity_type type)
{
  const Span<const assets::sound_asset> variants = impact_sounds_for(type);
  if (variants.count == 0)
  {
    log_error("Shot_Impact landed on uid {} of entity type {}, which no shot can hit -- the "
              "server's target set and ENTITY_TYPE_SOUNDS disagree",
              data.attached_entity, (uint32_t)type);
    return;
  }

  // Cycled rather than randomised: four identical thuds in a row is what makes
  // a sound read as canned, and a counter costs no RNG and no state to seed.
  static uint32_t next_variant = 0;
  const assets::sound_asset sound = variants[next_variant % variants.count];
  ++next_variant;

  // A headshot is louder, not different -- the distinct headshot sound belongs
  // to the shooter's hitmarker, and playing it out loud here would tell the
  // whole server where someone just got clipped in the head.
  const bool headshot = type == entities::entity_type::Player_Entity &&
                        static_cast<shared::hit_region_t>(data.region) == shared::hit_region_t::Head;
  context.audio->play_3d(sound, data.origin, headshot ? 1.0f : 0.8f);
}

bool is_replicated_player(const client_context_t& context, shared::entity_uid_t uid)
{
  for (const auto& [slot_index, player] : context.replication.latest_player_entities)
    if (player.entity_id == uid)
      return true;
  return false;
}

} // namespace

// SHOT_IMPACT: a shot landed. World-space and heard by everyone INCLUDING the
// shooter -- nothing predicts a hit, so there is no "already played locally"
// case to suppress. The shooter's hitmarker is a separate thing and rides
// Player_Entity::last_hit_tick, because it is per-viewer and this is a broadcast.
void on_shot_impact(client_context_t& context, const shared::Shot_Impact& data)
{
  if (!context.audio)
    return;

  if (data.attached_entity == shared::null_entity_uid)
  {
    play_world_impact(context, data);
    return;
  }

  // Map-placed things are in the session; players and physics bodies are
  // replicated rather than placed, so they are looked up where they live.
  const entities::Entity* entity =
      context.world.session.entity_system.try_find(data.attached_entity);
  if (entity != nullptr)
  {
    play_type_impact(context, data, entity->type);
    return;
  }

  if (is_replicated_player(context, data.attached_entity))
  {
    play_type_impact(context, data, entities::entity_type::Player_Entity);
    return;
  }

  if (context.replication.remote_physics_bodies.contains(data.attached_entity))
  {
    play_type_impact(context, data, entities::entity_type::Physics_Body_Entity);
    return;
  }

  log_warning("Shot_Impact names uid {}, which this client holds nothing for", data.attached_entity);
}

} // namespace client::effects
