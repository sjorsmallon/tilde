#pragma once

#include "../entity.hpp"
#include "../shapes.hpp"

namespace network
{

// A volume that invokes a named action when a player enters/overlaps it.
//
// Design notes (the WHY behind the fields):
//
//   action_name (pascal_string)
//     Looked up in server::Trigger_Action_Registry at fire time. We store the
//     name -- not an int ID -- so reordering or removing actions does not
//     silently rebind any saved trigger. Default is "kill" because that was
//     the only action before the registry refactor; existing maps therefore
//     keep their pre-refactor behavior unchanged. The set of valid names
//     lives in src/shared/trigger_action_list.hpp (X-macro), shared between
//     the client editor's inspector dropdown and the server's dispatch table.
//
//   fire_mode (pascal_string)
//     One of "on_enter" or "every_tick". Stored as a string (not int enum)
//     for the same reason as action_name -- and so the inspector can reuse
//     the same `string_choices_provider` dropdown mechanism. "on_enter"
//     fires once on the rising overlap edge; "every_tick" fires every server
//     tick while overlapping. We expose both because actions like
//     "print_message" or "spawn_entity" would spam at 60Hz on `every_tick`,
//     while `kill` is idempotent and works under either mode.
//
//   param_target_name, param_string, param_float
//     Generic typed slots that actions read from. Each action consumes only
//     the slots it needs; unused slots are ignored. This is a pragmatic
//     middle ground between a single stringly-typed param (Source-style)
//     and a full per-action schema (Unity / Unreal), which would require
//     discriminated-union schema support that doesn't exist here yet.

class Trigger_Volume_Entity : public Entity
{
public:
  SCHEMA_FIELD(shared::box_volume_t, volume,
               Schema_Flags::Networked | Schema_Flags::Editable | Schema_Flags::Saveable);

  shared::box_volume_t *get_box_volume() override { return &volume; }
  const shared::box_volume_t *get_box_volume() const override { return &volume; }

  SCHEMA_FIELD_DEFAULT(pascal_string, action_name,
                       Schema_Flags::Editable | Schema_Flags::Saveable,
                       pascal_string("kill"));

  SCHEMA_FIELD_DEFAULT(pascal_string, fire_mode,
                       Schema_Flags::Editable | Schema_Flags::Saveable,
                       pascal_string("on_enter"));

  SCHEMA_FIELD(pascal_string, param_target_name,
               Schema_Flags::Editable | Schema_Flags::Saveable);
  SCHEMA_FIELD(pascal_string, param_string,
               Schema_Flags::Editable | Schema_Flags::Saveable);
  SCHEMA_FIELD_DEFAULT(float32, param_float,
                       Schema_Flags::Editable | Schema_Flags::Saveable, 0.f);

  DECLARE_SCHEMA(Trigger_Volume_Entity)
};

SCHEMA_NAME_FOR_TYPE(Trigger_Volume_Entity)

} // namespace network
