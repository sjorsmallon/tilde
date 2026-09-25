#pragma once

#include "entities/generated/entities_generated.hpp"

#include <cstdint>

// What a shot DOES when it arrives (contact_effect_plan.md). One answer for a
// hitscan and every projectile: the button's contact_t on the weapon row, read
// at apply by update_contacts. The contact carries no per-shot numbers; what
// the target IS is asked of the entity system at apply, never stored.
namespace shared
{

enum class contact_effect_t : uint8_t
{
  None,
  Damage,
  Swap,
  Magnet,
  Reel,
  Throw,
  Teleport,
  Freeze,
  Explode,
  Land,
  Leave_Zone
};

// Who is moved by a Reel or a Throw.
enum class contact_subject_t : uint8_t
{
  Target,
  Shooter
};

// What a delivery may land on. Bodies is the tick's posed players plus the
// static damageables for a hitscan, and everything collect_projectile_targets
// returns for a projectile. Own_Remnants is the shooter's remnants and nothing
// else, never rewound.
enum class contact_targets_t : uint8_t
{
  Bodies,
  Own_Remnants
};

struct contact_damage_t
{
  float amount;
  float headshot_multiplier;
};

// The speed each of the two players gains along the line between them, negative apart.
struct contact_magnet_t
{
  float speed;
};

// The subject reels toward the other for `seconds`; speed and arrive radius are the sv_hook_* cvars.
struct contact_reel_t
{
  contact_subject_t subject;
  float             seconds;
};

struct contact_throw_t
{
  contact_subject_t subject;
};

// Stasis holds the target with their velocity kept; Statue zeroes it and lets gravity act.
struct contact_freeze_t
{
  entities::Movement_Override kind;
  float                       seconds;
};

struct contact_explode_t
{
  float radius;
  float knockback;
};

// Union-shaped over `effect`: each arm reads exactly one sub-struct and the
// others are zero (static_asserted over WEAPON_DEFINITIONS in weapons.hpp).
struct contact_t
{
  contact_effect_t  effect;
  contact_targets_t targets;
  contact_damage_t  damage;
  contact_magnet_t  magnet;
  contact_reel_t    reel;
  contact_throw_t   throw_;
  contact_freeze_t  freeze;
  contact_explode_t explode;
};

constexpr const char* to_string(contact_effect_t effect)
{
  switch (effect)
  {
  case contact_effect_t::None:       return "None";
  case contact_effect_t::Damage:     return "Damage";
  case contact_effect_t::Swap:       return "Swap";
  case contact_effect_t::Magnet:     return "Magnet";
  case contact_effect_t::Reel:       return "Reel";
  case contact_effect_t::Throw:      return "Throw";
  case contact_effect_t::Teleport:   return "Teleport";
  case contact_effect_t::Freeze:     return "Freeze";
  case contact_effect_t::Explode:    return "Explode";
  case contact_effect_t::Land:       return "Land";
  case contact_effect_t::Leave_Zone: return "Leave_Zone";
  }
  return "?";
}

constexpr const char* to_string(contact_subject_t subject)
{
  switch (subject)
  {
  case contact_subject_t::Target:  return "Target";
  case contact_subject_t::Shooter: return "Shooter";
  }
  return "?";
}

constexpr const char* to_string(contact_targets_t targets)
{
  switch (targets)
  {
  case contact_targets_t::Bodies:       return "Bodies";
  case contact_targets_t::Own_Remnants: return "Own_Remnants";
  }
  return "?";
}

} // namespace shared
