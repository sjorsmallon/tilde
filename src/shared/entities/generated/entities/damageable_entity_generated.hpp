// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
//
// Damageable_Entity: what it IS, and what it can be TOLD.
//
// The includes are relative to THIS file rather than to src/shared: a
// quoted include is resolved against the including file's directory first.
#pragma once

#include "../entities_core_generated.hpp"
#include "../traits/mortal_generated.hpp"

namespace entities
{

struct Damageable_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Damageable_Entity;

  Damageable_Entity() { type = entity_type::Damageable_Entity; }

  Health health = {};
  linalg::vec3f hitbox_half_extents = {16.0f, 32.0f, 16.0f};
  Damage_Type weakness = Damage_Type::Orange;
  Render render = {};
};

// The entity pool is a byte buffer: it copies with memcpy and runs no
// destructor. A field that breaks either of these corrupts or leaks
// silently, so the check lives here rather than in a test nobody runs
// before the pool does.
static_assert(std::is_trivially_copyable_v<Damageable_Entity>,
              "Damageable_Entity must stay trivially copyable: pooled storage, snapshot "
              "baselines and undo all copy entities with memcpy");
static_assert(std::is_trivially_destructible_v<Damageable_Entity>,
              "Damageable_Entity must stay trivially destructible: the entity pool frees a "
              "slot by overwriting it and runs no destructor");
static_assert(std::is_base_of_v<Entity, Damageable_Entity>,
              "Damageable_Entity must derive from Entity: the generated tables hand out "
              "Entity* for every entity type");

// --- what a Damageable_Entity accepts ---
//
// Its `is` list is: Mortal.
// No handler for a verb this type does not accept EXISTS, so calling one
// is "no matching function" rather than a runtime refusal; a declared
// handler nobody defined is a LINK error naming the symbol.
void kill(Entity&, Health&, const Kill_Data&, input_context_t&);   // Mortal, shared by every opting-in type: src/server/traits/mortal.cpp
void set_health(Entity&, Health&, const Set_Health_Data&, input_context_t&);   // Mortal, shared by every opting-in type: src/server/traits/mortal.cpp
void damage(Entity&, Health&, const Damage_Data&, input_context_t&);   // Mortal, shared by every opting-in type: src/server/traits/mortal.cpp

// --- what a Damageable_Entity announces ---
//
// Declared in the trait headers above and defined once in the
// binder; repeated here so this file answers both halves. The
// SYSTEM that writes the state change is what calls one.
void emit_died(const Entity& sender, const Died_Data& payload, input_context_t& context);   // Mortal
void emit_health_changed(const Entity& sender, const Health_Changed_Data& payload, input_context_t& context);   // Mortal

} // namespace entities
