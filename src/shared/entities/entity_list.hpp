#ifdef ENTITIES_WANT_INCLUDES
#include "entities/player_entity.hpp"
#include "entities/static_entities.hpp"
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
    "entities/weapon_entity.hpp")                                              \
  X(AABB, network::AABB_Entity, "aabb_entity", "entities/static_entities.hpp") \
  X(WEDGE, network::Wedge_Entity, "wedge_entity",                              \
    "entities/static_entities.hpp")                                            \
  X(STATIC_MESH, network::Static_Mesh_Entity, "static_mesh_entity",            \
    "entities/static_entities.hpp")

// we override the x macro from st get the enum name.
#define ENUM_NAME(enum_name, class_name, str_name, header) enum_name,
enum class entity_type
{
  UNKNOWN = 0,
  SHARED_ENTITIES_LIST(ENUM_NAME) COUNT
};
#undef ENUM_NAME

#endif // SHARED_ENTITY_LIST_HPP
