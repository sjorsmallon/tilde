// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
//
// Game_Rules_Entity: what it IS, and what it can be TOLD.
//
// The includes are relative to THIS file rather than to src/shared: a
// quoted include is resolved against the including file's directory first.
#pragma once

#include "../entities_core_generated.hpp"
#include "../traits/objective_generated.hpp"

namespace entities
{

struct Game_Rules_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Game_Rules_Entity;

  Game_Rules_Entity() { type = entity_type::Game_Rules_Entity; }

};

// The entity pool is a byte buffer: it copies with memcpy and runs no
// destructor. A field that breaks either of these corrupts or leaks
// silently, so the check lives here rather than in a test nobody runs
// before the pool does.
static_assert(std::is_trivially_copyable_v<Game_Rules_Entity>,
              "Game_Rules_Entity must stay trivially copyable: pooled storage, snapshot "
              "baselines and undo all copy entities with memcpy");
static_assert(std::is_trivially_destructible_v<Game_Rules_Entity>,
              "Game_Rules_Entity must stay trivially destructible: the entity pool frees a "
              "slot by overwriting it and runs no destructor");
static_assert(std::is_base_of_v<Entity, Game_Rules_Entity>,
              "Game_Rules_Entity must derive from Entity: the generated tables hand out "
              "Entity* for every entity type");

// --- what a Game_Rules_Entity accepts ---
//
// Its `is` list is: Objective.
// No handler for a verb this type does not accept EXISTS, so calling one
// is "no matching function" rather than a runtime refusal; a declared
// handler nobody defined is a LINK error naming the symbol.
void complete_level(Game_Rules_Entity&, const Complete_Level_Data&, input_context_t&);   // Objective, this type's own: src/server/entities/game_rules_entity.cpp

} // namespace entities
