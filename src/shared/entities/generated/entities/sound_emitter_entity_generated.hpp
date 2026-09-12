// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
//
// Sound_Emitter_Entity: what it IS, and what it can be TOLD.
//
// The includes are relative to THIS file rather than to src/shared: a
// quoted include is resolved against the including file's directory first.
#pragma once

#include "../entities_core_generated.hpp"
#include "../traits/switchable_generated.hpp"
#include "../traits/playable_generated.hpp"

namespace entities
{

struct Sound_Emitter_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Sound_Emitter_Entity;

  Sound_Emitter_Entity() { type = entity_type::Sound_Emitter_Entity; }

  Enabled switch_state = {};
  Playback playback = {};
  assets::sound_asset sound = {};
  float volume = 1.0f;
  bool loop = false;
  bool spatial = true;
  float range = 1024.0f;
};

// The entity pool is a byte buffer: it copies with memcpy and runs no
// destructor. A field that breaks either of these corrupts or leaks
// silently, so the check lives here rather than in a test nobody runs
// before the pool does.
static_assert(std::is_trivially_copyable_v<Sound_Emitter_Entity>,
              "Sound_Emitter_Entity must stay trivially copyable: pooled storage, snapshot "
              "baselines and undo all copy entities with memcpy");
static_assert(std::is_trivially_destructible_v<Sound_Emitter_Entity>,
              "Sound_Emitter_Entity must stay trivially destructible: the entity pool frees a "
              "slot by overwriting it and runs no destructor");
static_assert(std::is_base_of_v<Entity, Sound_Emitter_Entity>,
              "Sound_Emitter_Entity must derive from Entity: the generated tables hand out "
              "Entity* for every entity type");

// --- what a Sound_Emitter_Entity accepts ---
//
// Its `is` list is: Switchable, Playable.
// No handler for a verb this type does not accept EXISTS, so calling one
// is "no matching function" rather than a runtime refusal; a declared
// handler nobody defined is a LINK error naming the symbol.
void enable(Entity&, Enabled&, const Enable_Data&, input_context_t&);   // Switchable, shared by every opting-in type: src/server/traits/switchable.cpp
void disable(Entity&, Enabled&, const Disable_Data&, input_context_t&);   // Switchable, shared by every opting-in type: src/server/traits/switchable.cpp
void toggle_enabled(Entity&, Enabled&, const Toggle_Enabled_Data&, input_context_t&);   // Switchable, shared by every opting-in type: src/server/traits/switchable.cpp
void play(Entity&, Playback&, const Play_Data&, input_context_t&);   // Playable, shared by every opting-in type: src/server/traits/playable.cpp

} // namespace entities
