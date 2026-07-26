#ifndef SHARED_ENTITY_TYPE_HPP
#define SHARED_ENTITY_TYPE_HPP

// =============================================================================
// ENTITY REGISTRATION GUIDE
// =============================================================================
// To add a new entity type:
// 1. Add an entry to the macro below:
//    X(ENUM_NAME, Namespace::Class_Name, "string_classname", "path/ignored")
// 2. Add an include for your entity header in entity_list.hpp (under the
//    ENTITIES_WANT_INCLUDES block).
// 3. Declare schemas in your entity class using DECLARE_SCHEMA.
// 4. Inherit from network::Entity_Of<::entity_type::ENUM_NAME>, not Entity.
// =============================================================================

// This file is factored out of entity_list.hpp so entity.hpp can pull in just
// the enum (needed by Entity_Of's template parameter) without also dragging in
// the full entity-header pull-in block. The pull-in block depends on Entity_Of,
// so including it from entity.hpp would create an order-of-declaration cycle.

// X(EnumName, ClassName, StringName, HeaderPath)
#define SHARED_ENTITIES_LIST(X)                                                     \
  X(PLAYER_SPAWN, network::Player_Spawn_Entity, "player_start",                     \
    "entities/player_entity.hpp")                                                   \
  X(PLAYER, network::Player_Entity, "player_entity",                                \
    "entities/player_entity.hpp")                                                   \
  X(WEAPON, network::Weapon_Entity, "weapon_basic",                                 \
    "entities/weapon_entity.hpp")                                                   \
  X(ROCKET, network::Rocket_Entity, "rocket_entity", "entities/rocket_entity.hpp")   \
  X(PARTICLE_EMITTER, network::Particle_Emitter_Entity, "particle_emitter",         \
    "entities/particle_emitter_entity.hpp")                                        \
  X(TRIGGER_VOLUME, network::Trigger_Volume_Entity, "trigger_volume",              \
    "entities/trigger_volume_entity.hpp")                                          \
  X(LIGHT, network::Light_Entity, "light_entity",                                 \
    "entities/light_entity.hpp")                                                  \
  X(PHYSICS_BODY, network::Physics_Body_Entity, "physics_body",                   \
    "entities/physics_body_entity.hpp")


#define ENUM_NAME(enum_name, class_name, str_name, header) enum_name,
enum class entity_type
{
  UNKNOWN = 0,
  SHARED_ENTITIES_LIST(ENUM_NAME) COUNT
};
#undef ENUM_NAME

#endif // SHARED_ENTITY_TYPE_HPP
