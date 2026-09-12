// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
#include "entity_io_generated.hpp"

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#elif defined(_MSC_VER)
#pragma warning(disable : 4841)
#endif

namespace entities
{

namespace
{

constexpr field_info_t SET_COLOR_FIELDS[] = {
  {.name = "color",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Set_Color_Data, color),
   .size_in_bytes = (uint32_t)sizeof(Set_Color_Data::color),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
};

constexpr field_info_t ADD_FIELDS[] = {
  {.name = "amount",
   .type = FIELD_TYPE_I32,
   .offset = (uint32_t)offsetof(Add_Data, amount),
   .size_in_bytes = (uint32_t)sizeof(Add_Data::amount),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
};

constexpr field_info_t SET_HEALTH_FIELDS[] = {
  {.name = "amount",
   .type = FIELD_TYPE_I32,
   .offset = (uint32_t)offsetof(Set_Health_Data, amount),
   .size_in_bytes = (uint32_t)sizeof(Set_Health_Data::amount),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
};

constexpr field_info_t DAMAGE_FIELDS[] = {
  {.name = "amount",
   .type = FIELD_TYPE_I32,
   .offset = (uint32_t)offsetof(Damage_Data, amount),
   .size_in_bytes = (uint32_t)sizeof(Damage_Data::amount),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
};

constexpr field_info_t TELEPORT_FIELDS[] = {
  {.name = "destination",
   .type = FIELD_TYPE_ENTITY_UID,
   .offset = (uint32_t)offsetof(Teleport_Data, destination),
   .size_in_bytes = (uint32_t)sizeof(Teleport_Data::destination),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
  {.name = "keep_velocity",
   .type = FIELD_TYPE_BOOL,
   .offset = (uint32_t)offsetof(Teleport_Data, keep_velocity),
   .size_in_bytes = (uint32_t)sizeof(Teleport_Data::keep_velocity),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
};

constexpr field_info_t SET_VELOCITY_FIELDS[] = {
  {.name = "velocity",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Set_Velocity_Data, velocity),
   .size_in_bytes = (uint32_t)sizeof(Set_Velocity_Data::velocity),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
};

constexpr field_info_t ADD_VELOCITY_FIELDS[] = {
  {.name = "velocity",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Add_Velocity_Data, velocity),
   .size_in_bytes = (uint32_t)sizeof(Add_Velocity_Data::velocity),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
};

constexpr field_info_t GRANT_WEAPON_FIELDS[] = {
  {.name = "weapon",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Grant_Weapon_Data, weapon),
   .size_in_bytes = (uint32_t)sizeof(Grant_Weapon_Data::weapon),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = &ENUM_INFOS[2]},
  {.name = "damage_type",
   .type = FIELD_TYPE_ENUM,
   .offset = (uint32_t)offsetof(Grant_Weapon_Data, damage_type),
   .size_in_bytes = (uint32_t)sizeof(Grant_Weapon_Data::damage_type),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = &ENUM_INFOS[9]},
};

constexpr field_info_t SET_RESPAWN_POINT_FIELDS[] = {
  {.name = "location",
   .type = FIELD_TYPE_ENTITY_UID,
   .offset = (uint32_t)offsetof(Set_Respawn_Point_Data, location),
   .size_in_bytes = (uint32_t)sizeof(Set_Respawn_Point_Data::location),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
};

constexpr field_info_t COLOR_CHANGED_FIELDS[] = {
  {.name = "color",
   .type = FIELD_TYPE_V3,
   .offset = (uint32_t)offsetof(Color_Changed_Data, color),
   .size_in_bytes = (uint32_t)sizeof(Color_Changed_Data::color),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
};

constexpr field_info_t DIED_FIELDS[] = {
  {.name = "killer",
   .type = FIELD_TYPE_ENTITY_UID,
   .offset = (uint32_t)offsetof(Died_Data, killer),
   .size_in_bytes = (uint32_t)sizeof(Died_Data::killer),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
};

constexpr field_info_t HEALTH_CHANGED_FIELDS[] = {
  {.name = "health",
   .type = FIELD_TYPE_I32,
   .offset = (uint32_t)offsetof(Health_Changed_Data, health),
   .size_in_bytes = (uint32_t)sizeof(Health_Changed_Data::health),
   .flags = 0u,
   .component_id = NOT_A_COMPONENT,
   .string_capacity = NOT_A_STRING,
   .asset_class_id = NOT_AN_ASSET_CLASS,
   .enum_info = NOT_AN_ENUM},
};

constexpr Span<const field_info_t> ACTION_PAYLOAD_FIELDS[] = {
  {},   // Use
  {},   // Enable
  {},   // Disable
  {},   // Toggle_Enabled
  {},   // Play
  {SET_COLOR_FIELDS, 1},
  {ADD_FIELDS, 1},
  {},   // Reset
  {},   // Kill
  {SET_HEALTH_FIELDS, 1},
  {DAMAGE_FIELDS, 1},
  {TELEPORT_FIELDS, 2},
  {SET_VELOCITY_FIELDS, 1},
  {ADD_VELOCITY_FIELDS, 1},
  {GRANT_WEAPON_FIELDS, 2},
  {SET_RESPAWN_POINT_FIELDS, 1},
  {},   // Complete_Level
};

constexpr Span<const field_info_t> SIGNAL_PAYLOAD_FIELDS[] = {
  {COLOR_CHANGED_FIELDS, 1},
  {},   // Limit_Reached
  {},   // Touched
  {},   // Left
  {DIED_FIELDS, 1},
  {HEALTH_CHANGED_FIELDS, 1},
};

} // namespace

const char* to_string(entity_action value)
{
  switch (value)
  {
    case entity_action::Use: return "Use";
    case entity_action::Enable: return "Enable";
    case entity_action::Disable: return "Disable";
    case entity_action::Toggle_Enabled: return "Toggle_Enabled";
    case entity_action::Play: return "Play";
    case entity_action::Set_Color: return "Set_Color";
    case entity_action::Add: return "Add";
    case entity_action::Reset: return "Reset";
    case entity_action::Kill: return "Kill";
    case entity_action::Set_Health: return "Set_Health";
    case entity_action::Damage: return "Damage";
    case entity_action::Teleport: return "Teleport";
    case entity_action::Set_Velocity: return "Set_Velocity";
    case entity_action::Add_Velocity: return "Add_Velocity";
    case entity_action::Grant_Weapon: return "Grant_Weapon";
    case entity_action::Set_Respawn_Point: return "Set_Respawn_Point";
    case entity_action::Complete_Level: return "Complete_Level";
  }
  return "<unknown>";
}

template <> std::optional<entity_action> try_from_string<entity_action>(std::string_view text)
{
  if (text == "Use") return entity_action::Use;
  if (text == "Enable") return entity_action::Enable;
  if (text == "Disable") return entity_action::Disable;
  if (text == "Toggle_Enabled") return entity_action::Toggle_Enabled;
  if (text == "Play") return entity_action::Play;
  if (text == "Set_Color") return entity_action::Set_Color;
  if (text == "Add") return entity_action::Add;
  if (text == "Reset") return entity_action::Reset;
  if (text == "Kill") return entity_action::Kill;
  if (text == "Set_Health") return entity_action::Set_Health;
  if (text == "Damage") return entity_action::Damage;
  if (text == "Teleport") return entity_action::Teleport;
  if (text == "Set_Velocity") return entity_action::Set_Velocity;
  if (text == "Add_Velocity") return entity_action::Add_Velocity;
  if (text == "Grant_Weapon") return entity_action::Grant_Weapon;
  if (text == "Set_Respawn_Point") return entity_action::Set_Respawn_Point;
  if (text == "Complete_Level") return entity_action::Complete_Level;
  return std::nullopt;
}

const char* to_string(entity_signal value)
{
  switch (value)
  {
    case entity_signal::Color_Changed: return "Color_Changed";
    case entity_signal::Limit_Reached: return "Limit_Reached";
    case entity_signal::Touched: return "Touched";
    case entity_signal::Left: return "Left";
    case entity_signal::Died: return "Died";
    case entity_signal::Health_Changed: return "Health_Changed";
  }
  return "<unknown>";
}

template <> std::optional<entity_signal> try_from_string<entity_signal>(std::string_view text)
{
  if (text == "Color_Changed") return entity_signal::Color_Changed;
  if (text == "Limit_Reached") return entity_signal::Limit_Reached;
  if (text == "Touched") return entity_signal::Touched;
  if (text == "Left") return entity_signal::Left;
  if (text == "Died") return entity_signal::Died;
  if (text == "Health_Changed") return entity_signal::Health_Changed;
  return std::nullopt;
}

const char* to_string(entity_trait value)
{
  switch (value)
  {
    case entity_trait::Usable: return "Usable";
    case entity_trait::Switchable: return "Switchable";
    case entity_trait::Playable: return "Playable";
    case entity_trait::Colorable: return "Colorable";
    case entity_trait::Counting: return "Counting";
    case entity_trait::Touchable: return "Touchable";
    case entity_trait::Mortal: return "Mortal";
    case entity_trait::Mobile: return "Mobile";
    case entity_trait::Armable: return "Armable";
    case entity_trait::Respawnable: return "Respawnable";
    case entity_trait::Objective: return "Objective";
  }
  return "<unknown>";
}

template <> std::optional<entity_trait> try_from_string<entity_trait>(std::string_view text)
{
  if (text == "Usable") return entity_trait::Usable;
  if (text == "Switchable") return entity_trait::Switchable;
  if (text == "Playable") return entity_trait::Playable;
  if (text == "Colorable") return entity_trait::Colorable;
  if (text == "Counting") return entity_trait::Counting;
  if (text == "Touchable") return entity_trait::Touchable;
  if (text == "Mortal") return entity_trait::Mortal;
  if (text == "Mobile") return entity_trait::Mobile;
  if (text == "Armable") return entity_trait::Armable;
  if (text == "Respawnable") return entity_trait::Respawnable;
  if (text == "Objective") return entity_trait::Objective;
  return std::nullopt;
}

Span<const field_info_t> action_payload_fields(entity_action action)
{
  assert((uint32_t)action < ENTITY_ACTION_COUNT);
  return ACTION_PAYLOAD_FIELDS[(uint16_t)action];
}

Span<const field_info_t> signal_payload_fields(entity_signal signal)
{
  assert((uint32_t)signal < ENTITY_SIGNAL_COUNT);
  return SIGNAL_PAYLOAD_FIELDS[(uint16_t)signal];
}

uint32_t action_payload_size(entity_action action)
{
  switch (action)
  {
    case entity_action::Use: return (uint32_t)sizeof(Use_Data);
    case entity_action::Enable: return (uint32_t)sizeof(Enable_Data);
    case entity_action::Disable: return (uint32_t)sizeof(Disable_Data);
    case entity_action::Toggle_Enabled: return (uint32_t)sizeof(Toggle_Enabled_Data);
    case entity_action::Play: return (uint32_t)sizeof(Play_Data);
    case entity_action::Set_Color: return (uint32_t)sizeof(Set_Color_Data);
    case entity_action::Add: return (uint32_t)sizeof(Add_Data);
    case entity_action::Reset: return (uint32_t)sizeof(Reset_Data);
    case entity_action::Kill: return (uint32_t)sizeof(Kill_Data);
    case entity_action::Set_Health: return (uint32_t)sizeof(Set_Health_Data);
    case entity_action::Damage: return (uint32_t)sizeof(Damage_Data);
    case entity_action::Teleport: return (uint32_t)sizeof(Teleport_Data);
    case entity_action::Set_Velocity: return (uint32_t)sizeof(Set_Velocity_Data);
    case entity_action::Add_Velocity: return (uint32_t)sizeof(Add_Velocity_Data);
    case entity_action::Grant_Weapon: return (uint32_t)sizeof(Grant_Weapon_Data);
    case entity_action::Set_Respawn_Point: return (uint32_t)sizeof(Set_Respawn_Point_Data);
    case entity_action::Complete_Level: return (uint32_t)sizeof(Complete_Level_Data);
  }
  return 0;
}

uint32_t signal_payload_size(entity_signal signal)
{
  switch (signal)
  {
    case entity_signal::Color_Changed: return (uint32_t)sizeof(Color_Changed_Data);
    case entity_signal::Limit_Reached: return (uint32_t)sizeof(Limit_Reached_Data);
    case entity_signal::Touched: return (uint32_t)sizeof(Touched_Data);
    case entity_signal::Left: return (uint32_t)sizeof(Left_Data);
    case entity_signal::Died: return (uint32_t)sizeof(Died_Data);
    case entity_signal::Health_Changed: return (uint32_t)sizeof(Health_Changed_Data);
  }
  return 0;
}

action_data_t erase(const Use_Data& payload)
{
  action_data_t data;
  data.tag = entity_action::Use;
  data.use = payload;
  return data;
}

action_data_t erase(const Enable_Data& payload)
{
  action_data_t data;
  data.tag = entity_action::Enable;
  data.enable = payload;
  return data;
}

action_data_t erase(const Disable_Data& payload)
{
  action_data_t data;
  data.tag = entity_action::Disable;
  data.disable = payload;
  return data;
}

action_data_t erase(const Toggle_Enabled_Data& payload)
{
  action_data_t data;
  data.tag = entity_action::Toggle_Enabled;
  data.toggle_enabled = payload;
  return data;
}

action_data_t erase(const Play_Data& payload)
{
  action_data_t data;
  data.tag = entity_action::Play;
  data.play = payload;
  return data;
}

action_data_t erase(const Set_Color_Data& payload)
{
  action_data_t data;
  data.tag = entity_action::Set_Color;
  data.set_color = payload;
  return data;
}

action_data_t erase(const Add_Data& payload)
{
  action_data_t data;
  data.tag = entity_action::Add;
  data.add = payload;
  return data;
}

action_data_t erase(const Reset_Data& payload)
{
  action_data_t data;
  data.tag = entity_action::Reset;
  data.reset = payload;
  return data;
}

action_data_t erase(const Kill_Data& payload)
{
  action_data_t data;
  data.tag = entity_action::Kill;
  data.kill = payload;
  return data;
}

action_data_t erase(const Set_Health_Data& payload)
{
  action_data_t data;
  data.tag = entity_action::Set_Health;
  data.set_health = payload;
  return data;
}

action_data_t erase(const Damage_Data& payload)
{
  action_data_t data;
  data.tag = entity_action::Damage;
  data.damage = payload;
  return data;
}

action_data_t erase(const Teleport_Data& payload)
{
  action_data_t data;
  data.tag = entity_action::Teleport;
  data.teleport = payload;
  return data;
}

action_data_t erase(const Set_Velocity_Data& payload)
{
  action_data_t data;
  data.tag = entity_action::Set_Velocity;
  data.set_velocity = payload;
  return data;
}

action_data_t erase(const Add_Velocity_Data& payload)
{
  action_data_t data;
  data.tag = entity_action::Add_Velocity;
  data.add_velocity = payload;
  return data;
}

action_data_t erase(const Grant_Weapon_Data& payload)
{
  action_data_t data;
  data.tag = entity_action::Grant_Weapon;
  data.grant_weapon = payload;
  return data;
}

action_data_t erase(const Set_Respawn_Point_Data& payload)
{
  action_data_t data;
  data.tag = entity_action::Set_Respawn_Point;
  data.set_respawn_point = payload;
  return data;
}

action_data_t erase(const Complete_Level_Data& payload)
{
  action_data_t data;
  data.tag = entity_action::Complete_Level;
  data.complete_level = payload;
  return data;
}

} // namespace entities
