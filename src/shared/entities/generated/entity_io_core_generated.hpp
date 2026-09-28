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
  Add = 6,   // Counting
  Reset = 7,   // Counting
  Kill = 8,   // Mortal
  Set_Health = 9,   // Mortal
  Damage = 10,   // Mortal
  Teleport = 11,   // Mobile
  Set_Velocity = 12,   // Mobile
  Add_Velocity = 13,   // Mobile
  Grant_Weapon = 14,   // Armable
  Take_Weapon = 15,   // Armable
  Set_Respawn_Point = 16,   // Respawnable
  Complete_Level = 17,   // Objective
  Start = 18,   // Timer
  Stop = 19,   // Timer
  Restart = 20,   // Timer
  Pause = 21,   // Timer
  Resume = 22,   // Timer
  Start_Match = 23,   // Match_Control
  End_Round = 24,   // Match_Control
  Restart_Round = 25,   // Match_Control
  End_Match = 26,   // Match_Control
  Reverse = 27,   // Path_Following
  Go_To = 28,   // Path_Following
  Fire = 29,   // Firing
};

constexpr uint32_t ENTITY_ACTION_COUNT = 30;

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
  Counting = 3,
  Touchable = 4,
  Mortal = 5,
  Mobile = 6,
  Armable = 7,
  Respawnable = 8,
  Objective = 9,
  Timer = 10,
  Match_Control = 11,
  Path_Following = 12,
  Firing = 13,
};

constexpr uint32_t ENTITY_TRAIT_COUNT = 14;

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

