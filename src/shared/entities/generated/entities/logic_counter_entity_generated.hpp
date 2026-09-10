// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
//
// Logic_Counter_Entity: what it IS, and what it can be TOLD.
//
// The includes are relative to THIS file rather than to src/shared: a
// quoted include is resolved against the including file's directory first.
#pragma once

#include "../entities_core_generated.hpp"
#include "../traits/counting_generated.hpp"

namespace entities
{

struct Logic_Counter_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Logic_Counter_Entity;

  Logic_Counter_Entity() { type = entity_type::Logic_Counter_Entity; }

  Counter counter = {};
};

// The entity pool is a byte buffer: it copies with memcpy and runs no
// destructor. A field that breaks either of these corrupts or leaks
// silently, so the check lives here rather than in a test nobody runs
// before the pool does.
static_assert(std::is_trivially_copyable_v<Logic_Counter_Entity>,
              "Logic_Counter_Entity must stay trivially copyable: pooled storage, snapshot "
              "baselines and undo all copy entities with memcpy");
static_assert(std::is_trivially_destructible_v<Logic_Counter_Entity>,
              "Logic_Counter_Entity must stay trivially destructible: the entity pool frees a "
              "slot by overwriting it and runs no destructor");
static_assert(std::is_base_of_v<Entity, Logic_Counter_Entity>,
              "Logic_Counter_Entity must derive from Entity: the generated tables hand out "
              "Entity* for every entity type");

// --- what a Logic_Counter_Entity accepts ---
//
// Its `is` list is: Counting.
// No handler for a verb this type does not accept EXISTS, so calling one
// is "no matching function" rather than a runtime refusal; a declared
// handler nobody defined is a LINK error naming the symbol.
void add(Entity&, Counter&, const Add_Data&, input_context_t&);   // Counting, shared by every opting-in type: src/server/traits/counting.cpp
void reset(Entity&, Counter&, const Reset_Data&, input_context_t&);   // Counting, shared by every opting-in type: src/server/traits/counting.cpp

// --- what a Logic_Counter_Entity announces ---
//
// Declared in the trait headers above and defined once in the
// binder; repeated here so this file answers both halves. The
// SYSTEM that writes the state change is what calls one.
void emit_limit_reached(const Entity& sender, const Limit_Reached_Data& payload, input_context_t& context);   // Counting

} // namespace entities
