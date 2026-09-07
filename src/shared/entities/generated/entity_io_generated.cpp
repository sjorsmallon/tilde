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
   .type = FIELD_TYPE_U32,
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
  {SET_COLOR_FIELDS, 1},
  {},   // Kill
  {SET_HEALTH_FIELDS, 1},
  {DAMAGE_FIELDS, 1},
};

constexpr Span<const field_info_t> SIGNAL_PAYLOAD_FIELDS[] = {
  {COLOR_CHANGED_FIELDS, 1},
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
    case entity_action::Set_Color: return "Set_Color";
    case entity_action::Kill: return "Kill";
    case entity_action::Set_Health: return "Set_Health";
    case entity_action::Damage: return "Damage";
  }
  return "<unknown>";
}

template <> std::optional<entity_action> try_from_string<entity_action>(std::string_view text)
{
  if (text == "Use") return entity_action::Use;
  if (text == "Enable") return entity_action::Enable;
  if (text == "Disable") return entity_action::Disable;
  if (text == "Toggle_Enabled") return entity_action::Toggle_Enabled;
  if (text == "Set_Color") return entity_action::Set_Color;
  if (text == "Kill") return entity_action::Kill;
  if (text == "Set_Health") return entity_action::Set_Health;
  if (text == "Damage") return entity_action::Damage;
  return std::nullopt;
}

const char* to_string(entity_signal value)
{
  switch (value)
  {
    case entity_signal::Color_Changed: return "Color_Changed";
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
    case entity_trait::Colorable: return "Colorable";
    case entity_trait::Touchable: return "Touchable";
    case entity_trait::Mortal: return "Mortal";
  }
  return "<unknown>";
}

template <> std::optional<entity_trait> try_from_string<entity_trait>(std::string_view text)
{
  if (text == "Usable") return entity_trait::Usable;
  if (text == "Switchable") return entity_trait::Switchable;
  if (text == "Colorable") return entity_trait::Colorable;
  if (text == "Touchable") return entity_trait::Touchable;
  if (text == "Mortal") return entity_trait::Mortal;
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

action_data_t erase(const Set_Color_Data& payload)
{
  action_data_t data;
  data.tag = entity_action::Set_Color;
  data.set_color = payload;
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

} // namespace entities
