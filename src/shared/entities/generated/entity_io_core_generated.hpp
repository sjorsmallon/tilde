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
  Use = 0,   // Usable
  Enable = 1,   // Switchable
  Disable = 2,   // Switchable
  Toggle_Enabled = 3,   // Switchable
  Set_Color = 4,   // Colorable
  Add = 5,   // Counting
  Reset = 6,   // Counting
  Kill = 7,   // Mortal
  Set_Health = 8,   // Mortal
  Damage = 9,   // Mortal
  Teleport = 10,   // Mobile
  Set_Velocity = 11,   // Mobile
  Add_Velocity = 12,   // Mobile
  Grant_Weapon = 13,   // Armable
  Set_Respawn_Point = 14,   // Respawnable
  Complete_Level = 15,   // Objective
};

constexpr uint32_t ENTITY_ACTION_COUNT = 16;

enum class entity_signal : uint16_t
{
  Color_Changed = 0,   // Colorable
  Limit_Reached = 1,   // Counting
  Touched = 2,   // Touchable
  Left = 3,   // Touchable
  Died = 4,   // Mortal
  Health_Changed = 5,   // Mortal
};

constexpr uint32_t ENTITY_SIGNAL_COUNT = 6;

enum class entity_trait : uint16_t
{
  Usable = 0,
  Switchable = 1,
  Colorable = 2,
  Counting = 3,
  Touchable = 4,
  Mortal = 5,
  Mobile = 6,
  Armable = 7,
  Respawnable = 8,
  Objective = 9,
};

constexpr uint32_t ENTITY_TRAIT_COUNT = 10;

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

