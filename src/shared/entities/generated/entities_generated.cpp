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

constexpr const char* Aim_Pose_VALUE_NAMES[] = {
  "Forward",
  "Upward",
  "Downward",
  "Left",
  "Right",
};

constexpr enum_type_info_t ENUM_INFOS[] = {
  {"Spawn_Type", {Spawn_Type_VALUE_NAMES, 2}},
  {"Team_Allegiance", {Team_Allegiance_VALUE_NAMES, 3}},
  {"Weapon", {Weapon_VALUE_NAMES, 3}},
  {"Weapon_Kind", {Weapon_Kind_VALUE_NAMES, 4}},
  {"Shader_Type", {Shader_Type_VALUE_NAMES, 2}},
  {"Shape_Kind", {Shape_Kind_VALUE_NAMES, 2}},
  {"Trigger_Action", {Trigger_Action_VALUE_NAMES, 4}},
  {"Fire_Mode", {Fire_Mode_VALUE_NAMES, 2}},
  {"Aim_Pose", {Aim_Pose_VALUE_NAMES, 5}},
};

constexpr field_info_t Box_Volume_FIELDS[] = {
  {.name = "position",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Box_Volume, position),
   .size_in_bytes = (uint32_t)sizeof(Box_Volume::position),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "half_extents",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Box_Volume, half_extents),
   .size_in_bytes = (uint32_t)sizeof(Box_Volume::half_extents),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
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
   .enum_info = &ENUM_INFOS[4]},
  {.name = "color",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Material, color),
   .size_in_bytes = (uint32_t)sizeof(Material::color),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "roughness",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Material, roughness),
   .size_in_bytes = (uint32_t)sizeof(Material::roughness),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
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
   .enum_info = NOT_AN_ENUM},
  {.name = "visible",
   .type = FIELD_TYPE_BOOL,
   .offset = (uint32_t)offsetof(Render, visible),
   .size_in_bytes = (uint32_t)sizeof(Render::visible),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "is_wireframe",
   .type = FIELD_TYPE_BOOL,
   .offset = (uint32_t)offsetof(Render, is_wireframe),
   .size_in_bytes = (uint32_t)sizeof(Render::is_wireframe),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "offset",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Render, offset),
   .size_in_bytes = (uint32_t)sizeof(Render::offset),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "scale",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Render, scale),
   .size_in_bytes = (uint32_t)sizeof(Render::scale),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "rotation",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Render, rotation),
   .size_in_bytes = (uint32_t)sizeof(Render::rotation),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "material",
   .type = FIELD_TYPE_COMPONENT,
   .offset = (uint32_t)offsetof(Render, material),
   .size_in_bytes = (uint32_t)sizeof(Render::material),
   .flags = 0u,
   .component_id = 1,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
};

constexpr field_info_t Light_FIELDS[] = {
  {.name = "color",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Light, color),
   .size_in_bytes = (uint32_t)sizeof(Light::color),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "intensity",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Light, intensity),
   .size_in_bytes = (uint32_t)sizeof(Light::intensity),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
};

constexpr field_info_t Inventory_FIELDS[] = {
  {.name = "weapons.Knife",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)(offsetof(Inventory, weapons) + 0 * sizeof(Inventory::weapons.values[0])),
   .size_in_bytes = (uint32_t)sizeof(Inventory::weapons.values[0]),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "weapons.Scout",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)(offsetof(Inventory, weapons) + 1 * sizeof(Inventory::weapons.values[0])),
   .size_in_bytes = (uint32_t)sizeof(Inventory::weapons.values[0]),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "weapons.Rocket_Launcher",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)(offsetof(Inventory, weapons) + 2 * sizeof(Inventory::weapons.values[0])),
   .size_in_bytes = (uint32_t)sizeof(Inventory::weapons.values[0]),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "active_weapon",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Inventory, active_weapon),
   .size_in_bytes = (uint32_t)sizeof(Inventory::active_weapon),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = &ENUM_INFOS[2]},
  {.name = "deploy_complete_time",
   .type = FIELD_TYPE_U64,
   .offset = (uint32_t)offsetof(Inventory, deploy_complete_time),
   .size_in_bytes = (uint32_t)sizeof(Inventory::deploy_complete_time),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
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
   .enum_info = NOT_AN_ENUM},
  {.name = "position",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Player_Spawn_Entity, position),
   .size_in_bytes = (uint32_t)sizeof(Player_Spawn_Entity::position),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "orientation",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Player_Spawn_Entity, orientation),
   .size_in_bytes = (uint32_t)sizeof(Player_Spawn_Entity::orientation),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "spawn_type",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Player_Spawn_Entity, spawn_type),
   .size_in_bytes = (uint32_t)sizeof(Player_Spawn_Entity::spawn_type),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = &ENUM_INFOS[0]},
  {.name = "team_allegiance",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Player_Spawn_Entity, team_allegiance),
   .size_in_bytes = (uint32_t)sizeof(Player_Spawn_Entity::team_allegiance),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = &ENUM_INFOS[1]},
};

constexpr field_info_t Player_Spectate_Entity_FIELDS[] = {
  {.name = "entity_id",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Player_Spectate_Entity, entity_id),
   .size_in_bytes = (uint32_t)sizeof(Player_Spectate_Entity::entity_id),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "position",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Player_Spectate_Entity, position),
   .size_in_bytes = (uint32_t)sizeof(Player_Spectate_Entity::position),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "orientation",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Player_Spectate_Entity, orientation),
   .size_in_bytes = (uint32_t)sizeof(Player_Spectate_Entity::orientation),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
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
   .enum_info = NOT_AN_ENUM},
  {.name = "position",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Player_Entity, position),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::position),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "orientation",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Player_Entity, orientation),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::orientation),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "view_angle_yaw",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Player_Entity, view_angle_yaw),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::view_angle_yaw),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "view_angle_pitch",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Player_Entity, view_angle_pitch),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::view_angle_pitch),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "body_yaw",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Player_Entity, body_yaw),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::body_yaw),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "health",
   .type = FIELD_TYPE_I32,
   .offset = (uint32_t)offsetof(Player_Entity, health),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::health),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "death_tick",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Player_Entity, death_tick),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::death_tick),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "last_fire_tick",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Player_Entity, last_fire_tick),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::last_fire_tick),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "last_fire_weapon",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Player_Entity, last_fire_weapon),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::last_fire_weapon),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = &ENUM_INFOS[2]},
  {.name = "reload_complete_time",
   .type = FIELD_TYPE_U64,
   .offset = (uint32_t)offsetof(Player_Entity, reload_complete_time),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::reload_complete_time),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "last_empty_fire_warning_tick",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Player_Entity, last_empty_fire_warning_tick),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::last_empty_fire_warning_tick),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "last_hit_tick",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Player_Entity, last_hit_tick),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::last_hit_tick),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "last_hit_was_headshot",
   .type = FIELD_TYPE_BOOL,
   .offset = (uint32_t)offsetof(Player_Entity, last_hit_was_headshot),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::last_hit_was_headshot),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "client_slot_index",
   .type = FIELD_TYPE_I32,
   .offset = (uint32_t)offsetof(Player_Entity, client_slot_index),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::client_slot_index),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "velocity",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Player_Entity, velocity),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::velocity),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "inventory",
   .type = FIELD_TYPE_COMPONENT,
   .offset = (uint32_t)offsetof(Player_Entity, inventory),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::inventory),
   .flags = 0u,
   .component_id = 4,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "render",
   .type = FIELD_TYPE_COMPONENT,
   .offset = (uint32_t)offsetof(Player_Entity, render),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::render),
   .flags = 0u,
   .component_id = 2,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "team_allegiance",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Player_Entity, team_allegiance),
   .size_in_bytes = (uint32_t)sizeof(Player_Entity::team_allegiance),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = &ENUM_INFOS[1]},
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
   .enum_info = NOT_AN_ENUM},
  {.name = "position",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Weapon_Entity, position),
   .size_in_bytes = (uint32_t)sizeof(Weapon_Entity::position),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "orientation",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Weapon_Entity, orientation),
   .size_in_bytes = (uint32_t)sizeof(Weapon_Entity::orientation),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "ammo",
   .type = FIELD_TYPE_I32,
   .offset = (uint32_t)offsetof(Weapon_Entity, ammo),
   .size_in_bytes = (uint32_t)sizeof(Weapon_Entity::ammo),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "weapon_id",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Weapon_Entity, weapon_id),
   .size_in_bytes = (uint32_t)sizeof(Weapon_Entity::weapon_id),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = &ENUM_INFOS[2]},
  {.name = "owner_uid",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Weapon_Entity, owner_uid),
   .size_in_bytes = (uint32_t)sizeof(Weapon_Entity::owner_uid),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "next_fire_time",
   .type = FIELD_TYPE_U64,
   .offset = (uint32_t)offsetof(Weapon_Entity, next_fire_time),
   .size_in_bytes = (uint32_t)sizeof(Weapon_Entity::next_fire_time),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "render",
   .type = FIELD_TYPE_COMPONENT,
   .offset = (uint32_t)offsetof(Weapon_Entity, render),
   .size_in_bytes = (uint32_t)sizeof(Weapon_Entity::render),
   .flags = 0u,
   .component_id = 2,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
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
   .enum_info = NOT_AN_ENUM},
  {.name = "position",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Rocket_Entity, position),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Entity::position),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "orientation",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Rocket_Entity, orientation),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Entity::orientation),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "velocity",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Rocket_Entity, velocity),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Entity::velocity),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "lifetime",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Rocket_Entity, lifetime),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Entity::lifetime),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "damage_amount",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Rocket_Entity, damage_amount),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Entity::damage_amount),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "damage_radius",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Rocket_Entity, damage_radius),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Entity::damage_radius),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "knockback_force",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Rocket_Entity, knockback_force),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Entity::knockback_force),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "owner_id",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Rocket_Entity, owner_id),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Entity::owner_id),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "collision_radius",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Rocket_Entity, collision_radius),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Entity::collision_radius),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "render",
   .type = FIELD_TYPE_COMPONENT,
   .offset = (uint32_t)offsetof(Rocket_Entity, render),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Entity::render),
   .flags = 0u,
   .component_id = 2,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
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
   .enum_info = NOT_AN_ENUM},
  {.name = "position",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, position),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::position),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "orientation",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, orientation),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::orientation),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "sprite",
   .type = FIELD_TYPE_ASSET,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, sprite),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::sprite),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = 1,
   .enum_info = NOT_AN_ENUM},
  {.name = "emit_rate",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, emit_rate),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::emit_rate),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "max_particles",
   .type = FIELD_TYPE_I32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, max_particles),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::max_particles),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "lifetime_min",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, lifetime_min),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::lifetime_min),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "lifetime_max",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, lifetime_max),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::lifetime_max),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "velocity_min",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, velocity_min),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::velocity_min),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "velocity_max",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, velocity_max),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::velocity_max),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "spread",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, spread),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::spread),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "gravity",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, gravity),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::gravity),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "drag",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, drag),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::drag),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "size_start",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, size_start),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::size_start),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "size_end",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, size_end),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::size_end),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "rotation_speed_min",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, rotation_speed_min),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::rotation_speed_min),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "rotation_speed_max",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, rotation_speed_max),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::rotation_speed_max),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "color_start",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, color_start),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::color_start),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "color_end",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, color_end),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::color_end),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "alpha_start",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, alpha_start),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::alpha_start),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "alpha_end",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, alpha_end),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::alpha_end),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "emitter_lifetime",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, emitter_lifetime),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::emitter_lifetime),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "parent_entity_id",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Particle_Emitter_Entity, parent_entity_id),
   .size_in_bytes = (uint32_t)sizeof(Particle_Emitter_Entity::parent_entity_id),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
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
   .enum_info = NOT_AN_ENUM},
  {.name = "position",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Trigger_Volume_Entity, position),
   .size_in_bytes = (uint32_t)sizeof(Trigger_Volume_Entity::position),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "orientation",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Trigger_Volume_Entity, orientation),
   .size_in_bytes = (uint32_t)sizeof(Trigger_Volume_Entity::orientation),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "volume",
   .type = FIELD_TYPE_COMPONENT,
   .offset = (uint32_t)offsetof(Trigger_Volume_Entity, volume),
   .size_in_bytes = (uint32_t)sizeof(Trigger_Volume_Entity::volume),
   .flags = 0u,
   .component_id = 0,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "action",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Trigger_Volume_Entity, action),
   .size_in_bytes = (uint32_t)sizeof(Trigger_Volume_Entity::action),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = &ENUM_INFOS[6]},
  {.name = "fire_mode",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Trigger_Volume_Entity, fire_mode),
   .size_in_bytes = (uint32_t)sizeof(Trigger_Volume_Entity::fire_mode),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = &ENUM_INFOS[7]},
  {.name = "param_target_name",
   .type = FIELD_TYPE_STRING,
   .offset = (uint32_t)offsetof(Trigger_Volume_Entity, param_target_name),
   .size_in_bytes = (uint32_t)sizeof(Trigger_Volume_Entity::param_target_name),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = 64,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "param_string",
   .type = FIELD_TYPE_STRING,
   .offset = (uint32_t)offsetof(Trigger_Volume_Entity, param_string),
   .size_in_bytes = (uint32_t)sizeof(Trigger_Volume_Entity::param_string),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = 128,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "param_float",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Trigger_Volume_Entity, param_float),
   .size_in_bytes = (uint32_t)sizeof(Trigger_Volume_Entity::param_float),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
};

constexpr field_info_t Point_Light_Entity_FIELDS[] = {
  {.name = "entity_id",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Point_Light_Entity, entity_id),
   .size_in_bytes = (uint32_t)sizeof(Point_Light_Entity::entity_id),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "position",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Point_Light_Entity, position),
   .size_in_bytes = (uint32_t)sizeof(Point_Light_Entity::position),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "orientation",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Point_Light_Entity, orientation),
   .size_in_bytes = (uint32_t)sizeof(Point_Light_Entity::orientation),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "light",
   .type = FIELD_TYPE_COMPONENT,
   .offset = (uint32_t)offsetof(Point_Light_Entity, light),
   .size_in_bytes = (uint32_t)sizeof(Point_Light_Entity::light),
   .flags = 0u,
   .component_id = 3,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "range",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Point_Light_Entity, range),
   .size_in_bytes = (uint32_t)sizeof(Point_Light_Entity::range),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
};

constexpr field_info_t Spot_Light_Entity_FIELDS[] = {
  {.name = "entity_id",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Spot_Light_Entity, entity_id),
   .size_in_bytes = (uint32_t)sizeof(Spot_Light_Entity::entity_id),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "position",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Spot_Light_Entity, position),
   .size_in_bytes = (uint32_t)sizeof(Spot_Light_Entity::position),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "orientation",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Spot_Light_Entity, orientation),
   .size_in_bytes = (uint32_t)sizeof(Spot_Light_Entity::orientation),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "light",
   .type = FIELD_TYPE_COMPONENT,
   .offset = (uint32_t)offsetof(Spot_Light_Entity, light),
   .size_in_bytes = (uint32_t)sizeof(Spot_Light_Entity::light),
   .flags = 0u,
   .component_id = 3,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "range",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Spot_Light_Entity, range),
   .size_in_bytes = (uint32_t)sizeof(Spot_Light_Entity::range),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "inner_degrees",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Spot_Light_Entity, inner_degrees),
   .size_in_bytes = (uint32_t)sizeof(Spot_Light_Entity::inner_degrees),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "outer_degrees",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Spot_Light_Entity, outer_degrees),
   .size_in_bytes = (uint32_t)sizeof(Spot_Light_Entity::outer_degrees),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
};

constexpr field_info_t Directional_Light_Entity_FIELDS[] = {
  {.name = "entity_id",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Directional_Light_Entity, entity_id),
   .size_in_bytes = (uint32_t)sizeof(Directional_Light_Entity::entity_id),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "position",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Directional_Light_Entity, position),
   .size_in_bytes = (uint32_t)sizeof(Directional_Light_Entity::position),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "orientation",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Directional_Light_Entity, orientation),
   .size_in_bytes = (uint32_t)sizeof(Directional_Light_Entity::orientation),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "light",
   .type = FIELD_TYPE_COMPONENT,
   .offset = (uint32_t)offsetof(Directional_Light_Entity, light),
   .size_in_bytes = (uint32_t)sizeof(Directional_Light_Entity::light),
   .flags = 0u,
   .component_id = 3,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
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
   .enum_info = NOT_AN_ENUM},
  {.name = "position",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Physics_Body_Entity, position),
   .size_in_bytes = (uint32_t)sizeof(Physics_Body_Entity::position),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "orientation",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Physics_Body_Entity, orientation),
   .size_in_bytes = (uint32_t)sizeof(Physics_Body_Entity::orientation),
   .flags = 7u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "shape",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Physics_Body_Entity, shape),
   .size_in_bytes = (uint32_t)sizeof(Physics_Body_Entity::shape),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = &ENUM_INFOS[5]},
  {.name = "size",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Physics_Body_Entity, size),
   .size_in_bytes = (uint32_t)sizeof(Physics_Body_Entity::size),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "velocity",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Physics_Body_Entity, velocity),
   .size_in_bytes = (uint32_t)sizeof(Physics_Body_Entity::velocity),
   .flags = 1u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "mass",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Physics_Body_Entity, mass),
   .size_in_bytes = (uint32_t)sizeof(Physics_Body_Entity::mass),
   .flags = 6u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "render",
   .type = FIELD_TYPE_COMPONENT,
   .offset = (uint32_t)offsetof(Physics_Body_Entity, render),
   .size_in_bytes = (uint32_t)sizeof(Physics_Body_Entity::render),
   .flags = 0u,
   .component_id = 2,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
};

constexpr component_type_info_t COMPONENT_INFOS[] = {
  {"Box_Volume", {Box_Volume_FIELDS, 2}, (uint32_t)sizeof(Box_Volume)},
  {"Material", {Material_FIELDS, 3}, (uint32_t)sizeof(Material)},
  {"Render", {Render_FIELDS, 7}, (uint32_t)sizeof(Render)},
  {"Light", {Light_FIELDS, 2}, (uint32_t)sizeof(Light)},
  {"Inventory", {Inventory_FIELDS, 5}, (uint32_t)sizeof(Inventory)},
};

Entity* construct_Player_Spawn_Entity(void* memory) { return new (memory) Player_Spawn_Entity(); }
Entity* construct_Player_Spectate_Entity(void* memory) { return new (memory) Player_Spectate_Entity(); }
Entity* construct_Player_Entity(void* memory) { return new (memory) Player_Entity(); }
Entity* construct_Weapon_Entity(void* memory) { return new (memory) Weapon_Entity(); }
Entity* construct_Rocket_Entity(void* memory) { return new (memory) Rocket_Entity(); }
Entity* construct_Particle_Emitter_Entity(void* memory) { return new (memory) Particle_Emitter_Entity(); }
Entity* construct_Trigger_Volume_Entity(void* memory) { return new (memory) Trigger_Volume_Entity(); }
Entity* construct_Point_Light_Entity(void* memory) { return new (memory) Point_Light_Entity(); }
Entity* construct_Spot_Light_Entity(void* memory) { return new (memory) Spot_Light_Entity(); }
Entity* construct_Directional_Light_Entity(void* memory) { return new (memory) Directional_Light_Entity(); }
Entity* construct_Physics_Body_Entity(void* memory) { return new (memory) Physics_Body_Entity(); }

Entity* as_base_Player_Spawn_Entity(void* memory) { return static_cast<Entity*>((Player_Spawn_Entity*)memory); }
Entity* as_base_Player_Spectate_Entity(void* memory) { return static_cast<Entity*>((Player_Spectate_Entity*)memory); }
Entity* as_base_Player_Entity(void* memory) { return static_cast<Entity*>((Player_Entity*)memory); }
Entity* as_base_Weapon_Entity(void* memory) { return static_cast<Entity*>((Weapon_Entity*)memory); }
Entity* as_base_Rocket_Entity(void* memory) { return static_cast<Entity*>((Rocket_Entity*)memory); }
Entity* as_base_Particle_Emitter_Entity(void* memory) { return static_cast<Entity*>((Particle_Emitter_Entity*)memory); }
Entity* as_base_Trigger_Volume_Entity(void* memory) { return static_cast<Entity*>((Trigger_Volume_Entity*)memory); }
Entity* as_base_Point_Light_Entity(void* memory) { return static_cast<Entity*>((Point_Light_Entity*)memory); }
Entity* as_base_Spot_Light_Entity(void* memory) { return static_cast<Entity*>((Spot_Light_Entity*)memory); }
Entity* as_base_Directional_Light_Entity(void* memory) { return static_cast<Entity*>((Directional_Light_Entity*)memory); }
Entity* as_base_Physics_Body_Entity(void* memory) { return static_cast<Entity*>((Physics_Body_Entity*)memory); }

constexpr entity_type_info_t ENTITY_INFOS[] = {
  {"", "", {}, 0, 0, 0, false, nullptr, nullptr}, // Invalid
  {"player_spawn_entity", "Player Spawn", {Player_Spawn_Entity_FIELDS, 5}, (uint32_t)sizeof(Player_Spawn_Entity), (uint32_t)alignof(Player_Spawn_Entity), 0u, false, construct_Player_Spawn_Entity, as_base_Player_Spawn_Entity},
  {"player_spectate_entity", "Player Spectate", {Player_Spectate_Entity_FIELDS, 3}, (uint32_t)sizeof(Player_Spectate_Entity), (uint32_t)alignof(Player_Spectate_Entity), 0u, false, construct_Player_Spectate_Entity, as_base_Player_Spectate_Entity},
  {"player_entity", "Player", {Player_Entity_FIELDS, 19}, (uint32_t)sizeof(Player_Entity), (uint32_t)alignof(Player_Entity), 20u, true, construct_Player_Entity, as_base_Player_Entity},
  {"weapon_entity", "Weapon", {Weapon_Entity_FIELDS, 8}, (uint32_t)sizeof(Weapon_Entity), (uint32_t)alignof(Weapon_Entity), 4u, false, construct_Weapon_Entity, as_base_Weapon_Entity},
  {"rocket_entity", "Rocket", {Rocket_Entity_FIELDS, 11}, (uint32_t)sizeof(Rocket_Entity), (uint32_t)alignof(Rocket_Entity), 4u, true, construct_Rocket_Entity, as_base_Rocket_Entity},
  {"particle_emitter_entity", "Particle Emitter", {Particle_Emitter_Entity_FIELDS, 23}, (uint32_t)sizeof(Particle_Emitter_Entity), (uint32_t)alignof(Particle_Emitter_Entity), 0u, false, construct_Particle_Emitter_Entity, as_base_Particle_Emitter_Entity},
  {"trigger_volume_entity", "Trigger Volume", {Trigger_Volume_Entity_FIELDS, 9}, (uint32_t)sizeof(Trigger_Volume_Entity), (uint32_t)alignof(Trigger_Volume_Entity), 1u, false, construct_Trigger_Volume_Entity, as_base_Trigger_Volume_Entity},
  {"point_light_entity", "Point Light", {Point_Light_Entity_FIELDS, 5}, (uint32_t)sizeof(Point_Light_Entity), (uint32_t)alignof(Point_Light_Entity), 8u, false, construct_Point_Light_Entity, as_base_Point_Light_Entity},
  {"spot_light_entity", "Spot Light", {Spot_Light_Entity_FIELDS, 7}, (uint32_t)sizeof(Spot_Light_Entity), (uint32_t)alignof(Spot_Light_Entity), 8u, false, construct_Spot_Light_Entity, as_base_Spot_Light_Entity},
  {"directional_light_entity", "Directional Light", {Directional_Light_Entity_FIELDS, 4}, (uint32_t)sizeof(Directional_Light_Entity), (uint32_t)alignof(Directional_Light_Entity), 8u, false, construct_Directional_Light_Entity, as_base_Directional_Light_Entity},
  {"physics_body_entity", "Physics Body", {Physics_Body_Entity_FIELDS, 8}, (uint32_t)sizeof(Physics_Body_Entity), (uint32_t)alignof(Physics_Body_Entity), 4u, false, construct_Physics_Body_Entity, as_base_Physics_Body_Entity},
};

constexpr int32_t COMPONENT_OFFSETS[][5] = {
  {-1, -1, -1, -1, -1}, // Invalid
  {-1, -1, -1, -1, -1}, // Player_Spawn_Entity
  {-1, -1, -1, -1, -1}, // Player_Spectate_Entity
  {-1, -1, (int32_t)offsetof(Player_Entity, render), -1, (int32_t)offsetof(Player_Entity, inventory)}, // Player_Entity
  {-1, -1, (int32_t)offsetof(Weapon_Entity, render), -1, -1}, // Weapon_Entity
  {-1, -1, (int32_t)offsetof(Rocket_Entity, render), -1, -1}, // Rocket_Entity
  {-1, -1, -1, -1, -1}, // Particle_Emitter_Entity
  {(int32_t)offsetof(Trigger_Volume_Entity, volume), -1, -1, -1, -1}, // Trigger_Volume_Entity
  {-1, -1, -1, (int32_t)offsetof(Point_Light_Entity, light), -1}, // Point_Light_Entity
  {-1, -1, -1, (int32_t)offsetof(Spot_Light_Entity, light), -1}, // Spot_Light_Entity
  {-1, -1, -1, (int32_t)offsetof(Directional_Light_Entity, light), -1}, // Directional_Light_Entity
  {-1, -1, (int32_t)offsetof(Physics_Body_Entity, render), -1, -1}, // Physics_Body_Entity
};

constexpr uint32_t PLACEABLE_ENTITY_TYPE_COUNT = 9;
constexpr entity_type PLACEABLE_ENTITY_TYPES[] = {
  entity_type::Player_Spawn_Entity,
  entity_type::Player_Spectate_Entity,
  entity_type::Weapon_Entity,
  entity_type::Particle_Emitter_Entity,
  entity_type::Trigger_Volume_Entity,
  entity_type::Point_Light_Entity,
  entity_type::Spot_Light_Entity,
  entity_type::Directional_Light_Entity,
  entity_type::Physics_Body_Entity,
};

} // namespace

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

template <> std::optional<Spawn_Type> try_from_string<Spawn_Type>(std::string_view text)
{
  if (text == "Human") return Spawn_Type::Human;
  if (text == "Bot") return Spawn_Type::Bot;
  return std::nullopt;
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

template <> std::optional<Team_Allegiance> try_from_string<Team_Allegiance>(std::string_view text)
{
  if (text == "Red") return Team_Allegiance::Red;
  if (text == "Blu") return Team_Allegiance::Blu;
  if (text == "Free_For_All") return Team_Allegiance::Free_For_All;
  return std::nullopt;
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

template <> std::optional<Weapon> try_from_string<Weapon>(std::string_view text)
{
  if (text == "Knife") return Weapon::Knife;
  if (text == "Scout") return Weapon::Scout;
  if (text == "Rocket_Launcher") return Weapon::Rocket_Launcher;
  return std::nullopt;
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

template <> std::optional<Weapon_Kind> try_from_string<Weapon_Kind>(std::string_view text)
{
  if (text == "Melee") return Weapon_Kind::Melee;
  if (text == "Hitscan") return Weapon_Kind::Hitscan;
  if (text == "Projectile") return Weapon_Kind::Projectile;
  if (text == "Sniper") return Weapon_Kind::Sniper;
  return std::nullopt;
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

template <> std::optional<Shader_Type> try_from_string<Shader_Type>(std::string_view text)
{
  if (text == "Lit") return Shader_Type::Lit;
  if (text == "Unlit") return Shader_Type::Unlit;
  return std::nullopt;
}

const char* to_string(Shape_Kind value)
{
  switch (value)
  {
    case Shape_Kind::Sphere: return "Sphere";
    case Shape_Kind::Box: return "Box";
  }
  assert(false && "invalid Shape_Kind");
  return "";
}

template <> std::optional<Shape_Kind> try_from_string<Shape_Kind>(std::string_view text)
{
  if (text == "Sphere") return Shape_Kind::Sphere;
  if (text == "Box") return Shape_Kind::Box;
  return std::nullopt;
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

template <> std::optional<Trigger_Action> try_from_string<Trigger_Action>(std::string_view text)
{
  if (text == "Kill") return Trigger_Action::Kill;
  if (text == "Set_Health") return Trigger_Action::Set_Health;
  if (text == "Print_Message") return Trigger_Action::Print_Message;
  if (text == "Warp_To_Spawn") return Trigger_Action::Warp_To_Spawn;
  return std::nullopt;
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

template <> std::optional<Fire_Mode> try_from_string<Fire_Mode>(std::string_view text)
{
  if (text == "On_Enter") return Fire_Mode::On_Enter;
  if (text == "Every_Tick") return Fire_Mode::Every_Tick;
  return std::nullopt;
}

const char* to_string(Aim_Pose value)
{
  switch (value)
  {
    case Aim_Pose::Forward: return "Forward";
    case Aim_Pose::Upward: return "Upward";
    case Aim_Pose::Downward: return "Downward";
    case Aim_Pose::Left: return "Left";
    case Aim_Pose::Right: return "Right";
  }
  assert(false && "invalid Aim_Pose");
  return "";
}

template <> std::optional<Aim_Pose> try_from_string<Aim_Pose>(std::string_view text)
{
  if (text == "Forward") return Aim_Pose::Forward;
  if (text == "Upward") return Aim_Pose::Upward;
  if (text == "Downward") return Aim_Pose::Downward;
  if (text == "Left") return Aim_Pose::Left;
  if (text == "Right") return Aim_Pose::Right;
  return std::nullopt;
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
    case entity_type::Player_Spectate_Entity: return new Player_Spectate_Entity();
    case entity_type::Player_Entity: return new Player_Entity();
    case entity_type::Weapon_Entity: return new Weapon_Entity();
    case entity_type::Rocket_Entity: return new Rocket_Entity();
    case entity_type::Particle_Emitter_Entity: return new Particle_Emitter_Entity();
    case entity_type::Trigger_Volume_Entity: return new Trigger_Volume_Entity();
    case entity_type::Point_Light_Entity: return new Point_Light_Entity();
    case entity_type::Spot_Light_Entity: return new Spot_Light_Entity();
    case entity_type::Directional_Light_Entity: return new Directional_Light_Entity();
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
    case entity_type::Player_Spectate_Entity: delete static_cast<Player_Spectate_Entity*>(entity); return;
    case entity_type::Player_Entity: delete static_cast<Player_Entity*>(entity); return;
    case entity_type::Weapon_Entity: delete static_cast<Weapon_Entity*>(entity); return;
    case entity_type::Rocket_Entity: delete static_cast<Rocket_Entity*>(entity); return;
    case entity_type::Particle_Emitter_Entity: delete static_cast<Particle_Emitter_Entity*>(entity); return;
    case entity_type::Trigger_Volume_Entity: delete static_cast<Trigger_Volume_Entity*>(entity); return;
    case entity_type::Point_Light_Entity: delete static_cast<Point_Light_Entity*>(entity); return;
    case entity_type::Spot_Light_Entity: delete static_cast<Spot_Light_Entity*>(entity); return;
    case entity_type::Directional_Light_Entity: delete static_cast<Directional_Light_Entity*>(entity); return;
    case entity_type::Physics_Body_Entity: delete static_cast<Physics_Body_Entity*>(entity); return;
  }
  assert(false && "destroy_entity: entity carries an invalid tag");
}

Span<const entity_type> placeable_entity_types()
{
  return {PLACEABLE_ENTITY_TYPES, PLACEABLE_ENTITY_TYPE_COUNT};
}

const uint32_t SCHEMA_HASH = 0xfcfab397u;

} // namespace entities
