// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
//
// The I/O family's CORE: the three derived enums and the context every
// handler takes. What a SINGLE trait declares is in traits/, what a single
// entity accepts is in entities/, and the tables spanning both are in
// entity_io_generated.hpp beside this one.
#pragma once

#include "entities_core_generated.hpp"
#include <cstdint>
#include <optional>
#include <string_view>

// The context every handler takes, hand-written in src/server/ because it
// holds a server_context_t&. Only ever named through a reference here, so
// the declarations, the shim type and the dispatch entry points all live
// in shared headers while the definitions stay on the server side --
// the cvar family's split, for the cvar family's reason.
namespace server { struct input_context_t; }

namespace entities
{

using server::input_context_t;

// --- the derived enums -----------------------------------------------
//
// DERIVED from the traits rather than listed: a verb belongs to exactly
// one trait, so a list would spell every name twice. Order is declaration
// order and carries no meaning -- neither enum reaches the wire, and a map
// stores the NAME.

enum class entity_action : uint16_t
{
  Enable = 0,   // Switchable
  Disable = 1,   // Switchable
  Toggle_Enabled = 2,   // Switchable
  Play = 3,   // Playable
  Stop_Playing = 4,   // Playable
  Set_Color = 5,   // Colorable
  Set_Character = 6,   // Typeable
  Add = 7,   // Counting
  Reset = 8,   // Counting
  Kill = 9,   // Mortal
  Set_Health = 10,   // Mortal
  Damage = 11,   // Mortal
  Teleport = 12,   // Mobile
  Set_Velocity = 13,   // Mobile
  Add_Velocity = 14,   // Mobile
  Begin_Dolly = 15,   // Mobile
  End_Dolly = 16,   // Mobile
  Begin_Trail = 17,   // Mobile
  End_Trail = 18,   // Mobile
  Grant_Weapon = 19,   // Armable
  Take_Weapon = 20,   // Armable
  Set_Respawn_Point = 21,   // Respawnable
  Complete_Level = 22,   // Objective
  Start = 23,   // Timer
  Stop = 24,   // Timer
  Restart = 25,   // Timer
  Pause = 26,   // Timer
  Resume = 27,   // Timer
  Start_Match = 28,   // Match_Control
  End_Round = 29,   // Match_Control
  Restart_Round = 30,   // Match_Control
  End_Match = 31,   // Match_Control
  Reverse = 32,   // Path_Following
  Go_To = 33,   // Path_Following
  Fire = 34,   // Firing
};

constexpr uint32_t ENTITY_ACTION_COUNT = 35;

enum class entity_signal : uint16_t
{
  Color_Changed = 0,   // Colorable
  Limit_Reached = 1,   // Counting
  Fell_Below_Limit = 2,   // Counting
  Touched = 3,   // Touchable
  Left = 4,   // Touchable
  Died = 5,   // Mortal
  Health_Changed = 6,   // Mortal
  Elapsed = 7,   // Timer
  Match_Started = 8,   // Match_Control
  Round_Started = 9,   // Match_Control
  Round_Ended = 10,   // Match_Control
  Match_Ended = 11,   // Match_Control
  Node_Reached = 12,   // Path_Following
};

constexpr uint32_t ENTITY_SIGNAL_COUNT = 13;

enum class entity_trait : uint16_t
{
  Switchable = 0,
  Playable = 1,
  Colorable = 2,
  Typeable = 3,
  Counting = 4,
  Touchable = 5,
  Mortal = 6,
  Mobile = 7,
  Armable = 8,
  Respawnable = 9,
  Objective = 10,
  Timer = 11,
  Match_Control = 12,
  Path_Following = 13,
  Firing = 14,
};

constexpr uint32_t ENTITY_TRAIT_COUNT = 15;

const char* to_string(entity_action value);
const char* to_string(entity_signal value);
const char* to_string(entity_trait value);
template <> std::optional<entity_action> try_from_string<entity_action>(std::string_view text);
template <> std::optional<entity_signal> try_from_string<entity_signal>(std::string_view text);
template <> std::optional<entity_trait> try_from_string<entity_trait>(std::string_view text);

} // namespace entities

template <> struct enum_traits<entities::entity_action>
{
  static constexpr uint32_t count = entities::ENTITY_ACTION_COUNT;
};

template <> struct enum_traits<entities::entity_signal>
{
  static constexpr uint32_t count = entities::ENTITY_SIGNAL_COUNT;
};

template <> struct enum_traits<entities::entity_trait>
{
  static constexpr uint32_t count = entities::ENTITY_TRAIT_COUNT;
};

