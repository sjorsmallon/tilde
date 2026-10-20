#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>

#include "array.hpp"
#include "assets/asset_id.hpp"
#include "color_map.hpp"
#include "contact.hpp"
#include "entities/generated/entities_core_generated.hpp"
#include "collision/flight_path.hpp"
#include "movement/player_constants.hpp"
#include "movement/player_move.hpp"
#include "subtick.hpp"

namespace shared
{

struct self_impulse_t
{
  impulse_mode_t mode;
  float along_aim_speed;
  float upward_speed;
};


struct hitscan_t
{
  float range;
  // whether it leaves a decal.
  bool  leaves_bullet_impact;
};

// What a flight that runs out of lifetime touching nothing does: an airburst is a contact with
// no surface, and the row acts from wherever the shot was.
enum class projectile_expiration_t : uint8_t
{
  Vanishes,
  Arrives
};

struct projectile_t
{
  float speed;
  // Fraction of g_gravity acting on the projectile: 0 flies straight, 1 falls like a player.
  float gravity_scale;
  // The entity type a shot becomes; it must carry the Projectile component.
  entities::entity_type spawns;
  projectile_expiration_t   expiry;
};

struct projectile_step_t
{
  vec3f position;
  vec3f velocity;
};

// The closed form of constant acceleration, so advancing by dt twice is advancing by 2dt once.
inline projectile_step_t advance_projectile(const projectile_t& projectile, float gravity,
                                            vec3f position, vec3f velocity, float dt)
{
  const vec3f acceleration = {0.f, -gravity * projectile.gravity_scale, 0.f};
  return projectile_step_t{
    .position = position + velocity * dt + acceleration * (0.5f * dt * dt),
    .velocity = velocity + acceleration * dt
  };
}

// originate an entity from this position for projectiles.
enum class place_anchor_t : uint8_t
{
  Feet,
  Waist,
  Eye,
};

// Fire_Resolution::Place's parameters: the type set down at the anchor with no flight. The type's own
// system does whatever it does from there.
struct place_t
{
  entities::entity_type spawns;
  place_anchor_t anchor;
};

// What a piloted flight leaves behind when it ends.
enum class pilot_leaves_t : uint8_t
{
  Nothing,
  // A polyline of platforms under the rocket's line (flight_path.hpp), one path per pilot.
  Path,
};

// Fire_Resolution::Lens_View's parameters: the lens the owner holds to the eye while the view is on, a lens on
// every line of sight (shared/lighting/lens.hpp). The tonemap keeps only the ink lines, on a page of its colour.
struct lens_view_t
{
  entities::Lens_Filter filter;
};

// Fire_Resolution::Pilot's parameters: the rocket the shooter rides, written into Movement at the press.
struct pilot_t
{
  float speed;
  float radius;
  float seconds;
  pilot_leaves_t         leaves;
  flight_path_settings_t path;
};

constexpr bool flight_path_settings_are_zero(const flight_path_settings_t& path)
{
  return path.turn_degrees == 0.f && path.shortest_segment == 0.f && path.longest_segment == 0.f &&
         path.joint_overlap == 0.f && path.drop == 0.f && path.half_width == 0.f &&
         path.solid_seconds == 0.f && path.colors.empty();
}

constexpr bool pilot_path_matches_what_it_leaves(const pilot_t& pilot)
{
  switch (pilot.leaves)
  {
  case pilot_leaves_t::Nothing:
    return flight_path_settings_are_zero(pilot.path);
  case pilot_leaves_t::Path:
    return pilot.path.turn_degrees > 0.f && pilot.path.shortest_segment > 0.f &&
           pilot.path.longest_segment > pilot.path.shortest_segment &&
           pilot.path.joint_overlap >= 0.f && pilot.path.half_width > 0.f &&
           pilot.path.solid_seconds > 0.f && !pilot.path.colors.empty();
  }
  return false;
}

// What a button does when its owner already has max_alive of its shots alive.
enum class at_limit_t : uint8_t
{
  Replace_Oldest,
};

// How many shots off ONE button one carrier may have alive at once, landed ones included. 0 is no limit.
struct alive_limit_t
{
  uint32_t   max_alive;
  at_limit_t at_limit;
};

// What the weapon sounds like. Client-only facts, on the shared row (see
// above). Names rather than ids, because this table is constexpr and an id is
// numbered at startup; the client resolves them at the play site. The empty
// name is a declared absence, resolved to Missing and logged once per id by
// the audio system, never a silent skip.
struct weapon_sounds_t
{
  assets::asset_name_t fire;
  // A Shot_Impact on static geometry. Scout has no bullet-on-wall file on disk.
  assets::asset_name_t world_impact;
};

// What ONE button does: the resolution and the parameters it reads, and what
// the shot does when it ARRIVES. Both buttons are this shape, so a secondary
// hitscan or a secondary projectile is the same arm of the same switch as a
// primary one.
struct weapon_fire_t
{
  entities::Fire_Resolution resolution;
  hitscan_t                 hitscan;
  projectile_t              projectile;
  place_t                   place;
  self_impulse_t            self_impulse;
  pilot_t                   pilot;
  lens_view_t               lens_view;
  // What a Hitscan or a sweeping Projectile does on contact (update_contacts).
  // None on a button whose shot never arrives anywhere: a fixed-arc projectile,
  // a place, an impulse.
  contact_t                 contact;
  // A button that stays down repeats at fire_interval_seconds (try_find_held_fire_time).
  bool                      fires_while_held;
};

struct weapon_definition_t
{
  // Which weapon this row is for. Present so the row can be checked against
  // its own index (see the static_assert below) rather than trusting that
  // whoever last edited this list counted correctly.
  entities::Weapon         weapon;
  const char*              display_name;
  // WHERE this weapon is held when granted. A property of the weapon, so
  // "a scout is a primary" is written once here rather than at every site that
  // hands one out -- which is what stopped the inventory needing to be keyed by
  // weapon identity at all (generalization_def.md §1).
  //
  // Two weapons naming one slot is legal and is what a pickup does: the second
  // one granted replaces the first. Nothing checks for uniqueness, deliberately
  // -- an Enum_Array of slots with two rifles competing for Primary is exactly
  // the loadout question, not a table error.
  entities::Inventory_Slot slot;

  // --- The clocks. Hitscan and Projectile run through them, on either button. ---
  float   fire_interval_seconds;
  // How long after this weapon is RAISED before anything may fire. A property
  // of the weapon, but the gate it feeds is the PLAYER's
  // (Inventory::deploy_complete_time): it blocks every weapon at once, which is
  // what makes a quick switch cost something. The weapon's own
  // fire_interval_seconds keeps running while holstered and is a separate
  // clock -- folding the two together is the bug this file's history is about.
  //
  // The NUMBER IS AUTHORED HERE, not derived from an animation length. Source 1
  // took it from SequenceDuration(), which meant re-exporting a weapon's anims
  // silently reshuffled its timings; Source 2 moving weapon timings into .vdata
  // is Valve walking that back. A future draw animation is authored against
  // this, not the reverse.
  float   deploy_duration_seconds;
  // 0 means NO MAGAZINE: the weapon never consumes ammo and never reloads, and
  // the reload edge is a no-op on it. That is the knife, and it is a real
  // state rather than an unfilled row -- the static_assert below catches the
  // unfilled row, since a zeroed tail row is not at its own index.
  int32_t magazine_size;
  float   reload_duration_seconds;
  // The magazine IS how many shots the carrier may have out: the MAP under their feet gives back the ones that are gone, and no reload does.
  bool    refills_on_ground;
  // magazine_size and limit.max_alive are what a weapon of this kind is BORN with; the one a shot reads is the Weapon_Entity's.
  alive_limit_t limit;

  // The left and the right mouse button. The switch in resolve_player_shot is
  // over one of these and reads exactly one of its three structs. Zoom is a
  // secondary only: the client toggles its FOV off it and nothing else.
  weapon_fire_t primary_fire;
  weapon_fire_t secondary_fire;
  // Seconds before EITHER impulse, or a pilot's next flight, may be taken again. THE gate -- see
  // try_apply_self_impulse and the static_assert below for why it is not
  // fire_interval_seconds. Shared by both buttons, because Movement carries one
  // countdown and a second one is a second thing the replay has to restart.
  float         self_impulse_cooldown_seconds;

  weapon_sounds_t sounds;
};

// Indexed by entities::Weapon, so entry N is the weapon whose enum value is N.
// The rows and the static_asserts that guard them live in weapons.cpp.
extern const Enum_Array<entities::Weapon, weapon_definition_t> WEAPON_DEFINITIONS;

inline const weapon_definition_t& get_weapon_definition(entities::Weapon id)
{
  assert(static_cast<uint32_t>(id) < WEAPON_DEFINITIONS.size() &&
         "get_weapon_definition on an id with no table entry");
  return WEAPON_DEFINITIONS[id];
}

// The half of the row a button reads.
constexpr const weapon_fire_t& get_weapon_fire_for_button(const weapon_definition_t& weapon,
                                       entities::Fire_Trigger trigger)
{
  switch (trigger)
  {
  case entities::Fire_Trigger::Primary:   return weapon.primary_fire;
  case entities::Fire_Trigger::Secondary: return weapon.secondary_fire;
  }
  return weapon.primary_fire;
}

// When a button that was ALREADY down fires inside [step_start, step_end): the moment both clocks open, never rounded to the step.
[[nodiscard]] constexpr std::optional<subtick_time_t>
try_find_held_fire_time(const weapon_fire_t& fire, subtick_time_t next_fire_time,
                        subtick_time_t deploy_complete_time, subtick_time_t step_start,
                        subtick_time_t step_end)
{
  if (!fire.fires_while_held)
    return std::nullopt;

  const subtick_time_t fire_time = std::max({step_start, next_fire_time, deploy_complete_time});
  if (fire_time >= step_end)
    return std::nullopt;
  return fire_time;
}

// Negative ammo or reserve is UNLIMITED; every count is per weapon INSTANCE and authored in the map.
inline constexpr int32_t UNLIMITED_AMMO = -1;

constexpr int32_t get_full_magazine_ammo(const weapon_definition_t& weapon)
{
  return weapon.magazine_size > 0 ? weapon.magazine_size : UNLIMITED_AMMO;
}

constexpr bool ammo_allows_a_shot(int32_t ammo)
{
  return ammo != 0;
}

// One weapon's three counts, read off its Weapon_Entity.
struct magazine_t
{
  int32_t size;
  int32_t ammo;
  int32_t reserve_ammo;
};

constexpr bool reload_may_start(const weapon_definition_t& weapon, const magazine_t& magazine)
{
  return !weapon.refills_on_ground && magazine.size > 0 && magazine.ammo >= 0 &&
         magazine.ammo < magazine.size && magazine.reserve_ammo != 0;
}

constexpr magazine_t reloaded_magazine(const weapon_definition_t& weapon, magazine_t magazine)
{
  if (!reload_may_start(weapon, magazine))
    return magazine;

  const int32_t missing = magazine.size - magazine.ammo;
  if (magazine.reserve_ammo < 0)
    return {.size = magazine.size, .ammo = magazine.size, .reserve_ammo = magazine.reserve_ammo};

  const int32_t moved = missing < magazine.reserve_ammo ? missing : magazine.reserve_ammo;
  return {.size         = magazine.size,
          .ammo         = magazine.ammo + moved,
          .reserve_ammo = magazine.reserve_ammo - moved};
}

// The ground gives back the shots that are GONE: it fills to the size less the carrier's shots still alive.
// An unlimited magazine stays unlimited and a fuller one stays fuller: the ground only ever raises.
constexpr int32_t ammo_after_ground_refill(const weapon_definition_t& weapon, const magazine_t& magazine,
                                           int32_t alive_shot_count)
{
  if (!weapon.refills_on_ground || magazine.ammo < 0)
    return magazine.ammo;
  return std::max(magazine.ammo, magazine.size - alive_shot_count);
}

// How a live projectile flies: the row that fired it, the button it came off.
inline const projectile_t& get_projectile_parameters(const entities::Projectile& projectile)
{
  return get_weapon_fire_for_button(get_weapon_definition(projectile.weapon_id), projectile.trigger).projectile;
}

// ---------------------------------------------------------------------------
// Self-impulse
// ---------------------------------------------------------------------------
//
// Push the shooter along their own aim. THE shared half of
// Fire_Resolution::Self_Impulse, and it has to be shared: this is the one
// resolution whose outcome lands on the player who pressed the button, so the
// client predicts it and the server simulates it, and a second spelling of the
// arithmetic is a rubber-band on every dash.
//
// Three callers, all inside a sub-tick step loop at the trigger edge: the
// server's resolve_player_shot, the client's live prediction, and the client's
// reconciliation replay. The replay is why every input and output here is
// either the weapon's own table row or Movement -- there is nothing else a
// replay holds.
//
// `aim_direction` must be normalized; it is the step's view direction, so the
// dash goes where the player was looking at the press rather than wherever the
// mouse finished the tick.
//
// The TRIGGER picks the row half, primary_fire or secondary_fire. A button
// whose half is not an impulse is refused here rather than by the
// caller, which is what lets every site call this off whatever is in the hand.
[[nodiscard]] inline bool try_apply_self_impulse(const movement_settings_t& settings,
                                                 const weapon_definition_t& weapon,
                                                 entities::Fire_Trigger trigger,
                                                 const vec3f& aim_direction,
                                                 entities::Movement& movement,
                                                 vec3f& velocity)
{
  const weapon_fire_t& fire = get_weapon_fire_for_button(weapon, trigger);
  if (fire.resolution != entities::Fire_Resolution::Self_Impulse)
    return false;
  const self_impulse_t* impulse = &fire.self_impulse;

  if (movement.seconds_until_impulse_ready > 0.f)
    return false;
  
  // The row states WHAT the press wants and the model decides where it lands.
  // The vertical half is the one that differs from the horizontal: an Add
  // dash REPLACES a fall (max(vy, 0) + lift) rather than summing with it.
  vec3f wanted = aim_direction * impulse->along_aim_speed;
  switch (impulse->mode)
  {
  case impulse_mode_t::Keep:
    break;

  case impulse_mode_t::Add:
  {
    impulse_t added{.horizontal = impulse_mode_t::Add,
                    .vertical   = impulse_mode_t::Add,
                    .velocity   = wanted};
    if (impulse->upward_speed > 0.f)
    {
      added.vertical   = impulse_mode_t::Set;
      added.velocity.y = std::max(velocity.y + wanted.y, 0.f) + impulse->upward_speed;
    }
    apply_impulse(settings, velocity, movement, added);
    break;
  }

  case impulse_mode_t::Set:
    wanted.y += impulse->upward_speed;
    apply_impulse(settings, velocity, movement,
                  {.horizontal = impulse_mode_t::Set,
                   .vertical   = impulse_mode_t::Set,
                   .velocity   = wanted});
    break;
  }

  movement.seconds_until_impulse_ready = weapon.self_impulse_cooldown_seconds;
  return true;
}

// ---------------------------------------------------------------------------
// Pilot
// ---------------------------------------------------------------------------

// The press that launches: the self-impulse's three callers and its one gate.
[[nodiscard]] inline bool try_begin_pilot_flight(const weapon_definition_t& weapon,
                                                 entities::Fire_Trigger trigger, const vec3f& eye,
                                                 entities::Movement& movement)
{
  const weapon_fire_t& fire = get_weapon_fire_for_button(weapon, trigger);
  if (fire.resolution != entities::Fire_Resolution::Pilot)
    return false;

  if (movement.active_override != entities::Movement_Override::None)
    return false;

  if (movement.seconds_until_impulse_ready > 0.f)
    return false;

  movement.active_override            = entities::Movement_Override::Pilot;
  movement.override_target_uid        = null_entity_uid;
  movement.override_target_position   = eye;
  movement.override_seconds_remaining = fire.pilot.seconds;
  movement.override_speed             = fire.pilot.speed;
  movement.override_radius            = fire.pilot.radius;

  movement.seconds_until_impulse_ready = weapon.self_impulse_cooldown_seconds;
  return true;
}

// The row half a flight was launched off: the primary when both are pilots, nothing when neither is.
[[nodiscard]] constexpr const pilot_t* try_find_pilot_for_weapon(const weapon_definition_t& weapon)
{
  for (const weapon_fire_t* fire : {&weapon.primary_fire, &weapon.secondary_fire})
    if (fire->resolution == entities::Fire_Resolution::Pilot)
      return &fire->pilot;
  return nullptr;
}

// Any trigger press while a flight is live lets go of it, whatever is in the hand; the next step ends it.
[[nodiscard]] inline bool try_end_pilot_flight(entities::Movement& movement)
{
  if (movement.active_override != entities::Movement_Override::Pilot)
    return false;

  movement.override_seconds_remaining = 0.f;
  return true;
}

// ---------------------------------------------------------------------------
// Which key equips what
// ---------------------------------------------------------------------------
//
// SHARED because both sides run it, and they must run the same one: the server
// applies the switch in its step loop and the client predicts the deploy clock
// off that same edge. Spelled out twice, "Key1 means Primary" would be two
// answers to one question, and the failure is a client counting down a deploy
// for a weapon the server never raised.
//
// A key selects a SLOT, not a weapon type, and that is the same correction the
// inventory itself took (generalization_def.md §1): which key equips what is a
// statement about the hand, not about the gun. Binding to a weapon type made an
// EMPTY slot unrepresentable -- Key1 meant "equip the scout" and had nothing to
// say about a player who is not carrying one, so a hand with a rifle in it
// could only be reached by adding a rifle row. Now Key1 means "hold whatever is
// in Primary", which is also the only spelling a pickup can work with.
//
// It is NOT keyed by the Inventory_Slot enum, and deliberately so: which key
// equips what is a binding, and the two orders are independent. A slot with no
// key is a missing row rather than a hole in an Enum_Array.
struct weapon_select_binding_t
{
  uint64_t                 button;
  entities::Inventory_Slot slot;
};

inline constexpr Array<weapon_select_binding_t, 5> WEAPON_SELECT_BINDINGS = {{
    {Button::Key1, entities::Inventory_Slot::Primary},
    {Button::Key2, entities::Inventory_Slot::Secondary},
    {Button::Key3, entities::Inventory_Slot::Melee},
    {Button::Key4, entities::Inventory_Slot::Utility_1},
    {Button::Key5, entities::Inventory_Slot::Utility_2},
}};

// The inventory slot a step's newly-pressed buttons equip, or nothing when none
// of them is bound to one. Number keys with no binding land here as nothing,
// which is what stops an unbound Key6 cancelling a reload on one side and not
// the other.
//
// Several weapon keys inside ONE SUB-TICK slot are simultaneous at that
// resolution, so which wins is arbitrary and only has to be fixed: the last row
// that matches, which is the order the server's chain of ifs already resolved
// in. (Two senses of "slot" meet in that sentence and they are unrelated -- one
// is a moment inside a tick, the other is a place in the hand.)
//
// Selecting an EMPTY slot is legal and is a real switch: the hand comes up
// holding nothing, the deploy clock runs, and the fire path finds no weapon.
// Refusing it here instead would make "what is in Primary" a question this
// function cannot see the answer to -- it has no player -- and the two sides
// would have to agree about it separately.
[[nodiscard]] constexpr std::optional<entities::Inventory_Slot>
try_slot_selected_by(uint64_t pressed_buttons)
{
  std::optional<entities::Inventory_Slot> selected;
  for (const weapon_select_binding_t& binding : WEAPON_SELECT_BINDINGS)
    if (pressed_buttons & binding.button)
      selected = binding.slot;

  return selected;
}

} // namespace shared
