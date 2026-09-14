// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
//
// Logic_Timer_Entity: what it IS, and what it can be TOLD.
//
// The includes are relative to THIS file rather than to src/shared: a
// quoted include is resolved against the including file's directory first.
#pragma once

#include "../entities_core_generated.hpp"
#include "../traits/timer_generated.hpp"

namespace entities
{

struct Logic_Timer_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Logic_Timer_Entity;

  Logic_Timer_Entity() { type = entity_type::Logic_Timer_Entity; }

  Timer_State timer = {};
};

// The entity pool is a byte buffer: it copies with memcpy and runs no
// destructor. A field that breaks either of these corrupts or leaks
// silently, so the check lives here rather than in a test nobody runs
// before the pool does.
static_assert(std::is_trivially_copyable_v<Logic_Timer_Entity>,
              "Logic_Timer_Entity must stay trivially copyable: pooled storage, snapshot "
              "baselines and undo all copy entities with memcpy");
static_assert(std::is_trivially_destructible_v<Logic_Timer_Entity>,
              "Logic_Timer_Entity must stay trivially destructible: the entity pool frees a "
              "slot by overwriting it and runs no destructor");
static_assert(std::is_base_of_v<Entity, Logic_Timer_Entity>,
              "Logic_Timer_Entity must derive from Entity: the generated tables hand out "
              "Entity* for every entity type");

// --- what a Logic_Timer_Entity accepts ---
//
// Its `is` list is: Timer.
// No handler for a verb this type does not accept EXISTS, so calling one
// is "no matching function" rather than a runtime refusal; a declared
// handler nobody defined is a LINK error naming the symbol.
void start(Entity&, Timer_State&, const Start_Data&, input_context_t&);   // Timer, shared by every opting-in type: src/server/traits/timer.cpp
void stop(Entity&, Timer_State&, const Stop_Data&, input_context_t&);   // Timer, shared by every opting-in type: src/server/traits/timer.cpp
void restart(Entity&, Timer_State&, const Restart_Data&, input_context_t&);   // Timer, shared by every opting-in type: src/server/traits/timer.cpp

// --- what a Logic_Timer_Entity announces ---
//
// Declared in the trait headers above and defined once in the
// binder; repeated here so this file answers both halves. The
// SYSTEM that writes the state change is what calls one.
void emit_elapsed(const Entity& sender, const Elapsed_Data& payload, input_context_t& context);   // Timer

} // namespace entities
