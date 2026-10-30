#include "player_sight.hpp"

#include "entities/generated/entities/player_entity_generated.hpp"
#include "entity_system.hpp"
#include "lighting/lens.hpp"
#include "weapon_instance.hpp"

namespace shared
{

namespace
{

std::optional<entities::Reveal_Cone_Kind> wielded_reveal_kind(const Entity_System& system,
                                                              const entities::Player_Entity& player)
{
  const weapon_definition_t* held = try_find_held_weapon_definition(system, player);
  if (held == nullptr)
    return std::nullopt;

  const entities::Fire_Resolution primary = held->primary_fire.resolution;
  if (primary == entities::Fire_Resolution::Reveal_Light)
    return entities::Reveal_Cone_Kind::Reveals;
  if (primary == entities::Fire_Resolution::Erase_Light)
    return entities::Reveal_Cone_Kind::Erases;
  return std::nullopt;
}

} // namespace

player_sight_t player_sight_from_entity(const Entity_System& system, const entities::Player_Entity& player)
{
  const std::optional<entities::Lens_Filter> lens_filter = try_get_held_lens_filter(system, player);
  return {.uid                   = player.entity_id,
          .alive                 = player.health.current_health > 0,
          .feet                  = player.position,
          .view_entering_tick    = {player.view_angle_yaw, player.view_angle_pitch},
          .toggles               = {.reveal_light_on       = player.reveal_light_on,
                                    .reveal_light_overhead = player.reveal_light_overhead,
                                    .held_lens_on          = player.held_lens_on},
          .screen                = {.fov_degrees = player.view_fov_degrees, .aspect = player.view_aspect},
          .wielded_reveal_kind   = wielded_reveal_kind(system, player),
          .wielded_lens_channels = lens_filter ? get_channel_bits_for_lens_filter(*lens_filter) : 0};
}

void player_sights_from_entities(const Entity_System& system, std::vector<player_sight_t>& out)
{
  out.clear();
  for (const entities::Player_Entity& player : system.entities_of_type<entities::Player_Entity>())
    out.push_back(player_sight_from_entity(system, player));
}

} // namespace shared
