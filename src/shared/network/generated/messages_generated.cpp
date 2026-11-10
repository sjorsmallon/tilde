// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/network/messages.def by def_gen. Do not edit.
#include "network/generated/messages_generated.hpp"

#include "log.hpp"
#include "network/field_codec.hpp"

#include <cassert>
#include <cstddef>
#include <format>

namespace shared
{

namespace
{

constexpr const char* BRACKET_STATUS_VALUE_NAMES[] = {
  "Ok",
  "Clamped",
  "Absent",
  "Malformed",
  "Unheld",
};

constexpr const char* BOT_GOAL_VALUE_NAMES[] = {
  "Idle",
  "Chase",
  "Attack",
  "Retreat",
};

constexpr const char* BOT_BEHAVIOR_VALUE_NAMES[] = {
  "Idle",
  "Chase",
  "Regular",
};

constexpr enum_type_info_t ENUM_INFOS[] = {
  {"Bracket_Status", {BRACKET_STATUS_VALUE_NAMES, 5}},
  {"Bot_Goal", {BOT_GOAL_VALUE_NAMES, 4}},
  {"Bot_Behavior", {BOT_BEHAVIOR_VALUE_NAMES, 3}},
};

// Each declaration's own leaves; a component field and a counted list are
// code in the codec below, not rows here.
constexpr field_info_t EFFECT_SITE_FIELDS[] = {
  {.name = "origin",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Effect_Site, origin),
   .size_in_bytes = (uint32_t)sizeof(Effect_Site::origin),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "normal",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Effect_Site, normal),
   .size_in_bytes = (uint32_t)sizeof(Effect_Site::normal),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "color",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Effect_Site, color),
   .size_in_bytes = (uint32_t)sizeof(Effect_Site::color),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "scale",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Effect_Site, scale),
   .size_in_bytes = (uint32_t)sizeof(Effect_Site::scale),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "attached_entity",
   .type = FIELD_TYPE_ENTITY_UID,
   .offset = (uint32_t)offsetof(Effect_Site, attached_entity),
   .size_in_bytes = (uint32_t)sizeof(Effect_Site::attached_entity),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "surface_material",
   .type = FIELD_TYPE_U16,
   .offset = (uint32_t)offsetof(Effect_Site, surface_material),
   .size_in_bytes = (uint32_t)sizeof(Effect_Site::surface_material),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
};

constexpr field_info_t SERVER_MAP_FIELDS[] = {
  {.name = "generation",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Server_Map, generation),
   .size_in_bytes = (uint32_t)sizeof(Server_Map::generation),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "path",
   .type = FIELD_TYPE_STRING,
   .offset = (uint32_t)offsetof(Server_Map, path),
   .size_in_bytes = (uint32_t)sizeof(Server_Map::path),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = 128,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "name",
   .type = FIELD_TYPE_STRING,
   .offset = (uint32_t)offsetof(Server_Map, name),
   .size_in_bytes = (uint32_t)sizeof(Server_Map::name),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = 64,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "content_hash",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Server_Map, content_hash),
   .size_in_bytes = (uint32_t)sizeof(Server_Map::content_hash),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
};

constexpr field_info_t CVAR_VALUE_FIELDS[] = {
  {.name = "id",
   .type = FIELD_TYPE_U16,
   .offset = (uint32_t)offsetof(Cvar_Value, id),
   .size_in_bytes = (uint32_t)sizeof(Cvar_Value::id),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "text",
   .type = FIELD_TYPE_STRING,
   .offset = (uint32_t)offsetof(Cvar_Value, text),
   .size_in_bytes = (uint32_t)sizeof(Cvar_Value::text),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = 64,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
};

constexpr field_info_t VIEW_FIELDS[] = {
  {.name = "yaw",
   .type = FIELD_TYPE_F32_EXACT,
   .offset = (uint32_t)offsetof(View, yaw),
   .size_in_bytes = (uint32_t)sizeof(View::yaw),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "pitch",
   .type = FIELD_TYPE_F32_EXACT,
   .offset = (uint32_t)offsetof(View, pitch),
   .size_in_bytes = (uint32_t)sizeof(View::pitch),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
};

constexpr field_info_t BUTTON_CHANGE_FIELDS[] = {
  {.name = "slot",
   .type = FIELD_TYPE_U8,
   .offset = (uint32_t)offsetof(Button_Change, slot),
   .size_in_bytes = (uint32_t)sizeof(Button_Change::slot),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "buttons_after",
   .type = FIELD_TYPE_U64,
   .offset = (uint32_t)offsetof(Button_Change, buttons_after),
   .size_in_bytes = (uint32_t)sizeof(Button_Change::buttons_after),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
};

constexpr field_info_t TICK_INPUT_FIELDS[] = {
  {.name = "buttons_at_start",
   .type = FIELD_TYPE_U64,
   .offset = (uint32_t)offsetof(Tick_Input, buttons_at_start),
   .size_in_bytes = (uint32_t)sizeof(Tick_Input::buttons_at_start),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
};

constexpr field_info_t SHOT_DEBUG_TARGET_FIELDS[] = {
  {.name = "player_uid",
   .type = FIELD_TYPE_ENTITY_UID,
   .offset = (uint32_t)offsetof(Shot_Debug_Target, player_uid),
   .size_in_bytes = (uint32_t)sizeof(Shot_Debug_Target::player_uid),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "feet_position",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Shot_Debug_Target, feet_position),
   .size_in_bytes = (uint32_t)sizeof(Shot_Debug_Target::feet_position),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "body_yaw",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Shot_Debug_Target, body_yaw),
   .size_in_bytes = (uint32_t)sizeof(Shot_Debug_Target::body_yaw),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "view_yaw",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Shot_Debug_Target, view_yaw),
   .size_in_bytes = (uint32_t)sizeof(Shot_Debug_Target::view_yaw),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "view_pitch",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Shot_Debug_Target, view_pitch),
   .size_in_bytes = (uint32_t)sizeof(Shot_Debug_Target::view_pitch),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
};

constexpr field_info_t BOT_DEBUG_ENTRY_FIELDS[] = {
  {.name = "slot",
   .type = FIELD_TYPE_I32,
   .offset = (uint32_t)offsetof(Bot_Debug_Entry, slot),
   .size_in_bytes = (uint32_t)sizeof(Bot_Debug_Entry::slot),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "goal",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Bot_Debug_Entry, goal),
   .size_in_bytes = (uint32_t)sizeof(Bot_Debug_Entry::goal),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = &ENUM_INFOS[1],
   .list = NOT_A_LIST},
  {.name = "behavior",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Bot_Debug_Entry, behavior),
   .size_in_bytes = (uint32_t)sizeof(Bot_Debug_Entry::behavior),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = &ENUM_INFOS[2],
   .list = NOT_A_LIST},
};

constexpr field_info_t BOT_DEBUG_ENTRY_PATH_ELEMENT[] = {
  {.name = "path",
   .type = FIELD_TYPE_V3,
   .offset = 0u,
   .size_in_bytes = (uint32_t)sizeof(decltype(Bot_Debug_Entry::path)::element_t),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
};

constexpr field_info_t ROCKET_DETONATED_FIELDS[] = {
  {.name = "attacker_id",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Rocket_Detonated, attacker_id),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Detonated::attacker_id),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "victim_id",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Rocket_Detonated, victim_id),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Detonated::victim_id),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "weapon_id",
   .type = FIELD_TYPE_U16,
   .offset = (uint32_t)offsetof(Rocket_Detonated, weapon_id),
   .size_in_bytes = (uint32_t)sizeof(Rocket_Detonated::weapon_id),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
};

constexpr field_info_t PLAYER_DIED_FIELDS[] = {
  {.name = "victim_id",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Player_Died, victim_id),
   .size_in_bytes = (uint32_t)sizeof(Player_Died::victim_id),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "attacker_id",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Player_Died, attacker_id),
   .size_in_bytes = (uint32_t)sizeof(Player_Died::attacker_id),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "weapon_id",
   .type = FIELD_TYPE_U16,
   .offset = (uint32_t)offsetof(Player_Died, weapon_id),
   .size_in_bytes = (uint32_t)sizeof(Player_Died::weapon_id),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "was_headshot",
   .type = FIELD_TYPE_BOOL,
   .offset = (uint32_t)offsetof(Player_Died, was_headshot),
   .size_in_bytes = (uint32_t)sizeof(Player_Died::was_headshot),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
};

constexpr field_info_t PLAYER_SPAWNED_FIELDS[] = {
  {.name = "player_id",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Player_Spawned, player_id),
   .size_in_bytes = (uint32_t)sizeof(Player_Spawned::player_id),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "client_slot",
   .type = FIELD_TYPE_I32,
   .offset = (uint32_t)offsetof(Player_Spawned, client_slot),
   .size_in_bytes = (uint32_t)sizeof(Player_Spawned::client_slot),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "spawn_position",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Player_Spawned, spawn_position),
   .size_in_bytes = (uint32_t)sizeof(Player_Spawned::spawn_position),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "spawn_orientation",
   .type = FIELD_TYPE_QUAT,
   .offset = (uint32_t)offsetof(Player_Spawned, spawn_orientation),
   .size_in_bytes = (uint32_t)sizeof(Player_Spawned::spawn_orientation),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
};

constexpr field_info_t OBJECTIVE_REACHED_FIELDS[] = {
  {.name = "completed_by",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Objective_Reached, completed_by),
   .size_in_bytes = (uint32_t)sizeof(Objective_Reached::completed_by),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "attempt_ticks",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Objective_Reached, attempt_ticks),
   .size_in_bytes = (uint32_t)sizeof(Objective_Reached::attempt_ticks),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "best_ticks",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Objective_Reached, best_ticks),
   .size_in_bytes = (uint32_t)sizeof(Objective_Reached::best_ticks),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
};

constexpr field_info_t SHOT_IMPACT_FIELDS[] = {
  {.name = "region",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Shot_Impact, region),
   .size_in_bytes = (uint32_t)sizeof(Shot_Impact::region),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = &entities::ENUM_INFOS[(uint32_t)entities::enum_type::Hit_Region],
   .list = NOT_A_LIST},
  {.name = "weapon",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Shot_Impact, weapon),
   .size_in_bytes = (uint32_t)sizeof(Shot_Impact::weapon),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = &entities::ENUM_INFOS[(uint32_t)entities::enum_type::Weapon],
   .list = NOT_A_LIST},
  {.name = "trigger",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Shot_Impact, trigger),
   .size_in_bytes = (uint32_t)sizeof(Shot_Impact::trigger),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = &entities::ENUM_INFOS[(uint32_t)entities::enum_type::Fire_Trigger],
   .list = NOT_A_LIST},
};

constexpr field_info_t CONSOLE_LINE_FIELDS[] = {
  {.name = "line",
   .type = FIELD_TYPE_STRING,
   .offset = (uint32_t)offsetof(Console_Line, line),
   .size_in_bytes = (uint32_t)sizeof(Console_Line::line),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = 255,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
};

constexpr field_info_t SERVER_TEXT_FIELDS[] = {
  {.name = "text",
   .type = FIELD_TYPE_STRING,
   .offset = (uint32_t)offsetof(Server_Text, text),
   .size_in_bytes = (uint32_t)sizeof(Server_Text::text),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = 255,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
};

constexpr field_info_t CONNECT_FIELDS[] = {
  {.name = "protocol_version",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Connect, protocol_version),
   .size_in_bytes = (uint32_t)sizeof(Connect::protocol_version),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "player_name",
   .type = FIELD_TYPE_STRING,
   .offset = (uint32_t)offsetof(Connect, player_name),
   .size_in_bytes = (uint32_t)sizeof(Connect::player_name),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = 32,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "schema_hash",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Connect, schema_hash),
   .size_in_bytes = (uint32_t)sizeof(Connect::schema_hash),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "asset_table_hash",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Connect, asset_table_hash),
   .size_in_bytes = (uint32_t)sizeof(Connect::asset_table_hash),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
};

constexpr field_info_t DISCONNECT_FIELDS[] = {
  {.name = "reason",
   .type = FIELD_TYPE_STRING,
   .offset = (uint32_t)offsetof(Disconnect, reason),
   .size_in_bytes = (uint32_t)sizeof(Disconnect::reason),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = 64,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
};

constexpr field_info_t ACCEPT_FIELDS[] = {
  {.name = "client_slot",
   .type = FIELD_TYPE_I32,
   .offset = (uint32_t)offsetof(Accept, client_slot),
   .size_in_bytes = (uint32_t)sizeof(Accept::client_slot),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "server_tickrate",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Accept, server_tickrate),
   .size_in_bytes = (uint32_t)sizeof(Accept::server_tickrate),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
};

constexpr field_info_t REJECT_FIELDS[] = {
  {.name = "reason",
   .type = FIELD_TYPE_STRING,
   .offset = (uint32_t)offsetof(Reject, reason),
   .size_in_bytes = (uint32_t)sizeof(Reject::reason),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = 255,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "server_schema_hash",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Reject, server_schema_hash),
   .size_in_bytes = (uint32_t)sizeof(Reject::server_schema_hash),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "server_asset_table_hash",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Reject, server_asset_table_hash),
   .size_in_bytes = (uint32_t)sizeof(Reject::server_asset_table_hash),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
};

constexpr field_info_t MAP_DATA_REQUEST_FIELDS[] = {
  {.name = "map_generation",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Map_Data_Request, map_generation),
   .size_in_bytes = (uint32_t)sizeof(Map_Data_Request::map_generation),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "map_name",
   .type = FIELD_TYPE_STRING,
   .offset = (uint32_t)offsetof(Map_Data_Request, map_name),
   .size_in_bytes = (uint32_t)sizeof(Map_Data_Request::map_name),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = 64,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
};

constexpr field_info_t GHOST_AVAILABLE_FIELDS[] = {
  {.name = "party_size",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Ghost_Available, party_size),
   .size_in_bytes = (uint32_t)sizeof(Ghost_Available::party_size),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "ghost_hash",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Ghost_Available, ghost_hash),
   .size_in_bytes = (uint32_t)sizeof(Ghost_Available::ghost_hash),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "byte_count",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Ghost_Available, byte_count),
   .size_in_bytes = (uint32_t)sizeof(Ghost_Available::byte_count),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
};

constexpr field_info_t GHOST_REQUEST_FIELDS[] = {
  {.name = "ghost_hash",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Ghost_Request, ghost_hash),
   .size_in_bytes = (uint32_t)sizeof(Ghost_Request::ghost_hash),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
};

constexpr field_info_t CLIENT_INPUT_FIELDS[] = {
  {.name = "input_number",
   .type = FIELD_TYPE_I32,
   .offset = (uint32_t)offsetof(Client_Input, input_number),
   .size_in_bytes = (uint32_t)sizeof(Client_Input::input_number),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "held_snapshot_tick",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Client_Input, held_snapshot_tick),
   .size_in_bytes = (uint32_t)sizeof(Client_Input::held_snapshot_tick),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "map_content_hash",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Client_Input, map_content_hash),
   .size_in_bytes = (uint32_t)sizeof(Client_Input::map_content_hash),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "interpolated_from_tick",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Client_Input, interpolated_from_tick),
   .size_in_bytes = (uint32_t)sizeof(Client_Input::interpolated_from_tick),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "interpolated_towards_tick",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Client_Input, interpolated_towards_tick),
   .size_in_bytes = (uint32_t)sizeof(Client_Input::interpolated_towards_tick),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "interpolation_fraction",
   .type = FIELD_TYPE_F32_EXACT,
   .offset = (uint32_t)offsetof(Client_Input, interpolation_fraction),
   .size_in_bytes = (uint32_t)sizeof(Client_Input::interpolation_fraction),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "view_fov_degrees",
   .type = FIELD_TYPE_F32_EXACT,
   .offset = (uint32_t)offsetof(Client_Input, view_fov_degrees),
   .size_in_bytes = (uint32_t)sizeof(Client_Input::view_fov_degrees),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "view_aspect",
   .type = FIELD_TYPE_F32_EXACT,
   .offset = (uint32_t)offsetof(Client_Input, view_aspect),
   .size_in_bytes = (uint32_t)sizeof(Client_Input::view_aspect),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
};

constexpr field_info_t ENTITY_PACKAGE_FIELDS[] = {
  {.name = "latest_processed_input_number",
   .type = FIELD_TYPE_I32,
   .offset = (uint32_t)offsetof(Entity_Package, latest_processed_input_number),
   .size_in_bytes = (uint32_t)sizeof(Entity_Package::latest_processed_input_number),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "delta_from_tick",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Entity_Package, delta_from_tick),
   .size_in_bytes = (uint32_t)sizeof(Entity_Package::delta_from_tick),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
};

constexpr field_info_t SHOT_DEBUG_FIELDS[] = {
  {.name = "input_number",
   .type = FIELD_TYPE_I32,
   .offset = (uint32_t)offsetof(Shot_Debug, input_number),
   .size_in_bytes = (uint32_t)sizeof(Shot_Debug::input_number),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "fire_subtick_slot",
   .type = FIELD_TYPE_U8,
   .offset = (uint32_t)offsetof(Shot_Debug, fire_subtick_slot),
   .size_in_bytes = (uint32_t)sizeof(Shot_Debug::fire_subtick_slot),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "eye",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Shot_Debug, eye),
   .size_in_bytes = (uint32_t)sizeof(Shot_Debug::eye),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "direction",
   .type = FIELD_TYPE_V3_EXACT,
   .offset = (uint32_t)offsetof(Shot_Debug, direction),
   .size_in_bytes = (uint32_t)sizeof(Shot_Debug::direction),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "bracket_status",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Shot_Debug, bracket_status),
   .size_in_bytes = (uint32_t)sizeof(Shot_Debug::bracket_status),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = &ENUM_INFOS[0],
   .list = NOT_A_LIST},
  {.name = "used_rewind",
   .type = FIELD_TYPE_BOOL,
   .offset = (uint32_t)offsetof(Shot_Debug, used_rewind),
   .size_in_bytes = (uint32_t)sizeof(Shot_Debug::used_rewind),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "requested_from_tick",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Shot_Debug, requested_from_tick),
   .size_in_bytes = (uint32_t)sizeof(Shot_Debug::requested_from_tick),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "requested_towards_tick",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Shot_Debug, requested_towards_tick),
   .size_in_bytes = (uint32_t)sizeof(Shot_Debug::requested_towards_tick),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "requested_fraction",
   .type = FIELD_TYPE_F32_EXACT,
   .offset = (uint32_t)offsetof(Shot_Debug, requested_fraction),
   .size_in_bytes = (uint32_t)sizeof(Shot_Debug::requested_fraction),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "used_from_tick",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Shot_Debug, used_from_tick),
   .size_in_bytes = (uint32_t)sizeof(Shot_Debug::used_from_tick),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "used_towards_tick",
   .type = FIELD_TYPE_U32,
   .offset = (uint32_t)offsetof(Shot_Debug, used_towards_tick),
   .size_in_bytes = (uint32_t)sizeof(Shot_Debug::used_towards_tick),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "used_fraction",
   .type = FIELD_TYPE_F32_EXACT,
   .offset = (uint32_t)offsetof(Shot_Debug, used_fraction),
   .size_in_bytes = (uint32_t)sizeof(Shot_Debug::used_fraction),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "hit_uid",
   .type = FIELD_TYPE_ENTITY_UID,
   .offset = (uint32_t)offsetof(Shot_Debug, hit_uid),
   .size_in_bytes = (uint32_t)sizeof(Shot_Debug::hit_uid),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "hit_region",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Shot_Debug, hit_region),
   .size_in_bytes = (uint32_t)sizeof(Shot_Debug::hit_region),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = &entities::ENUM_INFOS[(uint32_t)entities::enum_type::Hit_Region],
   .list = NOT_A_LIST},
  {.name = "impact_point",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Shot_Debug, impact_point),
   .size_in_bytes = (uint32_t)sizeof(Shot_Debug::impact_point),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
  {.name = "nearest_miss_distance",
   .type = FIELD_TYPE_F32,
   .offset = (uint32_t)offsetof(Shot_Debug, nearest_miss_distance),
   .size_in_bytes = (uint32_t)sizeof(Shot_Debug::nearest_miss_distance),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM,
   .list = NOT_A_LIST},
};

std::string leaf_text(const void* base, const field_info_t& field)
{
  std::string text;
  return field_to_text(static_cast<const uint8_t*>(base) + field.offset, field, text)
             ? text
             : "<unprintable>";
}

} // namespace

const char* to_string(Bracket_Status value)
{
  switch (value)
  {
    case Bracket_Status::Ok: return "Ok";
    case Bracket_Status::Clamped: return "Clamped";
    case Bracket_Status::Absent: return "Absent";
    case Bracket_Status::Malformed: return "Malformed";
    case Bracket_Status::Unheld: return "Unheld";
  }
  assert(false && "invalid Bracket_Status");
  return "";
}

template <> std::optional<Bracket_Status> try_from_string<Bracket_Status>(std::string_view text)
{
  if (text == "Ok") return Bracket_Status::Ok;
  if (text == "Clamped") return Bracket_Status::Clamped;
  if (text == "Absent") return Bracket_Status::Absent;
  if (text == "Malformed") return Bracket_Status::Malformed;
  if (text == "Unheld") return Bracket_Status::Unheld;
  return std::nullopt;
}

const char* to_string(Bot_Goal value)
{
  switch (value)
  {
    case Bot_Goal::Idle: return "Idle";
    case Bot_Goal::Chase: return "Chase";
    case Bot_Goal::Attack: return "Attack";
    case Bot_Goal::Retreat: return "Retreat";
  }
  assert(false && "invalid Bot_Goal");
  return "";
}

template <> std::optional<Bot_Goal> try_from_string<Bot_Goal>(std::string_view text)
{
  if (text == "Idle") return Bot_Goal::Idle;
  if (text == "Chase") return Bot_Goal::Chase;
  if (text == "Attack") return Bot_Goal::Attack;
  if (text == "Retreat") return Bot_Goal::Retreat;
  return std::nullopt;
}

const char* to_string(Bot_Behavior value)
{
  switch (value)
  {
    case Bot_Behavior::Idle: return "Idle";
    case Bot_Behavior::Chase: return "Chase";
    case Bot_Behavior::Regular: return "Regular";
  }
  assert(false && "invalid Bot_Behavior");
  return "";
}

template <> std::optional<Bot_Behavior> try_from_string<Bot_Behavior>(std::string_view text)
{
  if (text == "Idle") return Bot_Behavior::Idle;
  if (text == "Chase") return Bot_Behavior::Chase;
  if (text == "Regular") return Bot_Behavior::Regular;
  return std::nullopt;
}

void write_effect_site(network::Bit_Writer& writer, const Effect_Site& value)
{
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), EFFECT_SITE_FIELDS[0], EFFECT_SITE_FIELDS[0].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), EFFECT_SITE_FIELDS[1], EFFECT_SITE_FIELDS[1].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), EFFECT_SITE_FIELDS[2], EFFECT_SITE_FIELDS[2].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), EFFECT_SITE_FIELDS[3], EFFECT_SITE_FIELDS[3].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), EFFECT_SITE_FIELDS[4], EFFECT_SITE_FIELDS[4].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), EFFECT_SITE_FIELDS[5], EFFECT_SITE_FIELDS[5].offset, nullptr);
}

std::optional<Effect_Site> try_read_effect_site(network::Bit_Reader& reader)
{
  Effect_Site value;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), EFFECT_SITE_FIELDS[0], EFFECT_SITE_FIELDS[0].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), EFFECT_SITE_FIELDS[1], EFFECT_SITE_FIELDS[1].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), EFFECT_SITE_FIELDS[2], EFFECT_SITE_FIELDS[2].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), EFFECT_SITE_FIELDS[3], EFFECT_SITE_FIELDS[3].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), EFFECT_SITE_FIELDS[4], EFFECT_SITE_FIELDS[4].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), EFFECT_SITE_FIELDS[5], EFFECT_SITE_FIELDS[5].offset))
    return std::nullopt;
  return value;
}

std::string to_text(const Effect_Site& value)
{
  std::string text = "{ ";
  text += "origin=";
  text += leaf_text(&value, EFFECT_SITE_FIELDS[0]);
  text += ", normal=";
  text += leaf_text(&value, EFFECT_SITE_FIELDS[1]);
  text += ", color=";
  text += leaf_text(&value, EFFECT_SITE_FIELDS[2]);
  text += ", scale=";
  text += leaf_text(&value, EFFECT_SITE_FIELDS[3]);
  text += ", attached_entity=";
  text += leaf_text(&value, EFFECT_SITE_FIELDS[4]);
  text += ", surface_material=";
  text += leaf_text(&value, EFFECT_SITE_FIELDS[5]);
  text += " }";
  return text;
}

void write_server_map(network::Bit_Writer& writer, const Server_Map& value)
{
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SERVER_MAP_FIELDS[0], SERVER_MAP_FIELDS[0].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SERVER_MAP_FIELDS[1], SERVER_MAP_FIELDS[1].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SERVER_MAP_FIELDS[2], SERVER_MAP_FIELDS[2].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SERVER_MAP_FIELDS[3], SERVER_MAP_FIELDS[3].offset, nullptr);
}

std::optional<Server_Map> try_read_server_map(network::Bit_Reader& reader)
{
  Server_Map value;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SERVER_MAP_FIELDS[0], SERVER_MAP_FIELDS[0].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SERVER_MAP_FIELDS[1], SERVER_MAP_FIELDS[1].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SERVER_MAP_FIELDS[2], SERVER_MAP_FIELDS[2].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SERVER_MAP_FIELDS[3], SERVER_MAP_FIELDS[3].offset))
    return std::nullopt;
  return value;
}

std::string to_text(const Server_Map& value)
{
  std::string text = "{ ";
  text += "generation=";
  text += leaf_text(&value, SERVER_MAP_FIELDS[0]);
  text += ", path=";
  text += leaf_text(&value, SERVER_MAP_FIELDS[1]);
  text += ", name=";
  text += leaf_text(&value, SERVER_MAP_FIELDS[2]);
  text += ", content_hash=";
  text += leaf_text(&value, SERVER_MAP_FIELDS[3]);
  text += " }";
  return text;
}

void write_cvar_value(network::Bit_Writer& writer, const Cvar_Value& value)
{
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), CVAR_VALUE_FIELDS[0], CVAR_VALUE_FIELDS[0].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), CVAR_VALUE_FIELDS[1], CVAR_VALUE_FIELDS[1].offset, nullptr);
}

std::optional<Cvar_Value> try_read_cvar_value(network::Bit_Reader& reader)
{
  Cvar_Value value;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), CVAR_VALUE_FIELDS[0], CVAR_VALUE_FIELDS[0].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), CVAR_VALUE_FIELDS[1], CVAR_VALUE_FIELDS[1].offset))
    return std::nullopt;
  return value;
}

std::string to_text(const Cvar_Value& value)
{
  std::string text = "{ ";
  text += "id=";
  text += leaf_text(&value, CVAR_VALUE_FIELDS[0]);
  text += ", text=";
  text += leaf_text(&value, CVAR_VALUE_FIELDS[1]);
  text += " }";
  return text;
}

void write_view(network::Bit_Writer& writer, const View& value)
{
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), VIEW_FIELDS[0], VIEW_FIELDS[0].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), VIEW_FIELDS[1], VIEW_FIELDS[1].offset, nullptr);
}

std::optional<View> try_read_view(network::Bit_Reader& reader)
{
  View value;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), VIEW_FIELDS[0], VIEW_FIELDS[0].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), VIEW_FIELDS[1], VIEW_FIELDS[1].offset))
    return std::nullopt;
  return value;
}

std::string to_text(const View& value)
{
  std::string text = "{ ";
  text += "yaw=";
  text += leaf_text(&value, VIEW_FIELDS[0]);
  text += ", pitch=";
  text += leaf_text(&value, VIEW_FIELDS[1]);
  text += " }";
  return text;
}

void write_button_change(network::Bit_Writer& writer, const Button_Change& value)
{
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), BUTTON_CHANGE_FIELDS[0], BUTTON_CHANGE_FIELDS[0].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), BUTTON_CHANGE_FIELDS[1], BUTTON_CHANGE_FIELDS[1].offset, nullptr);
  write_view(writer, value.view_after);
}

std::optional<Button_Change> try_read_button_change(network::Bit_Reader& reader)
{
  Button_Change value;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), BUTTON_CHANGE_FIELDS[0], BUTTON_CHANGE_FIELDS[0].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), BUTTON_CHANGE_FIELDS[1], BUTTON_CHANGE_FIELDS[1].offset))
    return std::nullopt;
  {
    const std::optional<View> field = try_read_view(reader);
    if (!field)
      return std::nullopt;
    value.view_after = *field;
  }
  return value;
}

std::string to_text(const Button_Change& value)
{
  std::string text = "{ ";
  text += "slot=";
  text += leaf_text(&value, BUTTON_CHANGE_FIELDS[0]);
  text += ", buttons_after=";
  text += leaf_text(&value, BUTTON_CHANGE_FIELDS[1]);
  text += ", view_after=";
  text += to_text(value.view_after);
  text += " }";
  return text;
}

void write_tick_input(network::Bit_Writer& writer, const Tick_Input& value)
{
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), TICK_INPUT_FIELDS[0], TICK_INPUT_FIELDS[0].offset, nullptr);
  write_view(writer, value.view_at_start);
  write_view(writer, value.view_at_end);
  network::write_var_uint(writer, value.button_changes.count);
  for (uint32_t index = 0; index < value.button_changes.count; ++index)
    write_button_change(writer, value.button_changes.elements[index]);
}

std::optional<Tick_Input> try_read_tick_input(network::Bit_Reader& reader)
{
  Tick_Input value;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), TICK_INPUT_FIELDS[0], TICK_INPUT_FIELDS[0].offset))
    return std::nullopt;
  {
    const std::optional<View> field = try_read_view(reader);
    if (!field)
      return std::nullopt;
    value.view_at_start = *field;
  }
  {
    const std::optional<View> field = try_read_view(reader);
    if (!field)
      return std::nullopt;
    value.view_at_end = *field;
  }
  {
    const uint32_t count = network::read_var_uint(reader);
    if (count > 8)
    {
      log_error("Tick_Input: 'button_changes' carries {} elements, at most 8 fit", count);
      return std::nullopt;
    }
    value.button_changes.count = count;
    for (uint32_t index = 0; index < count; ++index)
    {
      const std::optional<Button_Change> element = try_read_button_change(reader);
      if (!element)
        return std::nullopt;
      value.button_changes.elements[index] = *element;
    }
  }
  return value;
}

std::string to_text(const Tick_Input& value)
{
  std::string text = "{ ";
  text += "buttons_at_start=";
  text += leaf_text(&value, TICK_INPUT_FIELDS[0]);
  text += ", view_at_start=";
  text += to_text(value.view_at_start);
  text += ", view_at_end=";
  text += to_text(value.view_at_end);
  text += ", button_changes=[";
  for (uint32_t index = 0; index < value.button_changes.count; ++index)
  {
    if (index > 0)
      text += ", ";
    text += to_text(value.button_changes.elements[index]);
  }
  text += "]";
  text += " }";
  return text;
}

void write_shot_debug_target(network::Bit_Writer& writer, const Shot_Debug_Target& value)
{
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SHOT_DEBUG_TARGET_FIELDS[0], SHOT_DEBUG_TARGET_FIELDS[0].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SHOT_DEBUG_TARGET_FIELDS[1], SHOT_DEBUG_TARGET_FIELDS[1].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SHOT_DEBUG_TARGET_FIELDS[2], SHOT_DEBUG_TARGET_FIELDS[2].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SHOT_DEBUG_TARGET_FIELDS[3], SHOT_DEBUG_TARGET_FIELDS[3].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SHOT_DEBUG_TARGET_FIELDS[4], SHOT_DEBUG_TARGET_FIELDS[4].offset, nullptr);
}

std::optional<Shot_Debug_Target> try_read_shot_debug_target(network::Bit_Reader& reader)
{
  Shot_Debug_Target value;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SHOT_DEBUG_TARGET_FIELDS[0], SHOT_DEBUG_TARGET_FIELDS[0].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SHOT_DEBUG_TARGET_FIELDS[1], SHOT_DEBUG_TARGET_FIELDS[1].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SHOT_DEBUG_TARGET_FIELDS[2], SHOT_DEBUG_TARGET_FIELDS[2].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SHOT_DEBUG_TARGET_FIELDS[3], SHOT_DEBUG_TARGET_FIELDS[3].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SHOT_DEBUG_TARGET_FIELDS[4], SHOT_DEBUG_TARGET_FIELDS[4].offset))
    return std::nullopt;
  return value;
}

std::string to_text(const Shot_Debug_Target& value)
{
  std::string text = "{ ";
  text += "player_uid=";
  text += leaf_text(&value, SHOT_DEBUG_TARGET_FIELDS[0]);
  text += ", feet_position=";
  text += leaf_text(&value, SHOT_DEBUG_TARGET_FIELDS[1]);
  text += ", body_yaw=";
  text += leaf_text(&value, SHOT_DEBUG_TARGET_FIELDS[2]);
  text += ", view_yaw=";
  text += leaf_text(&value, SHOT_DEBUG_TARGET_FIELDS[3]);
  text += ", view_pitch=";
  text += leaf_text(&value, SHOT_DEBUG_TARGET_FIELDS[4]);
  text += " }";
  return text;
}

void write_bot_debug_entry(network::Bit_Writer& writer, const Bot_Debug_Entry& value)
{
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), BOT_DEBUG_ENTRY_FIELDS[0], BOT_DEBUG_ENTRY_FIELDS[0].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), BOT_DEBUG_ENTRY_FIELDS[1], BOT_DEBUG_ENTRY_FIELDS[1].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), BOT_DEBUG_ENTRY_FIELDS[2], BOT_DEBUG_ENTRY_FIELDS[2].offset, nullptr);
  network::write_var_uint(writer, value.path.count);
  for (uint32_t index = 0; index < value.path.count; ++index)
    network::write_field(writer, reinterpret_cast<const uint8_t*>(&value.path.elements[index]), BOT_DEBUG_ENTRY_PATH_ELEMENT[0], 0, nullptr);
}

std::optional<Bot_Debug_Entry> try_read_bot_debug_entry(network::Bit_Reader& reader)
{
  Bot_Debug_Entry value;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), BOT_DEBUG_ENTRY_FIELDS[0], BOT_DEBUG_ENTRY_FIELDS[0].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), BOT_DEBUG_ENTRY_FIELDS[1], BOT_DEBUG_ENTRY_FIELDS[1].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), BOT_DEBUG_ENTRY_FIELDS[2], BOT_DEBUG_ENTRY_FIELDS[2].offset))
    return std::nullopt;
  {
    const uint32_t count = network::read_var_uint(reader);
    if (count > 32)
    {
      log_error("Bot_Debug_Entry: 'path' carries {} elements, at most 32 fit", count);
      return std::nullopt;
    }
    value.path.count = count;
    for (uint32_t index = 0; index < count; ++index)
      if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value.path.elements[index]), BOT_DEBUG_ENTRY_PATH_ELEMENT[0], 0))
        return std::nullopt;
  }
  return value;
}

std::string to_text(const Bot_Debug_Entry& value)
{
  std::string text = "{ ";
  text += "slot=";
  text += leaf_text(&value, BOT_DEBUG_ENTRY_FIELDS[0]);
  text += ", goal=";
  text += leaf_text(&value, BOT_DEBUG_ENTRY_FIELDS[1]);
  text += ", behavior=";
  text += leaf_text(&value, BOT_DEBUG_ENTRY_FIELDS[2]);
  text += ", path=[";
  for (uint32_t index = 0; index < value.path.count; ++index)
  {
    if (index > 0)
      text += ", ";
    text += leaf_text(&value.path.elements[index], BOT_DEBUG_ENTRY_PATH_ELEMENT[0]);
  }
  text += "]";
  text += " }";
  return text;
}

const char* to_string(Message_Type value)
{
  switch (value)
  {
    case Message_Type::Rocket_Detonated: return "Rocket_Detonated";
    case Message_Type::Player_Died: return "Player_Died";
    case Message_Type::Player_Spawned: return "Player_Spawned";
    case Message_Type::Objective_Reached: return "Objective_Reached";
    case Message_Type::Rocket_Explosion: return "Rocket_Explosion";
    case Message_Type::Footstep: return "Footstep";
    case Message_Type::Jump: return "Jump";
    case Message_Type::Land: return "Land";
    case Message_Type::Shot_Impact: return "Shot_Impact";
    case Message_Type::Jump_Pad_Launch: return "Jump_Pad_Launch";
    case Message_Type::Ping: return "Ping";
    case Message_Type::Pilot_Impact: return "Pilot_Impact";
    case Message_Type::Console_Line: return "Console_Line";
    case Message_Type::Server_Text: return "Server_Text";
    case Message_Type::Connect: return "Connect";
    case Message_Type::Disconnect: return "Disconnect";
    case Message_Type::Accept: return "Accept";
    case Message_Type::Reject: return "Reject";
    case Message_Type::Change_Map: return "Change_Map";
    case Message_Type::Cvar_Values: return "Cvar_Values";
    case Message_Type::Map_Data_Request: return "Map_Data_Request";
    case Message_Type::Ghost_Available: return "Ghost_Available";
    case Message_Type::Ghost_Request: return "Ghost_Request";
    case Message_Type::Client_Input: return "Client_Input";
    case Message_Type::Entity_Package: return "Entity_Package";
    case Message_Type::Shot_Debug: return "Shot_Debug";
    case Message_Type::Bot_Debug: return "Bot_Debug";
  }
  assert(false && "invalid Message_Type");
  return "";
}

const char* description(Message_Type value)
{
  switch (value)
  {
    case Message_Type::Rocket_Detonated: return "a rocket detonated, by impact or lifetime expiry";
    case Message_Type::Player_Died: return "a player's health crossed from >0 to <=0";
    case Message_Type::Player_Spawned: return "a player entered the world at a spawn point";
    case Message_Type::Objective_Reached: return "the level's objective was completed";
    case Message_Type::Rocket_Explosion: return "splash particles + surface decal";
    case Message_Type::Footstep: return "one foot planting";
    case Message_Type::Jump: return "leaving the ground";
    case Message_Type::Land: return "arriving back on it";
    case Message_Type::Shot_Impact: return "a shot landed; attached_entity is what it hit, 0 for the world";
    case Message_Type::Jump_Pad_Launch: return "a pad threw somebody; normal is the launch direction, attached_entity who";
    case Message_Type::Ping: return "a player pinged a spot; attached_entity is who pinged, normal the surface";
    case Message_Type::Pilot_Impact: return "a piloted rocket ended on a surface; origin the contact, normal the surface, attached_entity the pilot";
    case Message_Type::Console_Line: return "a console line typed at the client, run by the server as its caller";
    case Message_Type::Server_Text: return "a line for the client's console";
    case Message_Type::Connect: return "a peer asking for a slot";
    case Message_Type::Disconnect: return "a client leaving on purpose";
    case Message_Type::Accept: return "the answer to a Connect that got a slot";
    case Message_Type::Reject: return "the answer to a Connect that did not";
    case Message_Type::Change_Map: return "the server switched map; follow it";
    case Message_Type::Cvar_Values: return "@Mirrored values: the whole set at connect, the changed subset after";
    case Message_Type::Map_Data_Request: return "the client lacks the compiled package of the map at this generation; stream it";
    case Message_Type::Ghost_Available: return "the ghost for the party being run, by hash; 0 is none";
    case Message_Type::Ghost_Request: return "the client's cached ghost did not hash to the announced one";
    case Message_Type::Client_Input: return "one tick of input plus what the client HOLDS, restated every tick: the snapshot, the map, the screen";
    case Message_Type::Entity_Package: return "the snapshot at the datagram's tick: a delta against the tick the client said it holds, or a full update";
    case Message_Type::Shot_Debug: return "the server's view of one of the shooter's own shots, to the shooter alone (sv_shot_debug)";
    case Message_Type::Bot_Debug: return "every bot's goal and the path ahead of it, each tick while bots exist; more than the bound is several rows";
  }
  assert(false && "invalid Message_Type");
  return "";
}

void write_rocket_detonated(reliable_stream_t& reliable, const Rocket_Detonated& value)
{
  message_stream_t& stream = reliable.records;
  network::Bit_Writer& writer = stream.writer;
  stream.begin_record((uint32_t)Message_Type::Rocket_Detonated);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), ROCKET_DETONATED_FIELDS[0], ROCKET_DETONATED_FIELDS[0].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), ROCKET_DETONATED_FIELDS[1], ROCKET_DETONATED_FIELDS[1].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), ROCKET_DETONATED_FIELDS[2], ROCKET_DETONATED_FIELDS[2].offset, nullptr);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[message written] {}", to_text(value));
}

std::optional<Rocket_Detonated> try_read_rocket_detonated(network::Bit_Reader& reader)
{
  Rocket_Detonated value;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), ROCKET_DETONATED_FIELDS[0], ROCKET_DETONATED_FIELDS[0].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), ROCKET_DETONATED_FIELDS[1], ROCKET_DETONATED_FIELDS[1].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), ROCKET_DETONATED_FIELDS[2], ROCKET_DETONATED_FIELDS[2].offset))
    return std::nullopt;
  return value;
}

std::string to_text(const Rocket_Detonated& value)
{
  std::string text = "Rocket_Detonated{ ";
  text += "attacker_id=";
  text += leaf_text(&value, ROCKET_DETONATED_FIELDS[0]);
  text += ", victim_id=";
  text += leaf_text(&value, ROCKET_DETONATED_FIELDS[1]);
  text += ", weapon_id=";
  text += leaf_text(&value, ROCKET_DETONATED_FIELDS[2]);
  text += " }";
  return text;
}

void write_player_died(reliable_stream_t& reliable, const Player_Died& value)
{
  message_stream_t& stream = reliable.records;
  network::Bit_Writer& writer = stream.writer;
  stream.begin_record((uint32_t)Message_Type::Player_Died);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), PLAYER_DIED_FIELDS[0], PLAYER_DIED_FIELDS[0].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), PLAYER_DIED_FIELDS[1], PLAYER_DIED_FIELDS[1].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), PLAYER_DIED_FIELDS[2], PLAYER_DIED_FIELDS[2].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), PLAYER_DIED_FIELDS[3], PLAYER_DIED_FIELDS[3].offset, nullptr);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[message written] {}", to_text(value));
}

std::optional<Player_Died> try_read_player_died(network::Bit_Reader& reader)
{
  Player_Died value;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), PLAYER_DIED_FIELDS[0], PLAYER_DIED_FIELDS[0].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), PLAYER_DIED_FIELDS[1], PLAYER_DIED_FIELDS[1].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), PLAYER_DIED_FIELDS[2], PLAYER_DIED_FIELDS[2].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), PLAYER_DIED_FIELDS[3], PLAYER_DIED_FIELDS[3].offset))
    return std::nullopt;
  return value;
}

std::string to_text(const Player_Died& value)
{
  std::string text = "Player_Died{ ";
  text += "victim_id=";
  text += leaf_text(&value, PLAYER_DIED_FIELDS[0]);
  text += ", attacker_id=";
  text += leaf_text(&value, PLAYER_DIED_FIELDS[1]);
  text += ", weapon_id=";
  text += leaf_text(&value, PLAYER_DIED_FIELDS[2]);
  text += ", was_headshot=";
  text += leaf_text(&value, PLAYER_DIED_FIELDS[3]);
  text += " }";
  return text;
}

void write_player_spawned(reliable_stream_t& reliable, const Player_Spawned& value)
{
  message_stream_t& stream = reliable.records;
  network::Bit_Writer& writer = stream.writer;
  stream.begin_record((uint32_t)Message_Type::Player_Spawned);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), PLAYER_SPAWNED_FIELDS[0], PLAYER_SPAWNED_FIELDS[0].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), PLAYER_SPAWNED_FIELDS[1], PLAYER_SPAWNED_FIELDS[1].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), PLAYER_SPAWNED_FIELDS[2], PLAYER_SPAWNED_FIELDS[2].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), PLAYER_SPAWNED_FIELDS[3], PLAYER_SPAWNED_FIELDS[3].offset, nullptr);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[message written] {}", to_text(value));
}

std::optional<Player_Spawned> try_read_player_spawned(network::Bit_Reader& reader)
{
  Player_Spawned value;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), PLAYER_SPAWNED_FIELDS[0], PLAYER_SPAWNED_FIELDS[0].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), PLAYER_SPAWNED_FIELDS[1], PLAYER_SPAWNED_FIELDS[1].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), PLAYER_SPAWNED_FIELDS[2], PLAYER_SPAWNED_FIELDS[2].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), PLAYER_SPAWNED_FIELDS[3], PLAYER_SPAWNED_FIELDS[3].offset))
    return std::nullopt;
  return value;
}

std::string to_text(const Player_Spawned& value)
{
  std::string text = "Player_Spawned{ ";
  text += "player_id=";
  text += leaf_text(&value, PLAYER_SPAWNED_FIELDS[0]);
  text += ", client_slot=";
  text += leaf_text(&value, PLAYER_SPAWNED_FIELDS[1]);
  text += ", spawn_position=";
  text += leaf_text(&value, PLAYER_SPAWNED_FIELDS[2]);
  text += ", spawn_orientation=";
  text += leaf_text(&value, PLAYER_SPAWNED_FIELDS[3]);
  text += " }";
  return text;
}

void write_objective_reached(reliable_stream_t& reliable, const Objective_Reached& value)
{
  message_stream_t& stream = reliable.records;
  network::Bit_Writer& writer = stream.writer;
  stream.begin_record((uint32_t)Message_Type::Objective_Reached);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), OBJECTIVE_REACHED_FIELDS[0], OBJECTIVE_REACHED_FIELDS[0].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), OBJECTIVE_REACHED_FIELDS[1], OBJECTIVE_REACHED_FIELDS[1].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), OBJECTIVE_REACHED_FIELDS[2], OBJECTIVE_REACHED_FIELDS[2].offset, nullptr);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[message written] {}", to_text(value));
}

std::optional<Objective_Reached> try_read_objective_reached(network::Bit_Reader& reader)
{
  Objective_Reached value;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), OBJECTIVE_REACHED_FIELDS[0], OBJECTIVE_REACHED_FIELDS[0].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), OBJECTIVE_REACHED_FIELDS[1], OBJECTIVE_REACHED_FIELDS[1].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), OBJECTIVE_REACHED_FIELDS[2], OBJECTIVE_REACHED_FIELDS[2].offset))
    return std::nullopt;
  return value;
}

std::string to_text(const Objective_Reached& value)
{
  std::string text = "Objective_Reached{ ";
  text += "completed_by=";
  text += leaf_text(&value, OBJECTIVE_REACHED_FIELDS[0]);
  text += ", attempt_ticks=";
  text += leaf_text(&value, OBJECTIVE_REACHED_FIELDS[1]);
  text += ", best_ticks=";
  text += leaf_text(&value, OBJECTIVE_REACHED_FIELDS[2]);
  text += " }";
  return text;
}

void write_rocket_explosion(message_stream_t& stream, const Rocket_Explosion& value)
{
  network::Bit_Writer& writer = stream.writer;
  stream.begin_record((uint32_t)Message_Type::Rocket_Explosion);
  write_effect_site(writer, value.site);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[message written] {}", to_text(value));
}

std::optional<Rocket_Explosion> try_read_rocket_explosion(network::Bit_Reader& reader)
{
  Rocket_Explosion value;
  {
    const std::optional<Effect_Site> field = try_read_effect_site(reader);
    if (!field)
      return std::nullopt;
    value.site = *field;
  }
  return value;
}

std::string to_text(const Rocket_Explosion& value)
{
  std::string text = "Rocket_Explosion{ ";
  text += "site=";
  text += to_text(value.site);
  text += " }";
  return text;
}

void write_footstep(message_stream_t& stream, const Footstep& value)
{
  network::Bit_Writer& writer = stream.writer;
  stream.begin_record((uint32_t)Message_Type::Footstep);
  write_effect_site(writer, value.site);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[message written] {}", to_text(value));
}

std::optional<Footstep> try_read_footstep(network::Bit_Reader& reader)
{
  Footstep value;
  {
    const std::optional<Effect_Site> field = try_read_effect_site(reader);
    if (!field)
      return std::nullopt;
    value.site = *field;
  }
  return value;
}

std::string to_text(const Footstep& value)
{
  std::string text = "Footstep{ ";
  text += "site=";
  text += to_text(value.site);
  text += " }";
  return text;
}

void write_jump(message_stream_t& stream, const Jump& value)
{
  network::Bit_Writer& writer = stream.writer;
  stream.begin_record((uint32_t)Message_Type::Jump);
  write_effect_site(writer, value.site);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[message written] {}", to_text(value));
}

std::optional<Jump> try_read_jump(network::Bit_Reader& reader)
{
  Jump value;
  {
    const std::optional<Effect_Site> field = try_read_effect_site(reader);
    if (!field)
      return std::nullopt;
    value.site = *field;
  }
  return value;
}

std::string to_text(const Jump& value)
{
  std::string text = "Jump{ ";
  text += "site=";
  text += to_text(value.site);
  text += " }";
  return text;
}

void write_land(message_stream_t& stream, const Land& value)
{
  network::Bit_Writer& writer = stream.writer;
  stream.begin_record((uint32_t)Message_Type::Land);
  write_effect_site(writer, value.site);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[message written] {}", to_text(value));
}

std::optional<Land> try_read_land(network::Bit_Reader& reader)
{
  Land value;
  {
    const std::optional<Effect_Site> field = try_read_effect_site(reader);
    if (!field)
      return std::nullopt;
    value.site = *field;
  }
  return value;
}

std::string to_text(const Land& value)
{
  std::string text = "Land{ ";
  text += "site=";
  text += to_text(value.site);
  text += " }";
  return text;
}

void write_shot_impact(message_stream_t& stream, const Shot_Impact& value)
{
  network::Bit_Writer& writer = stream.writer;
  stream.begin_record((uint32_t)Message_Type::Shot_Impact);
  write_effect_site(writer, value.site);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SHOT_IMPACT_FIELDS[0], SHOT_IMPACT_FIELDS[0].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SHOT_IMPACT_FIELDS[1], SHOT_IMPACT_FIELDS[1].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SHOT_IMPACT_FIELDS[2], SHOT_IMPACT_FIELDS[2].offset, nullptr);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[message written] {}", to_text(value));
}

std::optional<Shot_Impact> try_read_shot_impact(network::Bit_Reader& reader)
{
  Shot_Impact value;
  {
    const std::optional<Effect_Site> field = try_read_effect_site(reader);
    if (!field)
      return std::nullopt;
    value.site = *field;
  }
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SHOT_IMPACT_FIELDS[0], SHOT_IMPACT_FIELDS[0].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SHOT_IMPACT_FIELDS[1], SHOT_IMPACT_FIELDS[1].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SHOT_IMPACT_FIELDS[2], SHOT_IMPACT_FIELDS[2].offset))
    return std::nullopt;
  return value;
}

std::string to_text(const Shot_Impact& value)
{
  std::string text = "Shot_Impact{ ";
  text += "site=";
  text += to_text(value.site);
  text += ", region=";
  text += leaf_text(&value, SHOT_IMPACT_FIELDS[0]);
  text += ", weapon=";
  text += leaf_text(&value, SHOT_IMPACT_FIELDS[1]);
  text += ", trigger=";
  text += leaf_text(&value, SHOT_IMPACT_FIELDS[2]);
  text += " }";
  return text;
}

void write_jump_pad_launch(message_stream_t& stream, const Jump_Pad_Launch& value)
{
  network::Bit_Writer& writer = stream.writer;
  stream.begin_record((uint32_t)Message_Type::Jump_Pad_Launch);
  write_effect_site(writer, value.site);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[message written] {}", to_text(value));
}

std::optional<Jump_Pad_Launch> try_read_jump_pad_launch(network::Bit_Reader& reader)
{
  Jump_Pad_Launch value;
  {
    const std::optional<Effect_Site> field = try_read_effect_site(reader);
    if (!field)
      return std::nullopt;
    value.site = *field;
  }
  return value;
}

std::string to_text(const Jump_Pad_Launch& value)
{
  std::string text = "Jump_Pad_Launch{ ";
  text += "site=";
  text += to_text(value.site);
  text += " }";
  return text;
}

void write_ping(message_stream_t& stream, const Ping& value)
{
  network::Bit_Writer& writer = stream.writer;
  stream.begin_record((uint32_t)Message_Type::Ping);
  write_effect_site(writer, value.site);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[message written] {}", to_text(value));
}

std::optional<Ping> try_read_ping(network::Bit_Reader& reader)
{
  Ping value;
  {
    const std::optional<Effect_Site> field = try_read_effect_site(reader);
    if (!field)
      return std::nullopt;
    value.site = *field;
  }
  return value;
}

std::string to_text(const Ping& value)
{
  std::string text = "Ping{ ";
  text += "site=";
  text += to_text(value.site);
  text += " }";
  return text;
}

void write_pilot_impact(message_stream_t& stream, const Pilot_Impact& value)
{
  network::Bit_Writer& writer = stream.writer;
  stream.begin_record((uint32_t)Message_Type::Pilot_Impact);
  write_effect_site(writer, value.site);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[message written] {}", to_text(value));
}

std::optional<Pilot_Impact> try_read_pilot_impact(network::Bit_Reader& reader)
{
  Pilot_Impact value;
  {
    const std::optional<Effect_Site> field = try_read_effect_site(reader);
    if (!field)
      return std::nullopt;
    value.site = *field;
  }
  return value;
}

std::string to_text(const Pilot_Impact& value)
{
  std::string text = "Pilot_Impact{ ";
  text += "site=";
  text += to_text(value.site);
  text += " }";
  return text;
}

void write_console_line(reliable_stream_t& reliable, const Console_Line& value)
{
  message_stream_t& stream = reliable.records;
  network::Bit_Writer& writer = stream.writer;
  stream.begin_record((uint32_t)Message_Type::Console_Line);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), CONSOLE_LINE_FIELDS[0], CONSOLE_LINE_FIELDS[0].offset, nullptr);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[message written] {}", to_text(value));
}

std::optional<Console_Line> try_read_console_line(network::Bit_Reader& reader)
{
  Console_Line value;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), CONSOLE_LINE_FIELDS[0], CONSOLE_LINE_FIELDS[0].offset))
    return std::nullopt;
  return value;
}

std::string to_text(const Console_Line& value)
{
  std::string text = "Console_Line{ ";
  text += "line=";
  text += leaf_text(&value, CONSOLE_LINE_FIELDS[0]);
  text += " }";
  return text;
}

void write_server_text(reliable_stream_t& reliable, const Server_Text& value)
{
  message_stream_t& stream = reliable.records;
  network::Bit_Writer& writer = stream.writer;
  stream.begin_record((uint32_t)Message_Type::Server_Text);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SERVER_TEXT_FIELDS[0], SERVER_TEXT_FIELDS[0].offset, nullptr);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[message written] {}", to_text(value));
}

std::optional<Server_Text> try_read_server_text(network::Bit_Reader& reader)
{
  Server_Text value;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SERVER_TEXT_FIELDS[0], SERVER_TEXT_FIELDS[0].offset))
    return std::nullopt;
  return value;
}

std::string to_text(const Server_Text& value)
{
  std::string text = "Server_Text{ ";
  text += "text=";
  text += leaf_text(&value, SERVER_TEXT_FIELDS[0]);
  text += " }";
  return text;
}

void write_connect(message_stream_t& stream, const Connect& value)
{
  network::Bit_Writer& writer = stream.writer;
  stream.begin_record((uint32_t)Message_Type::Connect);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), CONNECT_FIELDS[0], CONNECT_FIELDS[0].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), CONNECT_FIELDS[1], CONNECT_FIELDS[1].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), CONNECT_FIELDS[2], CONNECT_FIELDS[2].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), CONNECT_FIELDS[3], CONNECT_FIELDS[3].offset, nullptr);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[message written] {}", to_text(value));
}

std::optional<Connect> try_read_connect(network::Bit_Reader& reader)
{
  Connect value;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), CONNECT_FIELDS[0], CONNECT_FIELDS[0].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), CONNECT_FIELDS[1], CONNECT_FIELDS[1].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), CONNECT_FIELDS[2], CONNECT_FIELDS[2].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), CONNECT_FIELDS[3], CONNECT_FIELDS[3].offset))
    return std::nullopt;
  return value;
}

std::string to_text(const Connect& value)
{
  std::string text = "Connect{ ";
  text += "protocol_version=";
  text += leaf_text(&value, CONNECT_FIELDS[0]);
  text += ", player_name=";
  text += leaf_text(&value, CONNECT_FIELDS[1]);
  text += ", schema_hash=";
  text += leaf_text(&value, CONNECT_FIELDS[2]);
  text += ", asset_table_hash=";
  text += leaf_text(&value, CONNECT_FIELDS[3]);
  text += " }";
  return text;
}

void write_disconnect(message_stream_t& stream, const Disconnect& value)
{
  network::Bit_Writer& writer = stream.writer;
  stream.begin_record((uint32_t)Message_Type::Disconnect);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), DISCONNECT_FIELDS[0], DISCONNECT_FIELDS[0].offset, nullptr);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[message written] {}", to_text(value));
}

std::optional<Disconnect> try_read_disconnect(network::Bit_Reader& reader)
{
  Disconnect value;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), DISCONNECT_FIELDS[0], DISCONNECT_FIELDS[0].offset))
    return std::nullopt;
  return value;
}

std::string to_text(const Disconnect& value)
{
  std::string text = "Disconnect{ ";
  text += "reason=";
  text += leaf_text(&value, DISCONNECT_FIELDS[0]);
  text += " }";
  return text;
}

void write_accept(message_stream_t& stream, const Accept& value)
{
  network::Bit_Writer& writer = stream.writer;
  stream.begin_record((uint32_t)Message_Type::Accept);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), ACCEPT_FIELDS[0], ACCEPT_FIELDS[0].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), ACCEPT_FIELDS[1], ACCEPT_FIELDS[1].offset, nullptr);
  write_server_map(writer, value.map);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[message written] {}", to_text(value));
}

std::optional<Accept> try_read_accept(network::Bit_Reader& reader)
{
  Accept value;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), ACCEPT_FIELDS[0], ACCEPT_FIELDS[0].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), ACCEPT_FIELDS[1], ACCEPT_FIELDS[1].offset))
    return std::nullopt;
  {
    const std::optional<Server_Map> field = try_read_server_map(reader);
    if (!field)
      return std::nullopt;
    value.map = *field;
  }
  return value;
}

std::string to_text(const Accept& value)
{
  std::string text = "Accept{ ";
  text += "client_slot=";
  text += leaf_text(&value, ACCEPT_FIELDS[0]);
  text += ", server_tickrate=";
  text += leaf_text(&value, ACCEPT_FIELDS[1]);
  text += ", map=";
  text += to_text(value.map);
  text += " }";
  return text;
}

void write_reject(message_stream_t& stream, const Reject& value)
{
  network::Bit_Writer& writer = stream.writer;
  stream.begin_record((uint32_t)Message_Type::Reject);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), REJECT_FIELDS[0], REJECT_FIELDS[0].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), REJECT_FIELDS[1], REJECT_FIELDS[1].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), REJECT_FIELDS[2], REJECT_FIELDS[2].offset, nullptr);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[message written] {}", to_text(value));
}

std::optional<Reject> try_read_reject(network::Bit_Reader& reader)
{
  Reject value;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), REJECT_FIELDS[0], REJECT_FIELDS[0].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), REJECT_FIELDS[1], REJECT_FIELDS[1].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), REJECT_FIELDS[2], REJECT_FIELDS[2].offset))
    return std::nullopt;
  return value;
}

std::string to_text(const Reject& value)
{
  std::string text = "Reject{ ";
  text += "reason=";
  text += leaf_text(&value, REJECT_FIELDS[0]);
  text += ", server_schema_hash=";
  text += leaf_text(&value, REJECT_FIELDS[1]);
  text += ", server_asset_table_hash=";
  text += leaf_text(&value, REJECT_FIELDS[2]);
  text += " }";
  return text;
}

void write_change_map(reliable_stream_t& reliable, const Change_Map& value)
{
  message_stream_t& stream = reliable.records;
  network::Bit_Writer& writer = stream.writer;
  stream.begin_record((uint32_t)Message_Type::Change_Map);
  write_server_map(writer, value.map);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[message written] {}", to_text(value));
}

std::optional<Change_Map> try_read_change_map(network::Bit_Reader& reader)
{
  Change_Map value;
  {
    const std::optional<Server_Map> field = try_read_server_map(reader);
    if (!field)
      return std::nullopt;
    value.map = *field;
  }
  return value;
}

std::string to_text(const Change_Map& value)
{
  std::string text = "Change_Map{ ";
  text += "map=";
  text += to_text(value.map);
  text += " }";
  return text;
}

void write_cvar_values(reliable_stream_t& reliable, const Cvar_Values& value)
{
  message_stream_t& stream = reliable.records;
  network::Bit_Writer& writer = stream.writer;
  stream.begin_record((uint32_t)Message_Type::Cvar_Values);
  network::write_var_uint(writer, value.values.count);
  for (uint32_t index = 0; index < value.values.count; ++index)
    write_cvar_value(writer, value.values.elements[index]);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[message written] {}", to_text(value));
}

std::optional<Cvar_Values> try_read_cvar_values(network::Bit_Reader& reader)
{
  Cvar_Values value;
  {
    const uint32_t count = network::read_var_uint(reader);
    if (count > 64)
    {
      log_error("Cvar_Values: 'values' carries {} elements, at most 64 fit", count);
      return std::nullopt;
    }
    value.values.count = count;
    for (uint32_t index = 0; index < count; ++index)
    {
      const std::optional<Cvar_Value> element = try_read_cvar_value(reader);
      if (!element)
        return std::nullopt;
      value.values.elements[index] = *element;
    }
  }
  return value;
}

std::string to_text(const Cvar_Values& value)
{
  std::string text = "Cvar_Values{ ";
  text += "values=[";
  for (uint32_t index = 0; index < value.values.count; ++index)
  {
    if (index > 0)
      text += ", ";
    text += to_text(value.values.elements[index]);
  }
  text += "]";
  text += " }";
  return text;
}

void write_map_data_request(reliable_stream_t& reliable, const Map_Data_Request& value)
{
  message_stream_t& stream = reliable.records;
  network::Bit_Writer& writer = stream.writer;
  stream.begin_record((uint32_t)Message_Type::Map_Data_Request);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), MAP_DATA_REQUEST_FIELDS[0], MAP_DATA_REQUEST_FIELDS[0].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), MAP_DATA_REQUEST_FIELDS[1], MAP_DATA_REQUEST_FIELDS[1].offset, nullptr);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[message written] {}", to_text(value));
}

std::optional<Map_Data_Request> try_read_map_data_request(network::Bit_Reader& reader)
{
  Map_Data_Request value;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), MAP_DATA_REQUEST_FIELDS[0], MAP_DATA_REQUEST_FIELDS[0].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), MAP_DATA_REQUEST_FIELDS[1], MAP_DATA_REQUEST_FIELDS[1].offset))
    return std::nullopt;
  return value;
}

std::string to_text(const Map_Data_Request& value)
{
  std::string text = "Map_Data_Request{ ";
  text += "map_generation=";
  text += leaf_text(&value, MAP_DATA_REQUEST_FIELDS[0]);
  text += ", map_name=";
  text += leaf_text(&value, MAP_DATA_REQUEST_FIELDS[1]);
  text += " }";
  return text;
}

void write_ghost_available(reliable_stream_t& reliable, const Ghost_Available& value)
{
  message_stream_t& stream = reliable.records;
  network::Bit_Writer& writer = stream.writer;
  stream.begin_record((uint32_t)Message_Type::Ghost_Available);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), GHOST_AVAILABLE_FIELDS[0], GHOST_AVAILABLE_FIELDS[0].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), GHOST_AVAILABLE_FIELDS[1], GHOST_AVAILABLE_FIELDS[1].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), GHOST_AVAILABLE_FIELDS[2], GHOST_AVAILABLE_FIELDS[2].offset, nullptr);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[message written] {}", to_text(value));
}

std::optional<Ghost_Available> try_read_ghost_available(network::Bit_Reader& reader)
{
  Ghost_Available value;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), GHOST_AVAILABLE_FIELDS[0], GHOST_AVAILABLE_FIELDS[0].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), GHOST_AVAILABLE_FIELDS[1], GHOST_AVAILABLE_FIELDS[1].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), GHOST_AVAILABLE_FIELDS[2], GHOST_AVAILABLE_FIELDS[2].offset))
    return std::nullopt;
  return value;
}

std::string to_text(const Ghost_Available& value)
{
  std::string text = "Ghost_Available{ ";
  text += "party_size=";
  text += leaf_text(&value, GHOST_AVAILABLE_FIELDS[0]);
  text += ", ghost_hash=";
  text += leaf_text(&value, GHOST_AVAILABLE_FIELDS[1]);
  text += ", byte_count=";
  text += leaf_text(&value, GHOST_AVAILABLE_FIELDS[2]);
  text += " }";
  return text;
}

void write_ghost_request(reliable_stream_t& reliable, const Ghost_Request& value)
{
  message_stream_t& stream = reliable.records;
  network::Bit_Writer& writer = stream.writer;
  stream.begin_record((uint32_t)Message_Type::Ghost_Request);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), GHOST_REQUEST_FIELDS[0], GHOST_REQUEST_FIELDS[0].offset, nullptr);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[message written] {}", to_text(value));
}

std::optional<Ghost_Request> try_read_ghost_request(network::Bit_Reader& reader)
{
  Ghost_Request value;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), GHOST_REQUEST_FIELDS[0], GHOST_REQUEST_FIELDS[0].offset))
    return std::nullopt;
  return value;
}

std::string to_text(const Ghost_Request& value)
{
  std::string text = "Ghost_Request{ ";
  text += "ghost_hash=";
  text += leaf_text(&value, GHOST_REQUEST_FIELDS[0]);
  text += " }";
  return text;
}

void write_client_input(message_stream_t& stream, const Client_Input& value)
{
  network::Bit_Writer& writer = stream.writer;
  stream.begin_record((uint32_t)Message_Type::Client_Input);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), CLIENT_INPUT_FIELDS[0], CLIENT_INPUT_FIELDS[0].offset, nullptr);
  write_tick_input(writer, value.input);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), CLIENT_INPUT_FIELDS[1], CLIENT_INPUT_FIELDS[1].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), CLIENT_INPUT_FIELDS[2], CLIENT_INPUT_FIELDS[2].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), CLIENT_INPUT_FIELDS[3], CLIENT_INPUT_FIELDS[3].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), CLIENT_INPUT_FIELDS[4], CLIENT_INPUT_FIELDS[4].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), CLIENT_INPUT_FIELDS[5], CLIENT_INPUT_FIELDS[5].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), CLIENT_INPUT_FIELDS[6], CLIENT_INPUT_FIELDS[6].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), CLIENT_INPUT_FIELDS[7], CLIENT_INPUT_FIELDS[7].offset, nullptr);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[message written] {}", to_text(value));
}

std::optional<Client_Input> try_read_client_input(network::Bit_Reader& reader)
{
  Client_Input value;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), CLIENT_INPUT_FIELDS[0], CLIENT_INPUT_FIELDS[0].offset))
    return std::nullopt;
  {
    const std::optional<Tick_Input> field = try_read_tick_input(reader);
    if (!field)
      return std::nullopt;
    value.input = *field;
  }
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), CLIENT_INPUT_FIELDS[1], CLIENT_INPUT_FIELDS[1].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), CLIENT_INPUT_FIELDS[2], CLIENT_INPUT_FIELDS[2].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), CLIENT_INPUT_FIELDS[3], CLIENT_INPUT_FIELDS[3].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), CLIENT_INPUT_FIELDS[4], CLIENT_INPUT_FIELDS[4].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), CLIENT_INPUT_FIELDS[5], CLIENT_INPUT_FIELDS[5].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), CLIENT_INPUT_FIELDS[6], CLIENT_INPUT_FIELDS[6].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), CLIENT_INPUT_FIELDS[7], CLIENT_INPUT_FIELDS[7].offset))
    return std::nullopt;
  return value;
}

std::string to_text(const Client_Input& value)
{
  std::string text = "Client_Input{ ";
  text += "input_number=";
  text += leaf_text(&value, CLIENT_INPUT_FIELDS[0]);
  text += ", input=";
  text += to_text(value.input);
  text += ", held_snapshot_tick=";
  text += leaf_text(&value, CLIENT_INPUT_FIELDS[1]);
  text += ", map_content_hash=";
  text += leaf_text(&value, CLIENT_INPUT_FIELDS[2]);
  text += ", interpolated_from_tick=";
  text += leaf_text(&value, CLIENT_INPUT_FIELDS[3]);
  text += ", interpolated_towards_tick=";
  text += leaf_text(&value, CLIENT_INPUT_FIELDS[4]);
  text += ", interpolation_fraction=";
  text += leaf_text(&value, CLIENT_INPUT_FIELDS[5]);
  text += ", view_fov_degrees=";
  text += leaf_text(&value, CLIENT_INPUT_FIELDS[6]);
  text += ", view_aspect=";
  text += leaf_text(&value, CLIENT_INPUT_FIELDS[7]);
  text += " }";
  return text;
}

void write_entity_package(message_stream_t& stream, const Entity_Package& value)
{
  network::Bit_Writer& writer = stream.writer;
  stream.begin_record((uint32_t)Message_Type::Entity_Package);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), ENTITY_PACKAGE_FIELDS[0], ENTITY_PACKAGE_FIELDS[0].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), ENTITY_PACKAGE_FIELDS[1], ENTITY_PACKAGE_FIELDS[1].offset, nullptr);
  writer.write_bits(value.entity_data.count, 32);
  writer.write_bytes(value.entity_data.data, value.entity_data.count);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[message written] {}", to_text(value));
}

std::optional<Entity_Package> try_read_entity_package(network::Bit_Reader& reader)
{
  Entity_Package value;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), ENTITY_PACKAGE_FIELDS[0], ENTITY_PACKAGE_FIELDS[0].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), ENTITY_PACKAGE_FIELDS[1], ENTITY_PACKAGE_FIELDS[1].offset))
    return std::nullopt;
  {
    const uint32_t length = reader.read_bits(32);
    const std::optional<Span<const uint8_t>> bytes = reader.try_read_byte_span(length);
    if (!bytes)
    {
      log_error("Entity_Package: 'entity_data' claims {} bytes, {} remain", length, reader.bits_remaining() / 8);
      return std::nullopt;
    }
    value.entity_data = *bytes;
  }
  return value;
}

std::string to_text(const Entity_Package& value)
{
  std::string text = "Entity_Package{ ";
  text += "latest_processed_input_number=";
  text += leaf_text(&value, ENTITY_PACKAGE_FIELDS[0]);
  text += ", delta_from_tick=";
  text += leaf_text(&value, ENTITY_PACKAGE_FIELDS[1]);
  text += std::format(", entity_data=<{} bytes>", value.entity_data.count);
  text += " }";
  return text;
}

void write_shot_debug(message_stream_t& stream, const Shot_Debug& value)
{
  network::Bit_Writer& writer = stream.writer;
  stream.begin_record((uint32_t)Message_Type::Shot_Debug);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SHOT_DEBUG_FIELDS[0], SHOT_DEBUG_FIELDS[0].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SHOT_DEBUG_FIELDS[1], SHOT_DEBUG_FIELDS[1].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SHOT_DEBUG_FIELDS[2], SHOT_DEBUG_FIELDS[2].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SHOT_DEBUG_FIELDS[3], SHOT_DEBUG_FIELDS[3].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SHOT_DEBUG_FIELDS[4], SHOT_DEBUG_FIELDS[4].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SHOT_DEBUG_FIELDS[5], SHOT_DEBUG_FIELDS[5].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SHOT_DEBUG_FIELDS[6], SHOT_DEBUG_FIELDS[6].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SHOT_DEBUG_FIELDS[7], SHOT_DEBUG_FIELDS[7].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SHOT_DEBUG_FIELDS[8], SHOT_DEBUG_FIELDS[8].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SHOT_DEBUG_FIELDS[9], SHOT_DEBUG_FIELDS[9].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SHOT_DEBUG_FIELDS[10], SHOT_DEBUG_FIELDS[10].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SHOT_DEBUG_FIELDS[11], SHOT_DEBUG_FIELDS[11].offset, nullptr);
  network::write_var_uint(writer, value.targets.count);
  for (uint32_t index = 0; index < value.targets.count; ++index)
    write_shot_debug_target(writer, value.targets.elements[index]);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SHOT_DEBUG_FIELDS[12], SHOT_DEBUG_FIELDS[12].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SHOT_DEBUG_FIELDS[13], SHOT_DEBUG_FIELDS[13].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SHOT_DEBUG_FIELDS[14], SHOT_DEBUG_FIELDS[14].offset, nullptr);
  network::write_field(writer, reinterpret_cast<const uint8_t*>(&value), SHOT_DEBUG_FIELDS[15], SHOT_DEBUG_FIELDS[15].offset, nullptr);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[message written] {}", to_text(value));
}

std::optional<Shot_Debug> try_read_shot_debug(network::Bit_Reader& reader)
{
  Shot_Debug value;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SHOT_DEBUG_FIELDS[0], SHOT_DEBUG_FIELDS[0].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SHOT_DEBUG_FIELDS[1], SHOT_DEBUG_FIELDS[1].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SHOT_DEBUG_FIELDS[2], SHOT_DEBUG_FIELDS[2].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SHOT_DEBUG_FIELDS[3], SHOT_DEBUG_FIELDS[3].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SHOT_DEBUG_FIELDS[4], SHOT_DEBUG_FIELDS[4].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SHOT_DEBUG_FIELDS[5], SHOT_DEBUG_FIELDS[5].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SHOT_DEBUG_FIELDS[6], SHOT_DEBUG_FIELDS[6].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SHOT_DEBUG_FIELDS[7], SHOT_DEBUG_FIELDS[7].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SHOT_DEBUG_FIELDS[8], SHOT_DEBUG_FIELDS[8].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SHOT_DEBUG_FIELDS[9], SHOT_DEBUG_FIELDS[9].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SHOT_DEBUG_FIELDS[10], SHOT_DEBUG_FIELDS[10].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SHOT_DEBUG_FIELDS[11], SHOT_DEBUG_FIELDS[11].offset))
    return std::nullopt;
  {
    const uint32_t count = network::read_var_uint(reader);
    if (count > 16)
    {
      log_error("Shot_Debug: 'targets' carries {} elements, at most 16 fit", count);
      return std::nullopt;
    }
    value.targets.count = count;
    for (uint32_t index = 0; index < count; ++index)
    {
      const std::optional<Shot_Debug_Target> element = try_read_shot_debug_target(reader);
      if (!element)
        return std::nullopt;
      value.targets.elements[index] = *element;
    }
  }
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SHOT_DEBUG_FIELDS[12], SHOT_DEBUG_FIELDS[12].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SHOT_DEBUG_FIELDS[13], SHOT_DEBUG_FIELDS[13].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SHOT_DEBUG_FIELDS[14], SHOT_DEBUG_FIELDS[14].offset))
    return std::nullopt;
  if (!network::read_field(reader, reinterpret_cast<uint8_t*>(&value), SHOT_DEBUG_FIELDS[15], SHOT_DEBUG_FIELDS[15].offset))
    return std::nullopt;
  return value;
}

std::string to_text(const Shot_Debug& value)
{
  std::string text = "Shot_Debug{ ";
  text += "input_number=";
  text += leaf_text(&value, SHOT_DEBUG_FIELDS[0]);
  text += ", fire_subtick_slot=";
  text += leaf_text(&value, SHOT_DEBUG_FIELDS[1]);
  text += ", eye=";
  text += leaf_text(&value, SHOT_DEBUG_FIELDS[2]);
  text += ", direction=";
  text += leaf_text(&value, SHOT_DEBUG_FIELDS[3]);
  text += ", bracket_status=";
  text += leaf_text(&value, SHOT_DEBUG_FIELDS[4]);
  text += ", used_rewind=";
  text += leaf_text(&value, SHOT_DEBUG_FIELDS[5]);
  text += ", requested_from_tick=";
  text += leaf_text(&value, SHOT_DEBUG_FIELDS[6]);
  text += ", requested_towards_tick=";
  text += leaf_text(&value, SHOT_DEBUG_FIELDS[7]);
  text += ", requested_fraction=";
  text += leaf_text(&value, SHOT_DEBUG_FIELDS[8]);
  text += ", used_from_tick=";
  text += leaf_text(&value, SHOT_DEBUG_FIELDS[9]);
  text += ", used_towards_tick=";
  text += leaf_text(&value, SHOT_DEBUG_FIELDS[10]);
  text += ", used_fraction=";
  text += leaf_text(&value, SHOT_DEBUG_FIELDS[11]);
  text += ", targets=[";
  for (uint32_t index = 0; index < value.targets.count; ++index)
  {
    if (index > 0)
      text += ", ";
    text += to_text(value.targets.elements[index]);
  }
  text += "]";
  text += ", hit_uid=";
  text += leaf_text(&value, SHOT_DEBUG_FIELDS[12]);
  text += ", hit_region=";
  text += leaf_text(&value, SHOT_DEBUG_FIELDS[13]);
  text += ", impact_point=";
  text += leaf_text(&value, SHOT_DEBUG_FIELDS[14]);
  text += ", nearest_miss_distance=";
  text += leaf_text(&value, SHOT_DEBUG_FIELDS[15]);
  text += " }";
  return text;
}

void write_bot_debug(message_stream_t& stream, const Bot_Debug& value)
{
  network::Bit_Writer& writer = stream.writer;
  stream.begin_record((uint32_t)Message_Type::Bot_Debug);
  network::write_var_uint(writer, value.bots.count);
  for (uint32_t index = 0; index < value.bots.count; ++index)
    write_bot_debug_entry(writer, value.bots.elements[index]);
  ++stream.count;

  if (stream.log_fired)
    log_terminal("[message written] {}", to_text(value));
}

std::optional<Bot_Debug> try_read_bot_debug(network::Bit_Reader& reader)
{
  Bot_Debug value;
  {
    const uint32_t count = network::read_var_uint(reader);
    if (count > 16)
    {
      log_error("Bot_Debug: 'bots' carries {} elements, at most 16 fit", count);
      return std::nullopt;
    }
    value.bots.count = count;
    for (uint32_t index = 0; index < count; ++index)
    {
      const std::optional<Bot_Debug_Entry> element = try_read_bot_debug_entry(reader);
      if (!element)
        return std::nullopt;
      value.bots.elements[index] = *element;
    }
  }
  return value;
}

std::string to_text(const Bot_Debug& value)
{
  std::string text = "Bot_Debug{ ";
  text += "bots=[";
  for (uint32_t index = 0; index < value.bots.count; ++index)
  {
    if (index > 0)
      text += ", ";
    text += to_text(value.bots.elements[index]);
  }
  text += "]";
  text += " }";
  return text;
}

std::string message_stream_to_text(const message_stream_t& stream)
{
  if (stream.empty())
    return "<nothing pending>";

  network::Bit_Reader reader(stream.writer.buffer.data(),
                             stream.writer.buffer.size());
  reader.read_bits(MESSAGE_STREAM_COUNT_BITS);

  std::string text;
  for (uint32_t index = 0; index < stream.count; ++index)
  {
    if (index > 0)
      text += '\n';

    const uint32_t kind = reader.read_bits(16);
    if (kind >= MESSAGE_TYPE_COUNT)
    {
      text += std::format("<unknown kind {}; the rest is unreadable>", kind);
      break;
    }

    switch ((Message_Type)kind)
    {
      case Message_Type::Rocket_Detonated:
      {
        const std::optional<Rocket_Detonated> payload = try_read_rocket_detonated(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case Message_Type::Player_Died:
      {
        const std::optional<Player_Died> payload = try_read_player_died(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case Message_Type::Player_Spawned:
      {
        const std::optional<Player_Spawned> payload = try_read_player_spawned(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case Message_Type::Objective_Reached:
      {
        const std::optional<Objective_Reached> payload = try_read_objective_reached(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case Message_Type::Rocket_Explosion:
      {
        const std::optional<Rocket_Explosion> payload = try_read_rocket_explosion(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case Message_Type::Footstep:
      {
        const std::optional<Footstep> payload = try_read_footstep(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case Message_Type::Jump:
      {
        const std::optional<Jump> payload = try_read_jump(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case Message_Type::Land:
      {
        const std::optional<Land> payload = try_read_land(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case Message_Type::Shot_Impact:
      {
        const std::optional<Shot_Impact> payload = try_read_shot_impact(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case Message_Type::Jump_Pad_Launch:
      {
        const std::optional<Jump_Pad_Launch> payload = try_read_jump_pad_launch(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case Message_Type::Ping:
      {
        const std::optional<Ping> payload = try_read_ping(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case Message_Type::Pilot_Impact:
      {
        const std::optional<Pilot_Impact> payload = try_read_pilot_impact(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case Message_Type::Console_Line:
      {
        const std::optional<Console_Line> payload = try_read_console_line(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case Message_Type::Server_Text:
      {
        const std::optional<Server_Text> payload = try_read_server_text(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case Message_Type::Connect:
      {
        const std::optional<Connect> payload = try_read_connect(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case Message_Type::Disconnect:
      {
        const std::optional<Disconnect> payload = try_read_disconnect(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case Message_Type::Accept:
      {
        const std::optional<Accept> payload = try_read_accept(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case Message_Type::Reject:
      {
        const std::optional<Reject> payload = try_read_reject(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case Message_Type::Change_Map:
      {
        const std::optional<Change_Map> payload = try_read_change_map(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case Message_Type::Cvar_Values:
      {
        const std::optional<Cvar_Values> payload = try_read_cvar_values(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case Message_Type::Map_Data_Request:
      {
        const std::optional<Map_Data_Request> payload = try_read_map_data_request(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case Message_Type::Ghost_Available:
      {
        const std::optional<Ghost_Available> payload = try_read_ghost_available(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case Message_Type::Ghost_Request:
      {
        const std::optional<Ghost_Request> payload = try_read_ghost_request(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case Message_Type::Client_Input:
      {
        const std::optional<Client_Input> payload = try_read_client_input(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case Message_Type::Entity_Package:
      {
        const std::optional<Entity_Package> payload = try_read_entity_package(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case Message_Type::Shot_Debug:
      {
        const std::optional<Shot_Debug> payload = try_read_shot_debug(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
      case Message_Type::Bot_Debug:
      {
        const std::optional<Bot_Debug> payload = try_read_bot_debug(reader);
        if (!payload)
        {
          text += "<undecodable payload; the rest is unreadable>";
          return text;
        }
        text += to_text(*payload);
        break;
      }
    }
  }

  return text;
}

} // namespace shared
