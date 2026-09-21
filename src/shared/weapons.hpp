#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>

#include "array.hpp"
#include "entities/generated/entities_generated.hpp"
#include "player_move.hpp"
#include "subtick.hpp"

namespace shared
{

enum class hit_effect_t : uint8_t
{
  Damage,
  Swap,
  Magnet,
  // Re-arms a Movement_Override::Reel on the SHOOTER toward the player it hit, so the pull is predicted.
  Tether,
};

// Speed along the AIM at the moment of the press, and speed straight up. Two
// numbers rather than one direction because the two abilities this exists for
// want opposite halves of it: a Godspeed-shaped dash is aim with a little lift,
// an Elevate-shaped launch is up with none. The lift is not decoration on a
// dash either -- a purely horizontal shove taken while grounded is most of the
// way eaten by ground friction before it is felt.
//
// Under Add the upward half is applied like a jump (it cancels a fall rather
// than summing with it) and the aim half is added, so a dash off a ledge is a
// dash, not a dash minus however long you had been falling.
struct self_impulse_t
{
  impulse_mode_t mode;
  float          along_aim_speed;
  float          upward_speed;
};

// Fire_Resolution::Hitscan's parameters.
struct hitscan_t
{
  float        damage;
  float        headshot_multiplier; // 1.0: no headshots
  float        range;               // knife 50, scout map-length
  // Swap exchanges position and velocity with a player target instead of damaging it.
  hit_effect_t hit_effect;
  // Magnet only: the speed each of the two players gains along the line between them, negative apart.
  float        magnet_speed;
  // Tether only: the reel's speed, and how long ONE hit keeps it alive (a lease the next held hit renews).
  float        tether_speed;
  float        tether_seconds;
  // Whether a Shot_Impact on static geometry leaves a bullet decal, decided by
  // the client -- the effect itself always fires, so the knife still sounds.
  // A BOOL rather than an "is this melee" test: a silenced pistol or a fist
  // would each want the same answer for an unrelated reason.
  bool         leaves_bullet_impact;
};

struct projectile_t
{
  float speed;
  // Fraction of g_gravity acting on the projectile: 0 flies straight, 1 falls like a player.
  float gravity_scale;
  // The entity type a shot becomes; it must carry the Projectile component.
  entities::entity_type spawns;
};

struct projectile_step_t
{
  vec3f position;
  vec3f velocity;
};

// The closed form of constant acceleration, so advancing by dt twice is advancing by 2dt once.
inline projectile_step_t advance_projectile(const projectile_t& projectile, float gravity,
                                            vec3f position, vec3f velocity, float dt)
{
  const vec3f acceleration = {0.f, -gravity * projectile.gravity_scale, 0.f};
  return {.position = position + velocity * dt + acceleration * (0.5f * dt * dt),
          .velocity = velocity + acceleration * dt};
}

// What the weapon sounds like. Client-only facts, on the shared row (see
// above). Missing is a declared absence, logged once per id by the audio
// system, never a silent skip.
struct weapon_sounds_t
{
  assets::sound_asset fire;
  // A Shot_Impact on static geometry. Scout has no bullet-on-wall file on disk.
  assets::sound_asset world_impact;
};

// What ONE button does: the resolution and the parameters it reads. Both
// buttons are this shape, so a secondary hitscan or a secondary projectile is
// the same arm of the same switch as a primary one.
struct weapon_fire_t
{
  entities::Fire_Resolution resolution;
  hitscan_t                 hitscan;
  projectile_t              projectile;
  self_impulse_t            self_impulse;
  // A button that stays down repeats at fire_interval_seconds (try_find_held_fire_time).
  bool                      fires_while_held;
};

struct weapon_definition_t
{
  // Which weapon this row is for. Present so the row can be checked against
  // its own index (see the static_assert below) rather than trusting that
  // whoever last edited this list counted correctly.
  entities::Weapon         weapon;
  const char*              display_name;
  // WHERE this weapon is held when granted. A property of the weapon, so
  // "a scout is a primary" is written once here rather than at every site that
  // hands one out -- which is what stopped the inventory needing to be keyed by
  // weapon identity at all (generalization_def.md §1).
  //
  // Two weapons naming one slot is legal and is what a pickup does: the second
  // one granted replaces the first. Nothing checks for uniqueness, deliberately
  // -- an Enum_Array of slots with two rifles competing for Primary is exactly
  // the loadout question, not a table error.
  entities::Inventory_Slot slot;

  // --- The clocks. Hitscan and Projectile run through them, on either button. ---
  float   fire_interval_seconds;
  // How long after this weapon is RAISED before anything may fire. A property
  // of the weapon, but the gate it feeds is the PLAYER's
  // (Inventory::deploy_complete_time): it blocks every weapon at once, which is
  // what makes a quick switch cost something. The weapon's own
  // fire_interval_seconds keeps running while holstered and is a separate
  // clock -- folding the two together is the bug this file's history is about.
  //
  // The NUMBER IS AUTHORED HERE, not derived from an animation length. Source 1
  // took it from SequenceDuration(), which meant re-exporting a weapon's anims
  // silently reshuffled its timings; Source 2 moving weapon timings into .vdata
  // is Valve walking that back. A future draw animation is authored against
  // this, not the reverse.
  float   deploy_duration_seconds;
  // 0 means NO MAGAZINE: the weapon never consumes ammo and never reloads, and
  // the reload edge is a no-op on it. That is the knife, and it is a real
  // state rather than an unfilled row -- the static_assert below catches the
  // unfilled row, since a zeroed tail row is not at its own index.
  int32_t magazine_size;
  float   reload_duration_seconds;

  // The left and the right mouse button. The switch in resolve_player_shot is
  // over one of these and reads exactly one of its three structs. Zoom is a
  // secondary only: the client toggles its FOV off it and nothing else.
  weapon_fire_t primary_fire;
  weapon_fire_t secondary_fire;
  // Seconds before EITHER impulse may be taken again. THE gate -- see
  // try_apply_self_impulse and the static_assert below for why it is not
  // fire_interval_seconds. Shared by both buttons, because Movement carries one
  // countdown and a second one is a second thing the replay has to restart.
  float         self_impulse_cooldown_seconds;

  weapon_sounds_t sounds;
};

// Indexed by entities::Weapon, so entry N is the weapon whose enum value is N
// and the order here must track the .def's, not Fire_Resolution's.
inline constexpr Enum_Array<entities::Weapon, weapon_definition_t> WEAPON_DEFINITIONS = {{
    {.weapon                  = entities::Weapon::Knife,
     .display_name            = "Knife",
     .slot                    = entities::Inventory_Slot::Melee,
     .fire_interval_seconds   = 0.5f,
     .deploy_duration_seconds = 0.4f,
     .magazine_size           = 0,
     .reload_duration_seconds = 0.f,
     .primary_fire            = {.resolution = entities::Fire_Resolution::Hitscan,
                                 .hitscan    = {.damage               = 50.f,
                                                .headshot_multiplier  = 1.0f,
                                                .range                = 50.f,
                                                .hit_effect           = hit_effect_t::Damage,
                                                .leaves_bullet_impact = false}},
     .sounds                  = {.fire         = assets::sound_asset::knife_slash1,
                                 .world_impact = assets::sound_asset::knife_hitwall1}},
    {.weapon                  = entities::Weapon::Scout,
     .display_name            = "Scout",
     .slot                    = entities::Inventory_Slot::Primary,
     .fire_interval_seconds   = 1.25f,
     .deploy_duration_seconds = 0.7f,
     .magazine_size           = 10,
     .reload_duration_seconds = 2.0f,
     .primary_fire            = {.resolution = entities::Fire_Resolution::Hitscan,
                                 .hitscan    = {.damage               = 60.f,
                                                .headshot_multiplier  = 2.0f,
                                                .range                = 10000.f,
                                                .hit_effect           = hit_effect_t::Damage,
                                                .leaves_bullet_impact = true}},
     .secondary_fire          = {.resolution = entities::Fire_Resolution::Zoom},
     .sounds                  = {.fire         = assets::sound_asset::scout_fire_1,
                                 .world_impact = assets::sound_asset::Missing}},
    {.weapon                  = entities::Weapon::Rocket_Launcher,
     .display_name            = "Rocket Launcher",
     .slot                    = entities::Inventory_Slot::Secondary,
     .fire_interval_seconds   = 0.1f,
     .deploy_duration_seconds = 0.9f,
     .magazine_size           = 0,
     .reload_duration_seconds = 2.5f,
     .primary_fire            = {.resolution = entities::Fire_Resolution::Projectile,
                                 .projectile = {.speed         = 600.f,
                                                .gravity_scale = 0.f,
                                                .spawns = entities::entity_type::Rocket_Entity}},
     .sounds                  = {.fire         = assets::sound_asset::Missing,
                                 .world_impact = assets::sound_asset::Missing}},
    {.weapon                        = entities::Weapon::Dash,
     .display_name                  = "Dash",
     .slot                          = entities::Inventory_Slot::Utility_1,
     .fire_interval_seconds         = 0.f,
     .deploy_duration_seconds       = 0.f,
     .magazine_size                 = 0,
     .reload_duration_seconds       = 0.f,
     .primary_fire                  = {.resolution   = entities::Fire_Resolution::Self_Impulse,
                                       .self_impulse = {.mode            = impulse_mode_t::Add,
                                                        .along_aim_speed = 900.f,
                                                        .upward_speed    = 150.f}},
     // The right mouse button is the same dash with the velocity REPLACED
     // rather than added, so the two can be felt side by side on one key each.
     .secondary_fire                = {.resolution   = entities::Fire_Resolution::Self_Impulse,
                                       .self_impulse = {.mode            = impulse_mode_t::Set,
                                                        .along_aim_speed = 900.f,
                                                        .upward_speed    = 0.f}},
     .self_impulse_cooldown_seconds = 1.5f,
     .sounds                        = {.fire         = assets::sound_asset::gust_of_wind,
                                       .world_impact = assets::sound_asset::Missing}},
    {.weapon                  = entities::Weapon::Swapper,
     .display_name            = "Swapper",
     .slot                    = entities::Inventory_Slot::Utility_2,
     .fire_interval_seconds   = 2.f,
     .deploy_duration_seconds = 0.f,
     .magazine_size           = 0,
     .reload_duration_seconds = 0.f,
     .primary_fire            = {.resolution = entities::Fire_Resolution::Hitscan,
                                 .hitscan    = {.damage               = 0.f,
                                                .headshot_multiplier  = 1.0f,
                                                .range                = 10000.f,
                                                .hit_effect           = hit_effect_t::Swap,
                                                .leaves_bullet_impact = false}},
     .sounds                  = {.fire         = assets::sound_asset::Missing,
                                 .world_impact = assets::sound_asset::Missing}},
    // The secondary is the same hook, fired as the reel (Hook_Entity::reels_target).
    {.weapon                  = entities::Weapon::Hook,
     .display_name            = "Hook",
     .slot                    = entities::Inventory_Slot::Secondary,
     .fire_interval_seconds   = 0.1f,
     .deploy_duration_seconds = 0.f,
     .magazine_size           = 0,
     .reload_duration_seconds = 2.5f,
     .primary_fire            = {.resolution = entities::Fire_Resolution::Projectile,
                                 .projectile = {.speed         = 850.f,
                                                .gravity_scale = 0.f,
                                                .spawns = entities::entity_type::Hook_Entity}},
     .secondary_fire          = {.resolution = entities::Fire_Resolution::Projectile,
                                 .projectile = {.speed         = 850.f,
                                                .gravity_scale = 0.f,
                                                .spawns = entities::entity_type::Hook_Entity}},
     .sounds                  = {.fire         = assets::sound_asset::Missing,
                                 .world_impact = assets::sound_asset::Missing}},
    {.weapon                  = entities::Weapon::Bubble,
     .display_name            = "Bubble",
     .slot                    = entities::Inventory_Slot::Secondary,
     .fire_interval_seconds   = 0.1f,
     .deploy_duration_seconds = 0.f,
     .magazine_size           = 0,
     .reload_duration_seconds = 2.5f,
     .primary_fire            = {.resolution = entities::Fire_Resolution::Projectile,
                                 .projectile = {.speed         = 500.f,
                                                .gravity_scale = -0.5f,
                                                .spawns = entities::entity_type::Bubble_Entity}},
     .sounds                  = {.fire         = assets::sound_asset::Missing,
                                 .world_impact = assets::sound_asset::Missing}},
    {.weapon                  = entities::Weapon::Kooh,
     .display_name            = "Kooh",
     .slot                    = entities::Inventory_Slot::Secondary,
     .fire_interval_seconds   = 0.1f,
     .deploy_duration_seconds = 0.f,
     .magazine_size           = 0,
     .reload_duration_seconds = 2.5f,
     .primary_fire            = {.resolution = entities::Fire_Resolution::Projectile,
                                 .projectile = {.speed         = 850.f,
                                                .gravity_scale = 0.f,
                                                .spawns = entities::entity_type::Kooh_Entity}},
     .secondary_fire          = {.resolution = entities::Fire_Resolution::Projectile,
                                 .projectile = {.speed         = 850.f,
                                                .gravity_scale = 0.f,
                                                .spawns = entities::entity_type::Kooh_Entity}},
     .sounds                  = {.fire         = assets::sound_asset::Missing,
                                 .world_impact = assets::sound_asset::Missing}},

    {.weapon                  = entities::Weapon::Magnet,
     .display_name            = "Magnet",
     .slot                    = entities::Inventory_Slot::Primary,
     .fire_interval_seconds   = 0.5f,
     .deploy_duration_seconds = 0.7f,
     .magazine_size           = 0,
     .reload_duration_seconds = 0.f,
     .primary_fire            = {.resolution = entities::Fire_Resolution::Hitscan,
                                 .hitscan    = {.damage               = 0.f,
                                                .headshot_multiplier  = 1.0f,
                                                .range                = 1500.f,
                                                .hit_effect           = hit_effect_t::Magnet,
                                                .magnet_speed         = 600.f,
                                                .leaves_bullet_impact = false}},
     .secondary_fire          = {.resolution = entities::Fire_Resolution::Hitscan,
                                 .hitscan    = {.damage               = 0.f,
                                                .headshot_multiplier  = 1.0f,
                                                .range                = 1500.f,
                                                .hit_effect           = hit_effect_t::Magnet,
                                                .magnet_speed         = -600.f,
                                                .leaves_bullet_impact = false}},
     .sounds                  = {.fire         = assets::sound_asset::scout_fire_1,
                                 .world_impact = assets::sound_asset::Missing}},
    // The fiddle row: both buttons repeat while held, a tether toward the player hit and a bullet stream.
    {.weapon                  = entities::Weapon::Mock,
     .display_name            = "Mock",
     .slot                    = entities::Inventory_Slot::Primary,
     .fire_interval_seconds   = 0.1f,
     .deploy_duration_seconds = 0.f,
     .magazine_size           = 0,
     .reload_duration_seconds = 0.f,
     .primary_fire            = {.resolution       = entities::Fire_Resolution::Hitscan,
                                 .hitscan          = {.damage               = 0.f,
                                                      .headshot_multiplier  = 1.0f,
                                                      .range                = 3000.f,
                                                      .hit_effect           = hit_effect_t::Tether,
                                                      .tether_speed         = 600.f,
                                                      .tether_seconds       = 0.25f,
                                                      .leaves_bullet_impact = false},
                                 .fires_while_held = true},
     .secondary_fire          = {.resolution       = entities::Fire_Resolution::Hitscan,
                                 .hitscan          = {.damage               = 8.f,
                                                      .headshot_multiplier  = 2.0f,
                                                      .range                = 10000.f,
                                                      .hit_effect           = hit_effect_t::Damage,
                                                      .leaves_bullet_impact = true},
                                 .fires_while_held = true},
     .sounds                  = {.fire         = assets::sound_asset::Missing,
                                 .world_impact = assets::sound_asset::Missing}},
    {.weapon                  = entities::Weapon::Platform,
     .display_name            = "Platform",
     .slot                    = entities::Inventory_Slot::Secondary,
     .fire_interval_seconds   = 1.0f,
     .deploy_duration_seconds = 0.f,
     .magazine_size           = 0,
     .reload_duration_seconds = 0.f,
     .primary_fire            = {.resolution = entities::Fire_Resolution::Projectile,
                                 .projectile = {.speed         = 700.f,
                                                .gravity_scale = 0.f,
                                                .spawns = entities::entity_type::Platform_Entity}},
     .sounds                  = {.fire         = assets::sound_asset::Missing,
                                 .world_impact = assets::sound_asset::Missing}},
}};

// The one check, and it has to carry both failures.
//
// Enum_Array sizes the storage from Weapon_COUNT, so the size can no longer
// disagree -- but it does not fill it. Add a weapon to the .def and this
// initializer is one row short, which value-initializes the tail rather than
// failing: the new weapon gets 0 damage and a null display_name, exactly the
// silence the old deduce-the-size-then-compare spelling was avoiding. And a
// REORDER -- swap two rows, or insert a weapon in the middle -- keeps every
// count right while every lookup returns a neighbour's stats.
//
// Each row names its own weapon, so both show up as a row that is not at its
// own index (a zeroed tail row reads as Knife, which is not where it sits).
static_assert(rows_in_enum_order<&weapon_definition_t::weapon>(WEAPON_DEFINITIONS),
              "WEAPON_DEFINITIONS rows are not in Weapon enum order: get_weapon_definition "
              "indexes by enum value, so a row out of place returns another weapon's stats.");

constexpr bool fire_passes_through_shot_clocks(const weapon_fire_t& fire)
{
  return fire.resolution == entities::Fire_Resolution::Hitscan ||
         fire.resolution == entities::Fire_Resolution::Projectile;
}

// THE SECOND CHECK: a self-impulse must be gated by exactly one clock.
//
// Its gate is Movement::seconds_until_impulse_ready, because that is the only
// per-player state a client's reconciliation replay restarts from -- the two
// clocks that gate every other weapon (Weapon_Entity::next_fire_time and
// Inventory::deploy_complete_time) are both server-only, so a client that
// predicted a dash against either would be predicting against a number it does
// not have. See try_apply_self_impulse.
//
// An impulse on either button never passes through the shot clocks, so a row
// on which NEITHER button does must carry zero clocks and no magazine: those
// are numbers nothing reads, and the day one is read beside the movement gate
// it is two gates that agree today and drift the first time one is tuned. A
// cooldown on a row with no impulse is the same silence the other way round.
//
// The day a self-impulse genuinely wants a raise time, what earns it is a
// PREDICTED deploy clock in the replay, not a second gate here.
constexpr uint32_t first_self_impulse_row_gated_by_more_than_movement()
{
  for (const weapon_definition_t& definition : WEAPON_DEFINITIONS)
  {
    const uint32_t row = static_cast<uint32_t>(definition.weapon);
    const bool any_impulse =
        definition.primary_fire.resolution == entities::Fire_Resolution::Self_Impulse ||
        definition.secondary_fire.resolution == entities::Fire_Resolution::Self_Impulse;
    const bool any_shot = fire_passes_through_shot_clocks(definition.primary_fire) ||
                          fire_passes_through_shot_clocks(definition.secondary_fire);

    if (any_impulse != (definition.self_impulse_cooldown_seconds > 0.f))
      return row;

    if (!any_shot &&
        (definition.fire_interval_seconds != 0.f || definition.deploy_duration_seconds != 0.f ||
         definition.magazine_size != 0))
      return row;
  }
  return entities::Weapon_COUNT;
}

static_assert(first_self_impulse_row_gated_by_more_than_movement() == entities::Weapon_COUNT,
              "THE LEFT NUMBER BELOW IS THE OFFENDING ROW'S Weapon VALUE. "
              "a row firing a self-impulse on either button carries a positive "
              "self_impulse_cooldown_seconds and a row with none carries zero, and a row on "
              "which no button is Hitscan or Projectile carries zero fire_interval_seconds, "
              "zero deploy_duration_seconds and no magazine: the only gate an impulse has is "
              "Movement::seconds_until_impulse_ready, which is the only one the client can "
              "replay.");

// THE THIRD CHECK: each button's parameters match its resolution. The struct a
// resolution reads must be filled and the ones it does not read must be zero
// -- a Swap on a Projectile fire or a range on a Self_Impulse fire is a number
// nothing reads, which is the same silence as an unfilled row. None and Zoom
// read nothing. The primary may be neither: an unfilled primary is a zeroed
// one, and Zoom is the client's right-click toggle.
constexpr bool fire_parameters_match_resolution(const weapon_fire_t& fire)
{
  const hitscan_t&      hitscan    = fire.hitscan;
  const projectile_t&   projectile = fire.projectile;
  const self_impulse_t& impulse    = fire.self_impulse;
  const bool hitscan_is_zero = hitscan.damage == 0.f && hitscan.headshot_multiplier == 0.f &&
                               hitscan.range == 0.f && hitscan.hit_effect == hit_effect_t::Damage &&
                               hitscan.magnet_speed == 0.f && hitscan.tether_speed == 0.f &&
                               hitscan.tether_seconds == 0.f && !hitscan.leaves_bullet_impact;
  const bool magnet_speed_matches_effect =
      (hitscan.hit_effect == hit_effect_t::Magnet) == (hitscan.magnet_speed != 0.f);
  const bool tether_matches_effect =
      hitscan.hit_effect == hit_effect_t::Tether
          ? hitscan.tether_speed > 0.f && hitscan.tether_seconds > 0.f
          : hitscan.tether_speed == 0.f && hitscan.tether_seconds == 0.f;
  const bool projectile_is_zero = projectile.speed == 0.f && projectile.gravity_scale == 0.f &&
                                  projectile.spawns == entities::entity_type::Invalid;
  const bool impulse_is_zero = impulse.along_aim_speed == 0.f && impulse.upward_speed == 0.f;

  switch (fire.resolution)
  {
  case entities::Fire_Resolution::None:
  case entities::Fire_Resolution::Zoom:
    return hitscan_is_zero && projectile_is_zero && impulse_is_zero && !fire.fires_while_held;
  case entities::Fire_Resolution::Hitscan:
    return hitscan.range > 0.f && hitscan.headshot_multiplier > 0.f &&
           magnet_speed_matches_effect && tether_matches_effect && projectile_is_zero &&
           impulse_is_zero;
  case entities::Fire_Resolution::Projectile:
    return projectile.speed > 0.f && projectile.spawns != entities::entity_type::Invalid &&
           hitscan_is_zero && impulse_is_zero;
  case entities::Fire_Resolution::Self_Impulse:
    return hitscan_is_zero && projectile_is_zero && !fire.fires_while_held;
  }
  return false;
}

constexpr uint32_t first_row_whose_parameters_mismatch_its_resolution()
{
  for (const weapon_definition_t& definition : WEAPON_DEFINITIONS)
  {
    const uint32_t row = static_cast<uint32_t>(definition.weapon);
    if (definition.primary_fire.resolution == entities::Fire_Resolution::None ||
        definition.primary_fire.resolution == entities::Fire_Resolution::Zoom)
      return row;
    if (!fire_parameters_match_resolution(definition.primary_fire) ||
        !fire_parameters_match_resolution(definition.secondary_fire))
      return row;
    if ((definition.primary_fire.fires_while_held || definition.secondary_fire.fires_while_held) &&
        !(definition.fire_interval_seconds > 0.f))
      return row;
    for (const weapon_fire_t* fire : {&definition.primary_fire, &definition.secondary_fire})
      if (fire->fires_while_held && fire->hitscan.hit_effect == hit_effect_t::Tether &&
          !(fire->hitscan.tether_seconds > definition.fire_interval_seconds))
        return row;
  }
  return entities::Weapon_COUNT;
}

static_assert(first_row_whose_parameters_mismatch_its_resolution() == entities::Weapon_COUNT,
              "THE LEFT NUMBER BELOW IS THE OFFENDING ROW'S Weapon VALUE. "
              "each fire of a WEAPON_DEFINITIONS row fills the parameter struct of its own "
              "Fire_Resolution (hitscan: positive range and headshot_multiplier; projectile: "
              "positive speed and a spawned entity type) and leaves the others zero, and the "
              "primary is neither None nor Zoom: resolve_player_shot reads exactly one struct, "
              "so a value in another is a number nothing reads. fires_while_held is for a "
              "Hitscan or Projectile fire on a row with a positive fire_interval_seconds: "
              "with no interval a held trigger fires once per sub-tick step, which is a rate "
              "set by how many edges the tick had. A held Tether's tether_seconds is LONGER "
              "than the fire interval: it is a lease the next hit renews, and one that runs "
              "out between two hits drops the reel ten times a second.");

constexpr const weapon_definition_t& get_weapon_definition(entities::Weapon id)
{
  assert(static_cast<uint32_t>(id) < WEAPON_DEFINITIONS.size() &&
         "get_weapon_definition on an id with no table entry");
  return WEAPON_DEFINITIONS[id];
}

// The half of the row a button reads.
constexpr const weapon_fire_t& fire_of(const weapon_definition_t& weapon,
                                       entities::Fire_Trigger trigger)
{
  switch (trigger)
  {
  case entities::Fire_Trigger::Primary:   return weapon.primary_fire;
  case entities::Fire_Trigger::Secondary: return weapon.secondary_fire;
  }
  return weapon.primary_fire;
}

// When a button that was ALREADY down fires inside [step_start, step_end): the moment both clocks open, never rounded to the step.
[[nodiscard]] constexpr std::optional<subtick_time_t>
try_find_held_fire_time(const weapon_fire_t& fire, subtick_time_t next_fire_time,
                        subtick_time_t deploy_complete_time, subtick_time_t step_start,
                        subtick_time_t step_end)
{
  if (!fire.fires_while_held)
    return std::nullopt;

  const subtick_time_t fire_time = std::max({step_start, next_fire_time, deploy_complete_time});
  if (fire_time >= step_end)
    return std::nullopt;
  return fire_time;
}

// Negative ammo or reserve is UNLIMITED; both are per weapon INSTANCE and authored in the map.
inline constexpr int32_t UNLIMITED_AMMO = -1;

constexpr int32_t full_magazine_of(const weapon_definition_t& weapon)
{
  return weapon.magazine_size > 0 ? weapon.magazine_size : UNLIMITED_AMMO;
}

constexpr bool ammo_allows_a_shot(int32_t ammo)
{
  return ammo != 0;
}

constexpr bool reload_may_start(const weapon_definition_t& weapon, int32_t ammo,
                                int32_t reserve_ammo)
{
  return weapon.magazine_size > 0 && ammo >= 0 && ammo < weapon.magazine_size &&
         reserve_ammo != 0;
}

struct magazine_t
{
  int32_t ammo;
  int32_t reserve_ammo;
};

constexpr magazine_t reloaded_magazine(const weapon_definition_t& weapon, magazine_t magazine)
{
  if (!reload_may_start(weapon, magazine.ammo, magazine.reserve_ammo))
    return magazine;

  const int32_t missing = weapon.magazine_size - magazine.ammo;
  if (magazine.reserve_ammo < 0)
    return {.ammo = weapon.magazine_size, .reserve_ammo = magazine.reserve_ammo};

  const int32_t moved = missing < magazine.reserve_ammo ? missing : magazine.reserve_ammo;
  return {.ammo = magazine.ammo + moved, .reserve_ammo = magazine.reserve_ammo - moved};
}

// How a live projectile flies: the row that fired it, the button it came off.
inline const projectile_t& projectile_parameters_of(const entities::Projectile& projectile)
{
  return fire_of(get_weapon_definition(projectile.weapon_id), projectile.trigger).projectile;
}

// ---------------------------------------------------------------------------
// Self-impulse
// ---------------------------------------------------------------------------
//
// Push the shooter along their own aim. THE shared half of
// Fire_Resolution::Self_Impulse, and it has to be shared: this is the one
// resolution whose outcome lands on the player who pressed the button, so the
// client predicts it and the server simulates it, and a second spelling of the
// arithmetic is a rubber-band on every dash.
//
// Three callers, all inside a sub-tick step loop at the trigger edge: the
// server's resolve_player_shot, the client's live prediction, and the client's
// reconciliation replay. The replay is why every input and output here is
// either the weapon's own table row or Movement -- there is nothing else a
// replay holds.
//
// `aim_direction` must be normalized; it is the step's view direction, so the
// dash goes where the player was looking at the press rather than wherever the
// mouse finished the tick.
//
// The TRIGGER picks the row half, primary_fire or secondary_fire. A button
// whose half is not an impulse is refused here rather than by the
// caller, which is what lets every site call this off whatever is in the hand.
[[nodiscard]] inline bool try_apply_self_impulse(const movement_settings_t& settings,
                                                 const weapon_definition_t& weapon,
                                                 entities::Fire_Trigger trigger,
                                                 const vec3f& aim_direction,
                                                 entities::Movement& movement,
                                                 vec3f& velocity)
{
  const weapon_fire_t& fire = fire_of(weapon, trigger);
  if (fire.resolution != entities::Fire_Resolution::Self_Impulse)
    return false;
  const self_impulse_t* impulse = &fire.self_impulse;

  if (movement.seconds_until_impulse_ready > 0.f)
    return false;
  
  // The row states WHAT the press wants and the model decides where it lands.
  // The vertical half is the one that differs from the horizontal: an Add
  // dash REPLACES a fall (max(vy, 0) + lift) rather than summing with it.
  vec3f wanted = aim_direction * impulse->along_aim_speed;
  switch (impulse->mode)
  {
  case impulse_mode_t::Keep:
    break;

  case impulse_mode_t::Add:
  {
    impulse_t added{.horizontal = impulse_mode_t::Add,
                    .vertical   = impulse_mode_t::Add,
                    .velocity   = wanted};
    if (impulse->upward_speed > 0.f)
    {
      added.vertical   = impulse_mode_t::Set;
      added.velocity.y = std::max(velocity.y + wanted.y, 0.f) + impulse->upward_speed;
    }
    apply_impulse(settings, velocity, movement, added);
    break;
  }

  case impulse_mode_t::Set:
    wanted.y += impulse->upward_speed;
    apply_impulse(settings, velocity, movement,
                  {.horizontal = impulse_mode_t::Set,
                   .vertical   = impulse_mode_t::Set,
                   .velocity   = wanted});
    break;
  }

  movement.seconds_until_impulse_ready = weapon.self_impulse_cooldown_seconds;
  return true;
}

// ---------------------------------------------------------------------------
// Which key equips what
// ---------------------------------------------------------------------------
//
// SHARED because both sides run it, and they must run the same one: the server
// applies the switch in its step loop and the client predicts the deploy clock
// off that same edge. Spelled out twice, "Key1 means Primary" would be two
// answers to one question, and the failure is a client counting down a deploy
// for a weapon the server never raised.
//
// A key selects a SLOT, not a weapon type, and that is the same correction the
// inventory itself took (generalization_def.md §1): which key equips what is a
// statement about the hand, not about the gun. Binding to a weapon type made an
// EMPTY slot unrepresentable -- Key1 meant "equip the scout" and had nothing to
// say about a player who is not carrying one, so a hand with a rifle in it
// could only be reached by adding a rifle row. Now Key1 means "hold whatever is
// in Primary", which is also the only spelling a pickup can work with.
//
// It is NOT keyed by the Inventory_Slot enum, and deliberately so: which key
// equips what is a binding, and the two orders are independent. A slot with no
// key is a missing row rather than a hole in an Enum_Array.
struct weapon_select_binding_t
{
  uint64_t                 button;
  entities::Inventory_Slot slot;
};

inline constexpr Array<weapon_select_binding_t, 5> WEAPON_SELECT_BINDINGS = {{
    {Button::Key1, entities::Inventory_Slot::Primary},
    {Button::Key2, entities::Inventory_Slot::Secondary},
    {Button::Key3, entities::Inventory_Slot::Melee},
    {Button::Key4, entities::Inventory_Slot::Utility_1},
    {Button::Key5, entities::Inventory_Slot::Utility_2},
}};

// The inventory slot a step's newly-pressed buttons equip, or nothing when none
// of them is bound to one. Number keys with no binding land here as nothing,
// which is what stops an unbound Key6 cancelling a reload on one side and not
// the other.
//
// Several weapon keys inside ONE SUB-TICK slot are simultaneous at that
// resolution, so which wins is arbitrary and only has to be fixed: the last row
// that matches, which is the order the server's chain of ifs already resolved
// in. (Two senses of "slot" meet in that sentence and they are unrelated -- one
// is a moment inside a tick, the other is a place in the hand.)
//
// Selecting an EMPTY slot is legal and is a real switch: the hand comes up
// holding nothing, the deploy clock runs, and the fire path finds no weapon.
// Refusing it here instead would make "what is in Primary" a question this
// function cannot see the answer to -- it has no player -- and the two sides
// would have to agree about it separately.
[[nodiscard]] constexpr std::optional<entities::Inventory_Slot>
try_slot_selected_by(uint64_t pressed_buttons)
{
  std::optional<entities::Inventory_Slot> selected;
  for (const weapon_select_binding_t& binding : WEAPON_SELECT_BINDINGS)
    if (pressed_buttons & binding.button)
      selected = binding.slot;

  return selected;
}

} // namespace shared
