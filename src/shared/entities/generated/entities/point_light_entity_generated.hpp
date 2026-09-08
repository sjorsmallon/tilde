// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
//
// Point_Light_Entity: what it IS, and what it can be TOLD.
//
// The includes are relative to THIS file rather than to src/shared: a
// quoted include is resolved against the including file's directory first.
#pragma once

#include "../entities_core_generated.hpp"
#include "../traits/colorable_generated.hpp"
#include "../traits/switchable_generated.hpp"

namespace entities
{

struct Point_Light_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Point_Light_Entity;

  Point_Light_Entity() { type = entity_type::Point_Light_Entity; }

  Enabled switch_state = {};
  Light light = {};
  float range = 256.0f;
};

// The entity pool is a byte buffer: it copies with memcpy and runs no
// destructor. A field that breaks either of these corrupts or leaks
// silently, so the check lives here rather than in a test nobody runs
// before the pool does.
static_assert(std::is_trivially_copyable_v<Point_Light_Entity>,
              "Point_Light_Entity must stay trivially copyable: pooled storage, snapshot "
              "baselines and undo all copy entities with memcpy");
static_assert(std::is_trivially_destructible_v<Point_Light_Entity>,
              "Point_Light_Entity must stay trivially destructible: the entity pool frees a "
              "slot by overwriting it and runs no destructor");
static_assert(std::is_base_of_v<Entity, Point_Light_Entity>,
              "Point_Light_Entity must derive from Entity: the generated tables hand out "
              "Entity* for every entity type");

// --- what a Point_Light_Entity accepts ---
//
// Its `is` list is: Colorable, Switchable.
// No handler for a verb this type does not accept EXISTS, so calling one
// is "no matching function" rather than a runtime refusal; a declared
// handler nobody defined is a LINK error naming the symbol.
void set_color(Point_Light_Entity&, const Set_Color_Data&, input_context_t&);   // Colorable, this type's own: src/server/entities/point_light_entity.cpp
void enable(Entity&, Enabled&, const Enable_Data&, input_context_t&);   // Switchable, shared by every opting-in type: src/server/traits/switchable.cpp
void disable(Entity&, Enabled&, const Disable_Data&, input_context_t&);   // Switchable, shared by every opting-in type: src/server/traits/switchable.cpp
void toggle_enabled(Entity&, Enabled&, const Toggle_Enabled_Data&, input_context_t&);   // Switchable, shared by every opting-in type: src/server/traits/switchable.cpp

// --- what a Point_Light_Entity announces ---
//
// Declared in the trait headers above and defined once in the
// binder; repeated here so this file answers both halves. The
// SYSTEM that writes the state change is what calls one.
void emit_color_changed(const Entity& sender, const Color_Changed_Data& payload, input_context_t& context);   // Colorable

} // namespace entities
