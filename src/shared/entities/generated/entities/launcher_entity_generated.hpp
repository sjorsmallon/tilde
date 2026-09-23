// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
//
// Launcher_Entity: what it IS, and what it can be TOLD.
//
// The includes are relative to THIS file rather than to src/shared: a
// quoted include is resolved against the including file's directory first.
#pragma once

#include "../entities_core_generated.hpp"
#include "../traits/switchable_generated.hpp"
#include "../traits/firing_generated.hpp"

namespace entities
{

struct Launcher_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Launcher_Entity;

  Launcher_Entity() { type = entity_type::Launcher_Entity; }

  Enabled switch_state = {};
  Weapon weapon = Weapon::Bubble;
  Fire_Trigger trigger = Fire_Trigger::Primary;
  float spread_yaw_degrees = 0.0f;
  float spread_pitch_degrees = 0.0f;
  float speed_variation = 0.0f;
  float flight_seconds_variation = 0.0f;
  float rest_seconds_variation = 0.0f;
  uint32_t shots_fired = {};
  Render render = {.mesh = assets::mesh_asset::Box, .scale = {16.0f, 16.0f, 16.0f}};
};

// The entity pool is a byte buffer: it copies with memcpy and runs no
// destructor. A field that breaks either of these corrupts or leaks
// silently, so the check lives here rather than in a test nobody runs
// before the pool does.
static_assert(std::is_trivially_copyable_v<Launcher_Entity>,
              "Launcher_Entity must stay trivially copyable: pooled storage, snapshot "
              "baselines and undo all copy entities with memcpy");
static_assert(std::is_trivially_destructible_v<Launcher_Entity>,
              "Launcher_Entity must stay trivially destructible: the entity pool frees a "
              "slot by overwriting it and runs no destructor");
static_assert(std::is_base_of_v<Entity, Launcher_Entity>,
              "Launcher_Entity must derive from Entity: the generated tables hand out "
              "Entity* for every entity type");

// --- what a Launcher_Entity accepts ---
//
// Its `is` list is: Switchable, Firing.
// No handler for a verb this type does not accept EXISTS, so calling one
// is "no matching function" rather than a runtime refusal; a declared
// handler nobody defined is a LINK error naming the symbol.
void enable(Entity&, Enabled&, const Enable_Data&, input_context_t&);   // Switchable, shared by every opting-in type: src/server/traits/switchable.cpp
void disable(Entity&, Enabled&, const Disable_Data&, input_context_t&);   // Switchable, shared by every opting-in type: src/server/traits/switchable.cpp
void toggle_enabled(Entity&, Enabled&, const Toggle_Enabled_Data&, input_context_t&);   // Switchable, shared by every opting-in type: src/server/traits/switchable.cpp
void fire(Launcher_Entity&, const Fire_Data&, input_context_t&);   // Firing, this type's own: src/server/entities/launcher_entity.cpp

} // namespace entities
