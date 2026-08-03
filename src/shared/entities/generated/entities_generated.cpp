// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
#include "entities_generated.hpp"
#include <cassert>
#include <cstddef>
#include <cstring>
#include <new>

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#elif defined(_MSC_VER)
#pragma warning(disable : 4841)
#endif

namespace entities
{

namespace
{

constexpr field_info_t Box_Volume_FIELDS[] = {
  {.name = "position",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Box_Volume, position),
   .size_in_bytes = (uint32_t)sizeof(Box_Volume::position),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "half_extents",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Box_Volume, half_extents),
   .size_in_bytes = (uint32_t)sizeof(Box_Volume::half_extents),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
};

constexpr field_info_t Material_FIELDS[] = {
  {.name = "shader_type",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Material, shader_type),
   .size_in_bytes = (uint32_t)sizeof(Material::shader_type),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = 5},
  {.name = "color",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Material, color),
   .size_in_bytes = (uint32_t)sizeof(Material::color),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "roughness",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Material, roughness),
   .size_in_bytes = (uint32_t)sizeof(Material::roughness),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
};

constexpr field_info_t Render_FIELDS[] = {
  {.name = "mesh",
   .type = FIELD_TYPE_ASSET,
   .offset = (uint32_t)offsetof(Render, mesh),
   .size_in_bytes = (uint32_t)sizeof(Render::mesh),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = 0,
   .enum_id = NOT_AN_ENUM},
  {.name = "visible",
   .type = FIELD_TYPE_BOOL,
   .offset = (uint32_t)offsetof(Render, visible),
   .size_in_bytes = (uint32_t)sizeof(Render::visible),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "is_wireframe",
   .type = FIELD_TYPE_BOOL,
   .offset = (uint32_t)offsetof(Render, is_wireframe),
   .size_in_bytes = (uint32_t)sizeof(Render::is_wireframe),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "offset",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Render, offset),
   .size_in_bytes = (uint32_t)sizeof(Render::offset),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "scale",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Render, scale),
   .size_in_bytes = (uint32_t)sizeof(Render::scale),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "rotation",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Render, rotation),
   .size_in_bytes = (uint32_t)sizeof(Render::rotation),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "material",
   .type = FIELD_TYPE_COMPONENT,
   .offset = (uint32_t)offsetof(Render, material),
   .size_in_bytes = (uint32_t)sizeof(Render::material),
   .flags = 0u,
   .component_id = 1,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
};

constexpr field_info_t Hitbox_FIELDS[] = {
  {.name = "shape",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Hitbox, shape),
   .size_in_bytes = (uint32_t)sizeof(Hitbox::shape),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = 6},
  {.name = "size",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Hitbox, size),
   .size_in_bytes = (uint32_t)sizeof(Hitbox::size),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "offset",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Hitbox, offset),
   .size_in_bytes = (uint32_t)sizeof(Hitbox::offset),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
};

constexpr field_info_t Player_Spawn_Entity_FIELDS[] = {
  {.name = "entity_id",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Player_Spawn_Entity, entity_id),
   .size_in_bytes = (uint32_t)sizeof(Player_Spawn_Entity::entity_id),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "position",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Player_Spawn_Entity, position),
   .size_in_bytes = (uint32_t)sizeof(Player_Spawn_Entity::position),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "orientation",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Player_Spawn_Entity, orientation),
   .size_in_bytes = (uint32_t)sizeof(Player_Spawn_Entity::orientation),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "spawn_type",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Player_Spawn_Entity, spawn_type),
   .size_in_bytes = (uint32_t)sizeof(Player_Spawn_Entity::spawn_type),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = 1},
  {.name = "team_allegiance",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Player_Spawn_Entity, team_allegiance),
   .size_in_bytes = (uint32_t)sizeof(Player_Spawn_Entity::team_allegiance),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = 2},
};

constexpr field_info_t Player_Entity_FIELDS[] = {
  {.name = "entity_id",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Player_Entity, entity_id),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::entity_id),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "position",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Player_Entity, position),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::position),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "orientation",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Player_Entity, orientation),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::orientation),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "view_angle_yaw",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Player_Entity, view_angle_yaw),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::view_angle_yaw),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "view_angle_pitch",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Player_Entity, view_angle_pitch),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::view_angle_pitch),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "health",
   .type = FIELD_TYPE_I32,
   .offset = (uint32_t)offsetof(Player_Entity, health),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::health),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "ammo",
   .type = FIELD_TYPE_I32,
   .offset = (uint32_t)offsetof(Player_Entity, ammo),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::ammo),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "active_weapon_id",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Player_Entity, active_weapon_id),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::active_weapon_id),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = 3},
  {.name = "client_slot_index",
   .type = FIELD_TYPE_I32,
   .offset = (uint32_t)offsetof(Player_Entity, client_slot_index),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::client_slot_index),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "velocity",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Player_Entity, velocity),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::velocity),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "render",
   .type = FIELD_TYPE_COMPONENT,
   .offset = (uint32_t)offsetof(Player_Entity, render),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::render),
   .flags = 0u,
   .component_id = 2,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "hitbox",
   .type = FIELD_TYPE_COMPONENT,
   .offset = (uint32_t)offsetof(Player_Entity, hitbox),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::hitbox),
   .flags = 0u,
   .component_id = 3,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "team_allegiance",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Player_Entity, team_allegiance),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::team_allegiance),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = 2},
};

constexpr field_info_t Weapon_Entity_FIELDS[] = {
  {.name = "entity_id",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Weapon_Entity, entity_id),
   .size_in_bytes = (uint32_t)sizeof(Weapon_Entity::entity_id),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "position",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Weapon_Entity, position),
   .size_in_bytes = (uint32_t)sizeof(Weapon_Entity::position),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "orientation",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Weapon_Entity, orientation),
   .size_in_bytes = (uint32_t)sizeof(Weapon_Entity::orientation),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "ammo",
   .type = FIELD_TYPE_I32,
   .offset = (uint32_t)offsetof(Weapon_Entity, ammo),
   .size_in_bytes = (uint32_t)sizeof(Weapon_Entity::ammo),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "weapon_id",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Weapon_Entity, weapon_id),
   .size_in_bytes = (uint32_t)sizeof(Weapon_Entity::weapon_id),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = 3},
  {.name = "render",
   .type = FIELD_TYPE_COMPONENT,
   .offset = (uint32_t)offsetof(Weapon_Entity, render),
   .size_in_bytes = (uint32_t)sizeof(Weapon_Entity::render),
   .flags = 0u,
   .component_id = 2,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
};

constexpr field_info_t Rocket_Entity_FIELDS[] = {
  {.name = "entity_id",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Rocket_Entity, entity_id),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Entity::entity_id),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "position",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Rocket_Entity, position),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Entity::position),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "orientation",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Rocket_Entity, orientation),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Entity::orientation),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "velocity",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Rocket_Entity, velocity),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Entity::velocity),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "lifetime",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Rocket_Entity, lifetime),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Entity::lifetime),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "damage_amount",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Rocket_Entity, damage_amount),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Entity::damage_amount),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "damage_radius",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Rocket_Entity, damage_radius),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Entity::damage_radius),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "knockback_force",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Rocket_Entity, knockback_force),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Entity::knockback_force),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "owner_id",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Rocket_Entity, owner_id),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Entity::owner_id),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "render",
   .type = FIELD_TYPE_COMPONENT,
   .offset = (uint32_t)offsetof(Rocket_Entity, render),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Entity::render),
   .flags = 0u,
   .component_id = 2,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "hitbox",
   .type = FIELD_TYPE_COMPONENT,
   .offset = (uint32_t)offsetof(Rocket_Entity, hitbox),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Entity::hitbox),
   .flags = 0u,
   .component_id = 3,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
};

constexpr field_info_t Particle_Emitter_Entity_FIELDS[] = {
  {.name = "entity_id",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, entity_id),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::entity_id),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "position",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, position),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::position),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "orientation",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, orientation),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::orientation),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "sprite",
   .type = FIELD_TYPE_ASSET,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, sprite),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::sprite),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = 1,
   .enum_id = NOT_AN_ENUM},
  {.name = "emit_rate",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, emit_rate),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::emit_rate),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "max_particles",
   .type = FIELD_TYPE_I32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, max_particles),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::max_particles),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "lifetime_min",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, lifetime_min),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::lifetime_min),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "lifetime_max",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, lifetime_max),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::lifetime_max),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "velocity_min",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, velocity_min),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::velocity_min),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "velocity_max",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, velocity_max),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::velocity_max),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "spread",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, spread),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::spread),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "gravity",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, gravity),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::gravity),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "drag",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, drag),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::drag),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "size_start",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, size_start),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::size_start),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "size_end",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, size_end),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::size_end),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "rotation_speed_min",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, rotation_speed_min),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::rotation_speed_min),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "rotation_speed_max",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, rotation_speed_max),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::rotation_speed_max),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "color_start",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, color_start),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::color_start),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "color_end",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, color_end),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::color_end),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "alpha_start",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, alpha_start),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::alpha_start),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "alpha_end",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, alpha_end),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::alpha_end),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "emitter_lifetime",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, emitter_lifetime),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::emitter_lifetime),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "parent_entity_id",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, parent_entity_id),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::parent_entity_id),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
};

constexpr field_info_t Trigger_Volume_Entity_FIELDS[] = {
  {.name = "entity_id",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Trigger_Volume_Entity, entity_id),
   .size_in_bytes = (uint32_t)sizeof(Trigger_Volume_Entity::entity_id),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "position",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Trigger_Volume_Entity, position),
   .size_in_bytes = (uint32_t)sizeof(Trigger_Volume_Entity::position),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "orientation",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Trigger_Volume_Entity, orientation),
   .size_in_bytes = (uint32_t)sizeof(Trigger_Volume_Entity::orientation),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "volume",
   .type = FIELD_TYPE_COMPONENT,
   .offset = (uint32_t)offsetof(Trigger_Volume_Entity, volume),
   .size_in_bytes = (uint32_t)sizeof(Trigger_Volume_Entity::volume),
   .flags = 0u,
   .component_id = 0,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "action",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Trigger_Volume_Entity, action),
   .size_in_bytes = (uint32_t)sizeof(Trigger_Volume_Entity::action),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = 7},
  {.name = "fire_mode",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Trigger_Volume_Entity, fire_mode),
   .size_in_bytes = (uint32_t)sizeof(Trigger_Volume_Entity::fire_mode),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = 8},
  {.name = "param_target_name",
   .type = FIELD_TYPE_STRING,
   .offset = (uint32_t)offsetof(Trigger_Volume_Entity, param_target_name),
   .size_in_bytes = (uint32_t)sizeof(Trigger_Volume_Entity::param_target_name),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = 64,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "param_string",
   .type = FIELD_TYPE_STRING,
   .offset = (uint32_t)offsetof(Trigger_Volume_Entity, param_string),
   .size_in_bytes = (uint32_t)sizeof(Trigger_Volume_Entity::param_string),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = 128,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "param_float",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Trigger_Volume_Entity, param_float),
   .size_in_bytes = (uint32_t)sizeof(Trigger_Volume_Entity::param_float),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
};

constexpr field_info_t Light_Entity_FIELDS[] = {
  {.name = "entity_id",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Light_Entity, entity_id),
   .size_in_bytes = (uint32_t)sizeof(Light_Entity::entity_id),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "position",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Light_Entity, position),
   .size_in_bytes = (uint32_t)sizeof(Light_Entity::position),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "orientation",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Light_Entity, orientation),
   .size_in_bytes = (uint32_t)sizeof(Light_Entity::orientation),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "direction",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Light_Entity, direction),
   .size_in_bytes = (uint32_t)sizeof(Light_Entity::direction),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "color",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Light_Entity, color),
   .size_in_bytes = (uint32_t)sizeof(Light_Entity::color),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "intensity",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Light_Entity, intensity),
   .size_in_bytes = (uint32_t)sizeof(Light_Entity::intensity),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "range",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Light_Entity, range),
   .size_in_bytes = (uint32_t)sizeof(Light_Entity::range),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "spot_inner_degrees",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Light_Entity, spot_inner_degrees),
   .size_in_bytes = (uint32_t)sizeof(Light_Entity::spot_inner_degrees),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "spot_outer_degrees",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Light_Entity, spot_outer_degrees),
   .size_in_bytes = (uint32_t)sizeof(Light_Entity::spot_outer_degrees),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "kind",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Light_Entity, kind),
   .size_in_bytes = (uint32_t)sizeof(Light_Entity::kind),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = 0},
};

constexpr field_info_t Physics_Body_Entity_FIELDS[] = {
  {.name = "entity_id",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Physics_Body_Entity, entity_id),
   .size_in_bytes = (uint32_t)sizeof(Physics_Body_Entity::entity_id),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "position",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Physics_Body_Entity, position),
   .size_in_bytes = (uint32_t)sizeof(Physics_Body_Entity::position),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "orientation",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Physics_Body_Entity, orientation),
   .size_in_bytes = (uint32_t)sizeof(Physics_Body_Entity::orientation),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "shape",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Physics_Body_Entity, shape),
   .size_in_bytes = (uint32_t)sizeof(Physics_Body_Entity::shape),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = 6},
  {.name = "size",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Physics_Body_Entity, size),
   .size_in_bytes = (uint32_t)sizeof(Physics_Body_Entity::size),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "velocity",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Physics_Body_Entity, velocity),
   .size_in_bytes = (uint32_t)sizeof(Physics_Body_Entity::velocity),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "mass",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Physics_Body_Entity, mass),
   .size_in_bytes = (uint32_t)sizeof(Physics_Body_Entity::mass),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "render",
   .type = FIELD_TYPE_COMPONENT,
   .offset = (uint32_t)offsetof(Physics_Body_Entity, render),
   .size_in_bytes = (uint32_t)sizeof(Physics_Body_Entity::render),
   .flags = 0u,
   .component_id = 2,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
  {.name = "hitbox",
   .type = FIELD_TYPE_COMPONENT,
   .offset = (uint32_t)offsetof(Physics_Body_Entity, hitbox),
   .size_in_bytes = (uint32_t)sizeof(Physics_Body_Entity::hitbox),
   .flags = 0u,
   .component_id = 3,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_id = NOT_AN_ENUM},
};

constexpr component_type_info_t COMPONENT_INFOS[] = {
  {"Box_Volume", {Box_Volume_FIELDS, 2}, (uint32_t)sizeof(Box_Volume)},
  {"Material", {Material_FIELDS, 3}, (uint32_t)sizeof(Material)},
  {"Render", {Render_FIELDS, 7}, (uint32_t)sizeof(Render)},
  {"Hitbox", {Hitbox_FIELDS, 3}, (uint32_t)sizeof(Hitbox)},
};

Entity* construct_Player_Spawn_Entity(void* memory) { return new (memory) Player_Spawn_Entity(); }
Entity* construct_Player_Entity(void* memory) { return new (memory) Player_Entity(); }
Entity* construct_Weapon_Entity(void* memory) { return new (memory) Weapon_Entity(); }
Entity* construct_Rocket_Entity(void* memory) { return new (memory) Rocket_Entity(); }
Entity* construct_Particle_Emitter_Entity(void* memory) { return new (memory) Particle_Emitter_Entity(); }
Entity* construct_Trigger_Volume_Entity(void* memory) { return new (memory) Trigger_Volume_Entity(); }
Entity* construct_Light_Entity(void* memory) { return new (memory) Light_Entity(); }
Entity* construct_Physics_Body_Entity(void* memory) { return new (memory) Physics_Body_Entity(); }

Entity* as_base_Player_Spawn_Entity(void* memory) { return static_cast<Entity*>((Player_Spawn_Entity*)memory); }
Entity* as_base_Player_Entity(void* memory) { return static_cast<Entity*>((Player_Entity*)memory); }
Entity* as_base_Weapon_Entity(void* memory) { return static_cast<Entity*>((Weapon_Entity*)memory); }
Entity* as_base_Rocket_Entity(void* memory) { return static_cast<Entity*>((Rocket_Entity*)memory); }
Entity* as_base_Particle_Emitter_Entity(void* memory) { return static_cast<Entity*>((Particle_Emitter_Entity*)memory); }
Entity* as_base_Trigger_Volume_Entity(void* memory) { return static_cast<Entity*>((Trigger_Volume_Entity*)memory); }
Entity* as_base_Light_Entity(void* memory) { return static_cast<Entity*>((Light_Entity*)memory); }
Entity* as_base_Physics_Body_Entity(void* memory) { return static_cast<Entity*>((Physics_Body_Entity*)memory); }

constexpr entity_type_info_t ENTITY_INFOS[] = {
  {"", "", {}, 0, 0, 0, false, nullptr, nullptr}, // Invalid
  {"player_spawn_entity", "Player Spawn", {Player_Spawn_Entity_FIELDS, 5}, (uint32_t)sizeof(Player_Spawn_Entity), (uint32_t)alignof(Player_Spawn_Entity), 0u, false, construct_Player_Spawn_Entity, as_base_Player_Spawn_Entity},
  {"player_entity", "Player", {Player_Entity_FIELDS, 13}, (uint32_t)sizeof(Player_Entity), (uint32_t)alignof(Player_Entity), 12u, true, construct_Player_Entity, as_base_Player_Entity},
  {"weapon_entity", "Weapon", {Weapon_Entity_FIELDS, 6}, (uint32_t)sizeof(Weapon_Entity), (uint32_t)alignof(Weapon_Entity), 4u, false, construct_Weapon_Entity, as_base_Weapon_Entity},
  {"rocket_entity", "Rocket", {Rocket_Entity_FIELDS, 11}, (uint32_t)sizeof(Rocket_Entity), (uint32_t)alignof(Rocket_Entity), 12u, true, construct_Rocket_Entity, as_base_Rocket_Entity},
  {"particle_emitter_entity", "Particle Emitter", {Particle_Emitter_Entity_FIELDS, 23}, (uint32_t)sizeof(Particle_Emitter_Entity), (uint32_t)alignof(Particle_Emitter_Entity), 0u, false, construct_Particle_Emitter_Entity, as_base_Particle_Emitter_Entity},
  {"trigger_volume_entity", "Trigger Volume", {Trigger_Volume_Entity_FIELDS, 9}, (uint32_t)sizeof(Trigger_Volume_Entity), (uint32_t)alignof(Trigger_Volume_Entity), 1u, false, construct_Trigger_Volume_Entity, as_base_Trigger_Volume_Entity},
  {"light_entity", "Light", {Light_Entity_FIELDS, 10}, (uint32_t)sizeof(Light_Entity), (uint32_t)alignof(Light_Entity), 0u, false, construct_Light_Entity, as_base_Light_Entity},
  {"physics_body_entity", "Physics Body", {Physics_Body_Entity_FIELDS, 9}, (uint32_t)sizeof(Physics_Body_Entity), (uint32_t)alignof(Physics_Body_Entity), 12u, false, construct_Physics_Body_Entity, as_base_Physics_Body_Entity},
};

constexpr int32_t COMPONENT_OFFSETS[][4] = {
  {-1, -1, -1, -1}, // Invalid
  {-1, -1, -1, -1}, // Player_Spawn_Entity
  {-1, -1, (int32_t)offsetof(Player_Entity, render), (int32_t)offsetof(Player_Entity, hitbox)}, // Player_Entity
  {-1, -1, (int32_t)offsetof(Weapon_Entity, render), -1}, // Weapon_Entity
  {-1, -1, (int32_t)offsetof(Rocket_Entity, render), (int32_t)offsetof(Rocket_Entity, hitbox)}, // Rocket_Entity
  {-1, -1, -1, -1}, // Particle_Emitter_Entity
  {(int32_t)offsetof(Trigger_Volume_Entity, volume), -1, -1, -1}, // Trigger_Volume_Entity
  {-1, -1, -1, -1}, // Light_Entity
  {-1, -1, (int32_t)offsetof(Physics_Body_Entity, render), (int32_t)offsetof(Physics_Body_Entity, hitbox)}, // Physics_Body_Entity
};

constexpr uint32_t PLACEABLE_ENTITY_TYPE_COUNT = 6;
constexpr entity_type PLACEABLE_ENTITY_TYPES[] = {
  entity_type::Player_Spawn_Entity,
  entity_type::Weapon_Entity,
  entity_type::Particle_Emitter_Entity,
  entity_type::Trigger_Volume_Entity,
  entity_type::Light_Entity,
  entity_type::Physics_Body_Entity,
};

constexpr asset_info_t mesh_asset_MANIFEST[] = {
  {"Missing", "resources/obj/error.obj", ASSET_SOURCE_FILE},
  {"Isosphere", "resources/obj/isosphere.obj", ASSET_SOURCE_FILE},
  {"Pyramid", "resources/obj/pyramid.obj", ASSET_SOURCE_FILE},
  {"Box", "box", ASSET_SOURCE_PROCEDURAL},
  {"Arrow", "arrow", ASSET_SOURCE_PROCEDURAL},
  {"Sphere", "sphere", ASSET_SOURCE_PROCEDURAL},
  {"Cylinder", "cylinder", ASSET_SOURCE_PROCEDURAL},
  {"Cone", "cone", ASSET_SOURCE_PROCEDURAL},
  {"Wedge", "wedge", ASSET_SOURCE_PROCEDURAL},
};

constexpr asset_info_t sprite_asset_MANIFEST[] = {
  {"Missing", "", ASSET_SOURCE_MISSING},
  {"Smoke", "resources/sprites/smoke.png", ASSET_SOURCE_FILE},
};

constexpr const char* Light_Type_VALUE_NAMES[] = {
  "Point",
  "Spot",
  "Directional",
};

constexpr const char* Spawn_Type_VALUE_NAMES[] = {
  "Human",
  "Bot",
};

constexpr const char* Team_Allegiance_VALUE_NAMES[] = {
  "Red",
  "Blu",
  "Free_For_All",
};

constexpr const char* Weapon_VALUE_NAMES[] = {
  "Knife",
  "Scout",
  "Rocket_Launcher",
};

constexpr const char* Weapon_Kind_VALUE_NAMES[] = {
  "Melee",
  "Hitscan",
  "Projectile",
  "Sniper",
};

constexpr const char* Shader_Type_VALUE_NAMES[] = {
  "Lit",
  "Unlit",
};

constexpr const char* Shape_Kind_VALUE_NAMES[] = {
  "Sphere",
  "Capsule",
  "Box",
};

constexpr const char* Trigger_Action_VALUE_NAMES[] = {
  "Kill",
  "Set_Health",
  "Print_Message",
  "Warp_To_Spawn",
};

constexpr const char* Fire_Mode_VALUE_NAMES[] = {
  "On_Enter",
  "Every_Tick",
};

constexpr enum_type_info_t ENUM_INFOS[] = {
  {"Light_Type", {Light_Type_VALUE_NAMES, 3}},
  {"Spawn_Type", {Spawn_Type_VALUE_NAMES, 2}},
  {"Team_Allegiance", {Team_Allegiance_VALUE_NAMES, 3}},
  {"Weapon", {Weapon_VALUE_NAMES, 3}},
  {"Weapon_Kind", {Weapon_Kind_VALUE_NAMES, 4}},
  {"Shader_Type", {Shader_Type_VALUE_NAMES, 2}},
  {"Shape_Kind", {Shape_Kind_VALUE_NAMES, 3}},
  {"Trigger_Action", {Trigger_Action_VALUE_NAMES, 4}},
  {"Fire_Mode", {Fire_Mode_VALUE_NAMES, 2}},
};

} // namespace

Span<const asset_info_t> mesh_asset_manifest()
{
  return {mesh_asset_MANIFEST, mesh_asset_COUNT};
}

const char* to_string(mesh_asset value)
{
  assert((uint32_t)value < mesh_asset_COUNT);
  return mesh_asset_MANIFEST[(uint16_t)value].name;
}

bool from_string(const char* text, mesh_asset* out_value)
{
  for (uint32_t index = 0; index < mesh_asset_COUNT; ++index)
  {
    if (strcmp(mesh_asset_MANIFEST[index].name, text) != 0)
      continue;
    *out_value = (mesh_asset)index;
    return true;
  }
  return false;
}

Span<const asset_info_t> sprite_asset_manifest()
{
  return {sprite_asset_MANIFEST, sprite_asset_COUNT};
}

const char* to_string(sprite_asset value)
{
  assert((uint32_t)value < sprite_asset_COUNT);
  return sprite_asset_MANIFEST[(uint16_t)value].name;
}

bool from_string(const char* text, sprite_asset* out_value)
{
  for (uint32_t index = 0; index < sprite_asset_COUNT; ++index)
  {
    if (strcmp(sprite_asset_MANIFEST[index].name, text) != 0)
      continue;
    *out_value = (sprite_asset)index;
    return true;
  }
  return false;
}

Span<const asset_info_t> asset_class_manifest(int32_t asset_class_id)
{
  switch (asset_class_id)
  {
    case 0: return mesh_asset_manifest();
    case 1: return sprite_asset_manifest();
  }
  assert(false && "asset_class_manifest: no asset class has this id");
  return {};
}

const char* to_string(Light_Type value)
{
  switch (value)
  {
    case Light_Type::Point: return "Point";
    case Light_Type::Spot: return "Spot";
    case Light_Type::Directional: return "Directional";
  }
  assert(false && "invalid Light_Type");
  return "";
}

bool from_string(const char* text, Light_Type* out_value)
{
  if (strcmp(text, "Point") == 0) { *out_value = Light_Type::Point; return true; }
  if (strcmp(text, "Spot") == 0) { *out_value = Light_Type::Spot; return true; }
  if (strcmp(text, "Directional") == 0) { *out_value = Light_Type::Directional; return true; }
  return false;
}

const char* to_string(Spawn_Type value)
{
  switch (value)
  {
    case Spawn_Type::Human: return "Human";
    case Spawn_Type::Bot: return "Bot";
  }
  assert(false && "invalid Spawn_Type");
  return "";
}

bool from_string(const char* text, Spawn_Type* out_value)
{
  if (strcmp(text, "Human") == 0) { *out_value = Spawn_Type::Human; return true; }
  if (strcmp(text, "Bot") == 0) { *out_value = Spawn_Type::Bot; return true; }
  return false;
}

const char* to_string(Team_Allegiance value)
{
  switch (value)
  {
    case Team_Allegiance::Red: return "Red";
    case Team_Allegiance::Blu: return "Blu";
    case Team_Allegiance::Free_For_All: return "Free_For_All";
  }
  assert(false && "invalid Team_Allegiance");
  return "";
}

bool from_string(const char* text, Team_Allegiance* out_value)
{
  if (strcmp(text, "Red") == 0) { *out_value = Team_Allegiance::Red; return true; }
  if (strcmp(text, "Blu") == 0) { *out_value = Team_Allegiance::Blu; return true; }
  if (strcmp(text, "Free_For_All") == 0) { *out_value = Team_Allegiance::Free_For_All; return true; }
  return false;
}

const char* to_string(Weapon value)
{
  switch (value)
  {
    case Weapon::Knife: return "Knife";
    case Weapon::Scout: return "Scout";
    case Weapon::Rocket_Launcher: return "Rocket_Launcher";
  }
  assert(false && "invalid Weapon");
  return "";
}

bool from_string(const char* text, Weapon* out_value)
{
  if (strcmp(text, "Knife") == 0) { *out_value = Weapon::Knife; return true; }
  if (strcmp(text, "Scout") == 0) { *out_value = Weapon::Scout; return true; }
  if (strcmp(text, "Rocket_Launcher") == 0) { *out_value = Weapon::Rocket_Launcher; return true; }
  return false;
}

const char* to_string(Weapon_Kind value)
{
  switch (value)
  {
    case Weapon_Kind::Melee: return "Melee";
    case Weapon_Kind::Hitscan: return "Hitscan";
    case Weapon_Kind::Projectile: return "Projectile";
    case Weapon_Kind::Sniper: return "Sniper";
  }
  assert(false && "invalid Weapon_Kind");
  return "";
}

bool from_string(const char* text, Weapon_Kind* out_value)
{
  if (strcmp(text, "Melee") == 0) { *out_value = Weapon_Kind::Melee; return true; }
  if (strcmp(text, "Hitscan") == 0) { *out_value = Weapon_Kind::Hitscan; return true; }
  if (strcmp(text, "Projectile") == 0) { *out_value = Weapon_Kind::Projectile; return true; }
  if (strcmp(text, "Sniper") == 0) { *out_value = Weapon_Kind::Sniper; return true; }
  return false;
}

const char* to_string(Shader_Type value)
{
  switch (value)
  {
    case Shader_Type::Lit: return "Lit";
    case Shader_Type::Unlit: return "Unlit";
  }
  assert(false && "invalid Shader_Type");
  return "";
}

bool from_string(const char* text, Shader_Type* out_value)
{
  if (strcmp(text, "Lit") == 0) { *out_value = Shader_Type::Lit; return true; }
  if (strcmp(text, "Unlit") == 0) { *out_value = Shader_Type::Unlit; return true; }
  return false;
}

const char* to_string(Shape_Kind value)
{
  switch (value)
  {
    case Shape_Kind::Sphere: return "Sphere";
    case Shape_Kind::Capsule: return "Capsule";
    case Shape_Kind::Box: return "Box";
  }
  assert(false && "invalid Shape_Kind");
  return "";
}

bool from_string(const char* text, Shape_Kind* out_value)
{
  if (strcmp(text, "Sphere") == 0) { *out_value = Shape_Kind::Sphere; return true; }
  if (strcmp(text, "Capsule") == 0) { *out_value = Shape_Kind::Capsule; return true; }
  if (strcmp(text, "Box") == 0) { *out_value = Shape_Kind::Box; return true; }
  return false;
}

const char* to_string(Trigger_Action value)
{
  switch (value)
  {
    case Trigger_Action::Kill: return "Kill";
    case Trigger_Action::Set_Health: return "Set_Health";
    case Trigger_Action::Print_Message: return "Print_Message";
    case Trigger_Action::Warp_To_Spawn: return "Warp_To_Spawn";
  }
  assert(false && "invalid Trigger_Action");
  return "";
}

bool from_string(const char* text, Trigger_Action* out_value)
{
  if (strcmp(text, "Kill") == 0) { *out_value = Trigger_Action::Kill; return true; }
  if (strcmp(text, "Set_Health") == 0) { *out_value = Trigger_Action::Set_Health; return true; }
  if (strcmp(text, "Print_Message") == 0) { *out_value = Trigger_Action::Print_Message; return true; }
  if (strcmp(text, "Warp_To_Spawn") == 0) { *out_value = Trigger_Action::Warp_To_Spawn; return true; }
  return false;
}

const char* to_string(Fire_Mode value)
{
  switch (value)
  {
    case Fire_Mode::On_Enter: return "On_Enter";
    case Fire_Mode::Every_Tick: return "Every_Tick";
  }
  assert(false && "invalid Fire_Mode");
  return "";
}

bool from_string(const char* text, Fire_Mode* out_value)
{
  if (strcmp(text, "On_Enter") == 0) { *out_value = Fire_Mode::On_Enter; return true; }
  if (strcmp(text, "Every_Tick") == 0) { *out_value = Fire_Mode::Every_Tick; return true; }
  return false;
}

const enum_type_info_t& enum_info(enum_type type)
{
  assert((uint32_t)type < ENUM_TYPE_COUNT);
  return ENUM_INFOS[(uint16_t)type];
}

const entity_type_info_t& entity_info(entity_type type)
{
  assert(type > entity_type::Invalid && (uint32_t)type < ENTITY_TYPE_COUNT);
  return ENTITY_INFOS[(uint16_t)type];
}

const component_type_info_t& component_info(component_type component)
{
  assert((uint32_t)component < COMPONENT_TYPE_COUNT);
  return COMPONENT_INFOS[(uint16_t)component];
}

entity_type entity_type_from_classname(const char* classname)
{
  for (uint32_t index = 1; index < ENTITY_TYPE_COUNT; ++index)
  {
    if (strcmp(ENTITY_INFOS[index].classname, classname) == 0)
      return (entity_type)index;
  }
  return entity_type::Invalid;
}

bool has_component(entity_type type, component_type component)
{
  return (entity_info(type).component_mask & (1u << (uint16_t)component)) != 0;
}

int32_t component_byte_offset(entity_type type, component_type component)
{
  assert(type > entity_type::Invalid && (uint32_t)type < ENTITY_TYPE_COUNT);
  assert((uint32_t)component < COMPONENT_TYPE_COUNT);
  return COMPONENT_OFFSETS[(uint16_t)type][(uint16_t)component];
}

Entity* create_entity(entity_type type)
{
  switch (type)
  {
    case entity_type::Invalid: break;
    case entity_type::Player_Spawn_Entity: return new Player_Spawn_Entity();
    case entity_type::Player_Entity: return new Player_Entity();
    case entity_type::Weapon_Entity: return new Weapon_Entity();
    case entity_type::Rocket_Entity: return new Rocket_Entity();
    case entity_type::Particle_Emitter_Entity: return new Particle_Emitter_Entity();
    case entity_type::Trigger_Volume_Entity: return new Trigger_Volume_Entity();
    case entity_type::Light_Entity: return new Light_Entity();
    case entity_type::Physics_Body_Entity: return new Physics_Body_Entity();
  }
  assert(false && "create_entity: not a valid entity_type");
  return nullptr;
}

Entity* entity_from_classname(const char* classname)
{
  entity_type type = entity_type_from_classname(classname);
  if (type == entity_type::Invalid)
    return nullptr; // unknown classname: the caller reports it
  return create_entity(type);
}

void destroy_entity(Entity* entity)
{
  if (entity == nullptr)
    return;

  switch (entity->type)
  {
    case entity_type::Invalid: break;
    case entity_type::Player_Spawn_Entity: delete static_cast<Player_Spawn_Entity*>(entity); return;
    case entity_type::Player_Entity: delete static_cast<Player_Entity*>(entity); return;
    case entity_type::Weapon_Entity: delete static_cast<Weapon_Entity*>(entity); return;
    case entity_type::Rocket_Entity: delete static_cast<Rocket_Entity*>(entity); return;
    case entity_type::Particle_Emitter_Entity: delete static_cast<Particle_Emitter_Entity*>(entity); return;
    case entity_type::Trigger_Volume_Entity: delete static_cast<Trigger_Volume_Entity*>(entity); return;
    case entity_type::Light_Entity: delete static_cast<Light_Entity*>(entity); return;
    case entity_type::Physics_Body_Entity: delete static_cast<Physics_Body_Entity*>(entity); return;
  }
  assert(false && "destroy_entity: entity carries an invalid tag");
}

Span<const entity_type> placeable_entity_types()
{
  return {PLACEABLE_ENTITY_TYPES, PLACEABLE_ENTITY_TYPE_COUNT};
}

const uint32_t SCHEMA_HASH = 0x8bb2283cu;

} // namespace entities
