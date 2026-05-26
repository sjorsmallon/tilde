#include "trigger_volume_entity.hpp"

#include "../trigger_action_list.hpp"

namespace network
{

// Compile-time list of valid fire modes. Used both for the inspector dropdown
// and for validation in the server tick loop. Add new modes here AND extend
// the dispatch in server_impl.cpp's trigger-overlap block.
static std::vector<std::string> list_trigger_fire_modes()
{
  return {"on_enter", "every_tick"};
}

// Compile-time list of valid trigger action names. Source of truth is the
// TRIGGER_ACTION_LIST X-macro in src/shared/trigger_action_list.hpp; the
// server-side dispatch table in src/server/trigger_actions.cpp reads from
// the same macro, so renaming one side without the other is impossible
// without touching the shared header.
static std::vector<std::string> list_trigger_action_names()
{
  std::vector<std::string> names;
#define X(symbol, string_name) names.emplace_back(string_name);
  TRIGGER_ACTION_LIST
#undef X
  return names;
}

DEFINE_SCHEMA_CLASS(Trigger_Volume_Entity, Entity)
{
  BEGIN_SCHEMA_FIELDS()
  REGISTER_SCHEMA_FIELD(volume);

  REGISTER_SCHEMA_FIELD(action_name);
  // Inspector renders this string field as a Combo. The list comes from the
  // shared X-macro (header is the single source of truth across client +
  // server); registration order within the server registry is irrelevant
  // because we never consult the registry here.
  props.back().string_choices_provider = &list_trigger_action_names;

  REGISTER_SCHEMA_FIELD(fire_mode);
  props.back().string_choices_provider = &list_trigger_fire_modes;

  REGISTER_SCHEMA_FIELD(param_target_name);
  REGISTER_SCHEMA_FIELD(param_string);
  REGISTER_SCHEMA_FIELD(param_float);
  END_SCHEMA_FIELDS()
}

} // namespace network
