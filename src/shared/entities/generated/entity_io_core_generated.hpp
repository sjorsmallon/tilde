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
  Kill = 5,   // Mortal
  Set_Health = 6,   // Mortal
  Damage = 7,   // Mortal
};

constexpr uint32_t ENTITY_ACTION_COUNT = 8;

enum class entity_signal : uint16_t
{
  Color_Changed = 0,   // Colorable
  Touched = 1,   // Touchable
  Left = 2,   // Touchable
  Died = 3,   // Mortal
  Health_Changed = 4,   // Mortal
};

constexpr uint32_t ENTITY_SIGNAL_COUNT = 5;

enum class entity_trait : uint16_t
{
  Usable = 0,
  Switchable = 1,
  Colorable = 2,
  Touchable = 3,
  Mortal = 4,
};

constexpr uint32_t ENTITY_TRAIT_COUNT = 5;

const char* to_string(entity_action value);
const char* to_string(entity_signal value);
const char* to_string(entity_trait value);
template <> std::optional<entity_action> try_from_string<entity_action>(std::string_view text);
template <> std::optional<entity_signal> try_from_string<entity_signal>(std::string_view text);
template <> std::optional<entity_trait> try_from_string<entity_trait>(std::string_view text);

} // namespace entities
