#include "../../shared/entities/generated/entities/launcher_entity_generated.hpp"
#include "../../shared/hash.hpp"
#include "../../shared/log.hpp"
#include "../../shared/weapons.hpp"
#include "../entity_io_context.hpp"
#include "../spawn_projectile.hpp"

namespace entities
{

namespace
{

constexpr uint32_t LAUNCHER_SPREAD_SEED = 0x9e3779b9u;

// The aim, turned in the launcher's own frame by an offset inside the two half-angles.
linalg::vec3f aim_of_shot(const Launcher_Entity& launcher)
{
  const uint32_t yaw_bits =
      shared::hash_mix(shared::hash_mix(LAUNCHER_SPREAD_SEED, launcher.entity_id), launcher.shots_fired);
  const uint32_t pitch_bits = shared::hash_mix(yaw_bits, 0x68bc21ebu);

  const float yaw_offset   = (2.f * shared::unit_float_from(yaw_bits) - 1.f) * launcher.spread_yaw_degrees;
  const float pitch_offset = (2.f * shared::unit_float_from(pitch_bits) - 1.f) * launcher.spread_pitch_degrees;
  return linalg::forward(launcher.orientation * linalg::from_view_angles(yaw_offset, pitch_offset));
}

} // namespace

// The switch is a mute: a disabled launcher swallows its Fire.
void fire(Launcher_Entity& launcher, const Fire_Data&, server::input_context_t& context)
{
  if (!launcher.switch_state.value)
    return;

  // try_get, not [] : the weapon and the trigger came out of a map file.
  const shared::weapon_definition_t* weapon = shared::WEAPON_DEFINITIONS.try_get(launcher.weapon);
  if (weapon == nullptr || (launcher.trigger != Fire_Trigger::Primary && launcher.trigger != Fire_Trigger::Secondary))
  {
    log_error("launcher {} (uid {}): weapon {} / trigger {} is outside the table, nothing fired",
              launcher.name.c_str(), launcher.entity_id, static_cast<int>(launcher.weapon),
              static_cast<int>(launcher.trigger));
    return;
  }

  if (shared::fire_of(*weapon, launcher.trigger).resolution != Fire_Resolution::Projectile)
  {
    log_warning("launcher {} (uid {}): {}'s {} fire is not a projectile, nothing fired",
                launcher.name.c_str(), launcher.entity_id, weapon->display_name,
                to_string(launcher.trigger));
    return;
  }

  server::spawn_projectile(context.server, launcher.entity_id, *weapon, launcher.position,
                           aim_of_shot(launcher), launcher.trigger);
  ++launcher.shots_fired;
}

} // namespace entities
