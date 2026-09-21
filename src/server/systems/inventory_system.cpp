#include "inventory_system.hpp"

#include "../../shared/linalg.hpp"
#include "../../shared/log.hpp"
#include "../../shared/physics.hpp"
#include "../../shared/player_constants.hpp"
#include "../../shared/shapes.hpp"
#include "../../shared/weapons.hpp"
#include "../entity_lifecycle.hpp"

#include <Jolt/Physics/Body/BodyInterface.h>

#include <algorithm>
#include <cmath>

namespace server
{

static constexpr float THROW_SPAWN_DISTANCE         = 32.f;
static constexpr float THROW_SPEED                  = 400.f;
static constexpr float THROW_UPWARD_SPEED           = 150.f;
static constexpr float THROW_PICKUP_DELAY_SECONDS   = 0.75f;

// The one place a weapon enters a hand. Every pickup or card-draw goes through here, so
// "which slot does this land in" is answered from the weapon's own definition
// once rather than at each site that hands one out.
//
// Fallible because the spawn is: a full pool is a real outcome and the caller
// gets no uid to record. Displacing whatever the slot held is NOT a failure and
// is not reported -- it is what taking a second rifle means.
//
// Takes the OWNER and its Inventory rather than a Player_Entity: Armable is
// declared `requires Inventory`, so its handler is written once against the
// component and gets exactly these two. Nothing in here ever wanted the rest
// of a player -- the uid is for owner_uid and the component is the slots.
[[nodiscard]] static shared::entity_uid_t spawn_weapon_into_slot(shared::game_session_t& session,
                                                                entities::Entity& owner,
                                                                entities::Inventory& inventory,
                                                                entities::Weapon weapon,
                                                                entities::Damage_Type damage_type)
{
  const shared::weapon_definition_t& definition = shared::get_weapon_definition(weapon);

  const shared::entity_uid_t weapon_uid = session.entity_system.spawn<entities::Weapon_Entity>();

  entities::Weapon_Entity* weapon_entity =
      session.entity_system.get<entities::Weapon_Entity>(weapon_uid);
  if (weapon_entity == nullptr)
  {
    log_error("spawn_weapon_into_slot: spawned {} for {} and could not resolve it",
              to_string(weapon), owner.entity_id);
    return shared::null_entity_uid;
  }

  weapon_entity->weapon_id   = weapon;
  weapon_entity->ammo        = shared::full_magazine_of(definition);
  weapon_entity->owner_uid   = owner.entity_id;
  weapon_entity->damage_type = damage_type;

  inventory.weapons[definition.slot] = weapon_uid;
  return weapon_uid;
}

shared::entity_uid_t try_grant_weapon(server_context_t&     context,
                                     entities::Entity&     owner,
                                     entities::Inventory&  inventory,
                                     entities::Weapon      weapon,
                                     entities::Damage_Type damage_type)
{
  const entities::Inventory_Slot slot      = shared::get_weapon_definition(weapon).slot;
  const shared::entity_uid_t     displaced = inventory.weapons[slot];

  const shared::entity_uid_t granted =
      spawn_weapon_into_slot(context.world.session, owner, inventory, weapon, damage_type);
  if (granted == shared::null_entity_uid)
    return shared::null_entity_uid;

  if (displaced != shared::null_entity_uid)
    destroy_entity(context, displaced);

  return granted;
}

void destroy_inventory(server_context_t& context, shared::entity_uid_t player_uid)
{
  entities::Player_Entity* player =
      context.world.session.entity_system.get<entities::Player_Entity>(player_uid);
  if (player == nullptr)
    return;

  // Copied out first: destroy_entity mutates the Weapon_Entity pool, and the
  // player is read across the loop.
  shared::entity_uid_t carried[enum_traits<entities::Inventory_Slot>::count] = {};
  for (uint32_t index = 0; index < enum_traits<entities::Inventory_Slot>::count; ++index)
  {
    const entities::Inventory_Slot slot = (entities::Inventory_Slot)index;

    carried[index]                  = player->inventory.weapons[slot];
    player->inventory.weapons[slot] = shared::null_entity_uid;
  }

  for (uint32_t index = 0; index < enum_traits<entities::Inventory_Slot>::count; ++index)
    if (carried[index] != shared::null_entity_uid)
      destroy_entity(context, carried[index]);
}

void refill_inventory(shared::game_session_t& session, entities::Player_Entity& player)
{
  for (uint32_t index = 0; index < enum_traits<entities::Inventory_Slot>::count; ++index)
  {
    const entities::Inventory_Slot slot       = (entities::Inventory_Slot)index;
    const shared::entity_uid_t     weapon_uid = player.inventory.weapons[slot];

    // An empty slot is the normal state, not a gap to report: nothing is
    // carrying Utility_1 today and a player who spent a card carries fewer.
    if (weapon_uid == shared::null_entity_uid)
      continue;

    entities::Weapon_Entity* weapon_entity =
        session.entity_system.get<entities::Weapon_Entity>(weapon_uid);
    if (weapon_entity == nullptr)
    {
      log_error("refill_inventory: player {} holds uid {} in {}, which resolves to nothing",
                player.entity_id, weapon_uid, to_string(slot));
      continue;
    }

    reload_magazine(*weapon_entity);
    weapon_entity->next_fire_time = 0;
  }

  player.inventory.deploy_complete_time = 0;
}

void reload_magazine(entities::Weapon_Entity& weapon)
{
  const shared::magazine_t reloaded = shared::reloaded_magazine(
      shared::get_weapon_definition(weapon.weapon_id),
      {.ammo = weapon.ammo, .reserve_ammo = weapon.reserve_ammo});

  weapon.ammo         = reloaded.ammo;
  weapon.reserve_ammo = reloaded.reserve_ammo;
}

entities::Weapon_Entity* try_find_active_weapon(shared::game_session_t&          session,
                                                const entities::Player_Entity& player)
{
  // try_get, not operator[]: active_slot is deserialized with no range check
  // like every other enum field, so indexing it unchecked is an out-of-bounds
  // read driven by a packet.
  const uint32_t* weapon_uid = player.inventory.weapons.try_get(player.inventory.active_slot);
  if (weapon_uid == nullptr || *weapon_uid == shared::null_entity_uid)
    return nullptr;

  return session.entity_system.get<entities::Weapon_Entity>(*weapon_uid);
}

bool carries_weapon(shared::game_session_t& session, const entities::Player_Entity& player,
                    entities::Weapon weapon)
{
  for (const shared::entity_uid_t weapon_uid : player.inventory.weapons)
  {
    if (weapon_uid == shared::null_entity_uid)
      continue;

    const entities::Weapon_Entity* carried =
        session.entity_system.get<entities::Weapon_Entity>(weapon_uid);
    if (carried != nullptr && carried->weapon_id == weapon)
      return true;
  }
  return false;
}

bool try_throw_active_weapon(server_context_t& context, entities::Player_Entity& player,
                             vec3f aim_direction, float tick_dt)
{
  const uint32_t* weapon_uid = player.inventory.weapons.try_get(player.inventory.active_slot);
  if (weapon_uid == nullptr || *weapon_uid == shared::null_entity_uid)
    return false;

  const shared::entity_uid_t thrown_uid = *weapon_uid;
  entities::Weapon_Entity* weapon =
      context.world.session.entity_system.get<entities::Weapon_Entity>(thrown_uid);
  if (weapon == nullptr)
  {
    log_error("try_throw_active_weapon: player {} holds uid {} in {}, which resolves to nothing",
              player.entity_id, thrown_uid, to_string(player.inventory.active_slot));
    return false;
  }

  const vec3f eye      = player.position + vec3f{0.f, shared::player_eye_height, 0.f};
  const vec3f position = eye + aim_direction * THROW_SPAWN_DISTANCE;
  const vec3f velocity =
      player.velocity + aim_direction * THROW_SPEED + vec3f{0.f, THROW_UPWARD_SPEED, 0.f};

  player.inventory.weapons[player.inventory.active_slot] = shared::null_entity_uid;

  weapon->owner_uid           = shared::null_entity_uid;
  weapon->position            = position;
  weapon->pickup_allowed_tick =
      context.tick_number + static_cast<uint32_t>(std::ceil(THROW_PICKUP_DELAY_SECONDS / tick_dt));

  register_dynamic_box(*context.world.physics, thrown_uid, position, weapon->volume.half_extents,
                       velocity);
  return true;
}

// A cube of the box's longest half extent: a thrown weapon tumbles and the volume does not turn with it.
static shared::aabb_bounds_t pickup_bounds_of(const entities::Weapon_Entity& weapon)
{
  const vec3f half_extents = weapon.volume.half_extents;
  const float reach        = std::max({half_extents.x, half_extents.y, half_extents.z});

  entities::Box_Volume cube = weapon.volume;
  cube.half_extents         = {reach, reach, reach};
  return shared::get_bounds(cube, weapon.position);
}

void update_dropped_weapons(server_context_t& context)
{
  shared::game_session_t& session = context.world.session;
  physics_state_t&        physics = *context.world.physics;
  JPH::BodyInterface&     body_interface = physics.physics_system.GetBodyInterface();

  Span<entities::Weapon_Entity> weapons = session.entity_system.entities_of<entities::Weapon_Entity>();
  Span<entities::Player_Entity> players = session.entity_system.entities_of<entities::Player_Entity>();

  for (entities::Weapon_Entity& weapon : weapons)
  {
    if (weapon.owner_uid != shared::null_entity_uid)
      continue;

    const auto body = physics.entity_body_map.find(weapon.entity_id);
    if (body != physics.entity_body_map.end())
    {
      const JPH::RVec3 jolt_position = body_interface.GetCenterOfMassPosition(body->second);
      const JPH::Quat  jolt_rotation = body_interface.GetRotation(body->second);

      weapon.position    = {jolt_position.GetX(), jolt_position.GetY(), jolt_position.GetZ()};
      weapon.orientation = {jolt_rotation.GetX(), jolt_rotation.GetY(), jolt_rotation.GetZ(),
                            jolt_rotation.GetW()};
    }

    if (context.tick_number < weapon.pickup_allowed_tick)
      continue;

    const shared::aabb_bounds_t pickup = pickup_bounds_of(weapon);
    const entities::Inventory_Slot slot = shared::get_weapon_definition(weapon.weapon_id).slot;

    for (entities::Player_Entity& player : players)
    {
      if (player.health.current_health <= 0)
        continue;
      if (player.inventory.weapons[slot] != shared::null_entity_uid)
        continue;

      const shared::aabb_bounds_t hull = shared::player_hull_bounds(player.position);
      if (!linalg::intersect_aabb_aabb(hull.min, hull.max, pickup.min, pickup.max))
        continue;

      unregister_physics_body(physics, weapon.entity_id);
      weapon.owner_uid               = player.entity_id;
      player.inventory.weapons[slot] = weapon.entity_id;
      break;
    }
  }
}

} // namespace server
