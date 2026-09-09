// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
//
// Player_Entity: what it IS, and what it can be TOLD.
//
// The includes are relative to THIS file rather than to src/shared: a
// quoted include is resolved against the including file's directory first.
#pragma once

#include "../entities_core_generated.hpp"
#include "../traits/mortal_generated.hpp"
#include "../traits/mobile_generated.hpp"
#include "../traits/armable_generated.hpp"
#include "../traits/respawnable_generated.hpp"

namespace entities
{

struct Player_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Player_Entity;

  Player_Entity() { type = entity_type::Player_Entity; }

  float view_angle_yaw = {};
  float view_angle_pitch = {};
  float body_yaw = {};
  Health health = {};
  uint32_t death_tick = {};
  uint32_t last_fire_tick = {};
  Weapon last_fire_weapon = Weapon::Knife;
  uint64_t reload_complete_time = {};
  uint32_t last_empty_fire_warning_tick = {};
  uint32_t checkpoint_uid = {};
  uint32_t last_hit_tick = {};
  bool last_hit_was_headshot = {};
  int32_t client_slot_index = {};
  network::pascal_string_t<32> display_name = {};
  int32_t kills = {};
  int32_t deaths = {};
  linalg::vec3f velocity = {};
  Inventory inventory = {};
  Movement movement = {};
  Render render = {.mesh = assets::mesh_asset::Leet_Full};
  Team_Allegiance team_allegiance = Team_Allegiance::Free_For_All;
};

// The entity pool is a byte buffer: it copies with memcpy and runs no
// destructor. A field that breaks either of these corrupts or leaks
// silently, so the check lives here rather than in a test nobody runs
// before the pool does.
static_assert(std::is_trivially_copyable_v<Player_Entity>,
              "Player_Entity must stay trivially copyable: pooled storage, snapshot "
              "baselines and undo all copy entities with memcpy");
static_assert(std::is_trivially_destructible_v<Player_Entity>,
              "Player_Entity must stay trivially destructible: the entity pool frees a "
              "slot by overwriting it and runs no destructor");
static_assert(std::is_base_of_v<Entity, Player_Entity>,
              "Player_Entity must derive from Entity: the generated tables hand out "
              "Entity* for every entity type");

// --- what a Player_Entity accepts ---
//
// Its `is` list is: Mortal, Mobile, Armable, Respawnable.
// No handler for a verb this type does not accept EXISTS, so calling one
// is "no matching function" rather than a runtime refusal; a declared
// handler nobody defined is a LINK error naming the symbol.
void kill(Entity&, Health&, const Kill_Data&, input_context_t&);   // Mortal, shared by every opting-in type: src/server/traits/mortal.cpp
void set_health(Entity&, Health&, const Set_Health_Data&, input_context_t&);   // Mortal, shared by every opting-in type: src/server/traits/mortal.cpp
void damage(Entity&, Health&, const Damage_Data&, input_context_t&);   // Mortal, shared by every opting-in type: src/server/traits/mortal.cpp
void teleport(Player_Entity&, const Teleport_Data&, input_context_t&);   // Mobile, this type's own: src/server/entities/player_entity.cpp
void set_velocity(Player_Entity&, const Set_Velocity_Data&, input_context_t&);   // Mobile, this type's own: src/server/entities/player_entity.cpp
void add_velocity(Player_Entity&, const Add_Velocity_Data&, input_context_t&);   // Mobile, this type's own: src/server/entities/player_entity.cpp
void grant_weapon(Entity&, Inventory&, const Grant_Weapon_Data&, input_context_t&);   // Armable, shared by every opting-in type: src/server/traits/armable.cpp
void set_respawn_point(Player_Entity&, const Set_Respawn_Point_Data&, input_context_t&);   // Respawnable, this type's own: src/server/entities/player_entity.cpp

// --- what a Player_Entity announces ---
//
// Declared in the trait headers above and defined once in the
// binder; repeated here so this file answers both halves. The
// SYSTEM that writes the state change is what calls one.
void emit_died(const Entity& sender, const Died_Data& payload, input_context_t& context);   // Mortal
void emit_health_changed(const Entity& sender, const Health_Changed_Data& payload, input_context_t& context);   // Mortal

} // namespace entities
