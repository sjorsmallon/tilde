#include "weapon_emancipation_grill_system.hpp"

#include "../../shared/entities/generated/entities/emancipated_weapon_entity_generated.hpp"
#include "../../shared/entities/generated/entities/weapon_emancipation_grill_entity_generated.hpp"
#include "../../shared/entities/generated/entities/weapon_entity_generated.hpp"
#include "../../shared/entity_system.hpp"
#include "../../shared/game_session.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/log.hpp"
#include "../../shared/player_constants.hpp"
#include "../../shared/shapes.hpp"
#include "../entity_lifecycle.hpp"
#include "../server_api.hpp"
#include "../server_context.hpp"
#include "inventory_system.hpp"

#include <vector>

namespace server
{

namespace
{

// Where a weapon taken from the hand hangs: the throw's release point, so it leaves the same way.
constexpr float HAND_DISTANCE = 32.f;

struct taken_weapon_t
{
  shared::entity_uid_t uid;
  vec3f                position;
  quatf                orientation;
};

void leave_emancipated_weapon(server_context_t& context, const taken_weapon_t& taken)
{
  shared::Entity_System& entity_system = context.world.session.entity_system;

  const entities::Weapon_Entity* weapon = entity_system.get<entities::Weapon_Entity>(taken.uid);
  if (weapon == nullptr)
  {
    log_error("emancipation grill: weapon {} was taken and resolves to nothing", taken.uid);
    return;
  }
  const entities::Render render = weapon->render;

  const shared::entity_uid_t fizzled_uid = entity_system.spawn<entities::Emancipated_Weapon_Entity>();
  entities::Emancipated_Weapon_Entity* fizzled =
      entity_system.get<entities::Emancipated_Weapon_Entity>(fizzled_uid);
  if (fizzled == nullptr)
  {
    log_error("emancipation grill: no room to leave weapon {} behind, it vanishes outright", taken.uid);
    return;
  }

  fizzled->position     = taken.position;
  fizzled->orientation  = taken.orientation;
  fizzled->spawned_tick = context.tick_number;
  fizzled->render       = render;
}

} // namespace

void update_weapon_emancipation_grills(server_context_t& context)
{
  shared::game_session_t& session       = context.world.session;
  shared::Entity_System&  entity_system = session.entity_system;

  std::vector<shared::entity_uid_t> stripped_players;
  std::vector<taken_weapon_t>       taken_weapons;

  for (const entities::Weapon_Emancipation_Grill_Entity& grill :
       entity_system.entities_of<entities::Weapon_Emancipation_Grill_Entity>())
  {
    if (!grill.switch_state.value)
      continue;

    const shared::aabb_bounds_t grill_bounds = shared::get_bounds(grill.volume, grill.position);

    for (const entities::Player_Entity& player : entity_system.entities_of<entities::Player_Entity>())
    {
      if (player.health.current_health <= 0)
        continue;
      if (!shared::aabbs_intersect(shared::player_hull_bounds(player.position), grill_bounds))
        continue;

      const quatf aim  = linalg::from_view_angles(player.view_angle_yaw, player.view_angle_pitch);
      const vec3f hand = player.position + vec3f{0.f, shared::player_eye_height, 0.f} +
                         linalg::forward(aim) * HAND_DISTANCE;

      bool carries_anything = false;
      for (uint32_t index = 0; index < enum_traits<entities::Inventory_Slot>::count; ++index)
      {
        const shared::entity_uid_t uid = player.inventory.weapons[(entities::Inventory_Slot)index];
        if (uid == shared::null_entity_uid)
          continue;
        carries_anything = true;
        taken_weapons.push_back({.uid = uid, .position = hand, .orientation = aim});
      }
      if (carries_anything)
        stripped_players.push_back(player.entity_id);
    }

    for (const entities::Weapon_Entity& weapon : entity_system.entities_of<entities::Weapon_Entity>())
    {
      if (weapon.owner_uid != shared::null_entity_uid)
        continue;
      if (shared::aabbs_intersect(shared::get_bounds(weapon.volume, weapon.position), grill_bounds))
        taken_weapons.push_back(
            {.uid = weapon.entity_id, .position = weapon.position, .orientation = weapon.orientation});
    }
  }

  for (const taken_weapon_t& taken : taken_weapons)
    leave_emancipated_weapon(context, taken);

  for (const shared::entity_uid_t player_uid : stripped_players)
  {
    entities::Player_Entity* player = entity_system.get<entities::Player_Entity>(player_uid);
    if (player == nullptr)
      continue;
    cancel_reload(*player);
    destroy_inventory(context, player_uid);
  }

  for (const taken_weapon_t& taken : taken_weapons)
    if (entity_system.try_find(taken.uid) != nullptr)
      destroy_entity(context, taken.uid);

  const float tick_interval_seconds = static_cast<float>(get_tick_interval());
  std::vector<shared::entity_uid_t> expired;
  for (const entities::Emancipated_Weapon_Entity& fizzled :
       entity_system.entities_of<entities::Emancipated_Weapon_Entity>())
  {
    const float elapsed_seconds =
        static_cast<float>(context.tick_number - fizzled.spawned_tick) * tick_interval_seconds;
    if (elapsed_seconds >= fizzled.lifetime_seconds)
      expired.push_back(fizzled.entity_id);
  }
  for (const shared::entity_uid_t uid : expired)
    destroy_entity(context, uid);
}

} // namespace server
