#ifdef ENTITIES_WANT_INCLUDES
#include "entities/player_entity.hpp"
#include "entities/weapon_entity.hpp"
#endif

#ifndef SHARED_ENTITY_LIST_HPP
#define SHARED_ENTITY_LIST_HPP

// =============================================================================
// ENTITY REGISTRATION GUIDE
// =============================================================================
// To add a new entity type:
// 1. Add the #include for your entity in the block above (inside #ifdef).
// 2. Add an entry to the macro below:
//    X(ENUM_NAME, Namespace::Class_Name, "string_classname", "path/ignored")
// 3. Make sure to declare schemas in your entity class using DECLARE_SCHEMA.
// =============================================================================

// X(EnumName, ClassName, StringName, HeaderPath)
#define SHARED_ENTITIES_LIST(X)                                                \
  X(PLAYER, network::Player_Entity, "player_start",                            \
    "entities/player_entity.hpp")                                              \
  X(WEAPON, network::Weapon_Entity, "weapon_basic",                            \
    "entities/weapon_entity.hpp")

#endif // SHARED_ENTITY_LIST_HPP
