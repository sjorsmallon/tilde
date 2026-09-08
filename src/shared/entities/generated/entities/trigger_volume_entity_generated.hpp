// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
//
// Trigger_Volume_Entity: what it IS, and what it can be TOLD.
//
// The includes are relative to THIS file rather than to src/shared: a
// quoted include is resolved against the including file's directory first.
#pragma once

#include "../entities_core_generated.hpp"
#include "../traits/switchable_generated.hpp"
#include "../traits/touchable_generated.hpp"

namespace entities
{

struct Trigger_Volume_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Trigger_Volume_Entity;

  Trigger_Volume_Entity() { type = entity_type::Trigger_Volume_Entity; }

  Enabled switch_state = {};
  Box_Volume volume = {.half_extents = {64.0f, 64.0f, 64.0f}};
  Trigger_Action action = Trigger_Action::Kill;
  Fire_Mode fire_mode = Fire_Mode::On_Enter;
  network::pascal_string_t<64> param_target_name = {};
  network::pascal_string_t<128> param_string = {};
  float param_float = 0.0f;
};

// The entity pool is a byte buffer: it copies with memcpy and runs no
// destructor. A field that breaks either of these corrupts or leaks
// silently, so the check lives here rather than in a test nobody runs
// before the pool does.
static_assert(std::is_trivially_copyable_v<Trigger_Volume_Entity>,
              "Trigger_Volume_Entity must stay trivially copyable: pooled storage, snapshot "
              "baselines and undo all copy entities with memcpy");
static_assert(std::is_trivially_destructible_v<Trigger_Volume_Entity>,
              "Trigger_Volume_Entity must stay trivially destructible: the entity pool frees a "
              "slot by overwriting it and runs no destructor");
static_assert(std::is_base_of_v<Entity, Trigger_Volume_Entity>,
              "Trigger_Volume_Entity must derive from Entity: the generated tables hand out "
              "Entity* for every entity type");

// --- what a Trigger_Volume_Entity accepts ---
//
// Its `is` list is: Switchable, Touchable.
// No handler for a verb this type does not accept EXISTS, so calling one
// is "no matching function" rather than a runtime refusal; a declared
// handler nobody defined is a LINK error naming the symbol.
void enable(Entity&, Enabled&, const Enable_Data&, input_context_t&);   // Switchable, shared by every opting-in type: src/server/traits/switchable.cpp
void disable(Entity&, Enabled&, const Disable_Data&, input_context_t&);   // Switchable, shared by every opting-in type: src/server/traits/switchable.cpp
void toggle_enabled(Entity&, Enabled&, const Toggle_Enabled_Data&, input_context_t&);   // Switchable, shared by every opting-in type: src/server/traits/switchable.cpp

// --- what a Trigger_Volume_Entity announces ---
//
// Declared in the trait headers above and defined once in the
// binder; repeated here so this file answers both halves. The
// SYSTEM that writes the state change is what calls one.
void emit_touched(const Entity& sender, const Touched_Data& payload, input_context_t& context);   // Touchable
void emit_left(const Entity& sender, const Left_Data& payload, input_context_t& context);   // Touchable

} // namespace entities
