#include "systems/launcher_system.hpp"

#include "../shared/entities/generated/entities_generated.hpp"
#include "../shared/entity_system.hpp"
#include "../shared/game_session.hpp"
#include "../shared/hash.hpp"
#include "../shared/log.hpp"
#include "../shared/subtick.hpp"
#include "../shared/weapons.hpp"
#include "server_context.hpp"
#include "spawn_projectile.hpp"

#include <algorithm>

namespace server
{

namespace
{

constexpr uint32_t LAUNCHER_SPREAD_SEED = 0x9e3779b9u;

enum class shot_channel_t : uint32_t
{
  yaw,
  pitch,
  speed,
  flight_seconds,
  rest_seconds,
};

// In [-1, 1], derived from (uid, shots_fired, channel), so every attempt sees the same pattern.
float signed_unit_of_shot(const entities::Launcher_Entity& launcher, shot_channel_t channel)
{
  const uint32_t shot_bits =
      shared::hash_mix(shared::hash_mix(LAUNCHER_SPREAD_SEED, launcher.entity_id), launcher.shots_fired);
  const uint32_t channel_bits =
      channel == shot_channel_t::yaw ? shot_bits
                                     : shared::hash_mix(shot_bits, 0x68bc21ebu + static_cast<uint32_t>(channel) - 1u);
  return 2.f * shared::unit_float_from(channel_bits) - 1.f;
}

float scale_of_shot(const entities::Launcher_Entity& launcher, shot_channel_t channel, float variation)
{
  return std::max(0.f, 1.f + variation * signed_unit_of_shot(launcher, channel));
}

// The aim, turned in the launcher's own frame by an offset inside the two half-angles.
linalg::vec3f aim_of_shot(const entities::Launcher_Entity& launcher)
{
  const float yaw_offset   = signed_unit_of_shot(launcher, shot_channel_t::yaw) * launcher.spread_yaw_degrees;
  const float pitch_offset = signed_unit_of_shot(launcher, shot_channel_t::pitch) * launcher.spread_pitch_degrees;
  return linalg::forward(launcher.orientation * linalg::from_view_angles(yaw_offset, pitch_offset));
}

void vary_timings(const entities::Launcher_Entity& launcher, float& flight_seconds, float& rest_seconds)
{
  flight_seconds *= scale_of_shot(launcher, shot_channel_t::flight_seconds, launcher.flight_seconds_variation);
  rest_seconds *= scale_of_shot(launcher, shot_channel_t::rest_seconds, launcher.rest_seconds_variation);
}

void vary_shot(const entities::Launcher_Entity& launcher, entities::Entity& shot)
{
  if (entities::Projectile* projectile = entities::get_component<entities::Projectile>(&shot))
    projectile->velocity =
        projectile->velocity * scale_of_shot(launcher, shot_channel_t::speed, launcher.speed_variation);

  if (entities::Bubble_Entity* bubble = entities::entity_as<entities::Bubble_Entity>(&shot))
    vary_timings(launcher, bubble->flight_seconds, bubble->rest_seconds);
  else if (entities::Platform_Entity* platform = entities::entity_as<entities::Platform_Entity>(&shot))
    vary_timings(launcher, platform->flight_seconds, platform->solid_seconds);
  else if (entities::Shrinking_Platform_Entity* shrinking =
               entities::entity_as<entities::Shrinking_Platform_Entity>(&shot))
    vary_timings(launcher, shrinking->flight_seconds, shrinking->solid_seconds);
}

} // namespace

void fire_launcher_shot(server_context_t& context, entities::Launcher_Entity& launcher)
{
  // try_get, not [] : the weapon and the trigger came out of a map file.
  const shared::weapon_definition_t* weapon = shared::WEAPON_DEFINITIONS.try_get(launcher.weapon);
  if (weapon == nullptr || (launcher.trigger != entities::Fire_Trigger::Primary &&
                            launcher.trigger != entities::Fire_Trigger::Secondary))
  {
    log_error("launcher {} (uid {}): weapon {} / trigger {} is outside the table, nothing fired",
              launcher.name.c_str(), launcher.entity_id, static_cast<int>(launcher.weapon),
              static_cast<int>(launcher.trigger));
    return;
  }

  if (shared::fire_of(*weapon, launcher.trigger).resolution != entities::Fire_Resolution::Projectile)
  {
    log_warning("launcher {} (uid {}): {}'s {} fire is not a projectile, nothing fired",
                launcher.name.c_str(), launcher.entity_id, weapon->display_name,
                to_string(launcher.trigger));
    return;
  }

  const shared::entity_uid_t shot_uid = spawn_projectile(
      context, launcher.entity_id, *weapon, launcher.position, aim_of_shot(launcher), launcher.trigger);
  if (entities::Entity* shot = context.world.session.entity_system.try_find(shot_uid))
    vary_shot(launcher, *shot);
  ++launcher.shots_fired;
}

void update_launchers(server_context_t& context)
{
  for (entities::Launcher_Entity& launcher :
       context.world.session.entity_system.entities_of<entities::Launcher_Entity>())
  {
    if (!launcher.switch_state.value || launcher.fire_interval_seconds <= 0.f ||
        context.tick_number < launcher.next_fire_tick)
      continue;

    const uint32_t interval_ticks = std::max<uint32_t>(
        1, shared::ticks_from_seconds(launcher.fire_interval_seconds, context.cvars->sv_tickrate));
    launcher.next_fire_tick = context.tick_number + interval_ticks;
    fire_launcher_shot(context, launcher);
  }
}

} // namespace server
