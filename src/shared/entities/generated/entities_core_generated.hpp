// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
//
// The entity family's CORE: the declared enums, the components and
// the base every entity derives from. Everything ONE entity's struct
// is built out of, and nothing that spans the set -- the tables and
// the per-type headers are in entities_generated.hpp beside this one.
#pragma once

#include "array.hpp"
#include "entity_uid.hpp"
#include "linalg.hpp"
#include "network/network_types.hpp"
#include "reflection.hpp"
#include "span.hpp"
#include "assets/generated/assets_generated.hpp"
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

namespace entities
{

template <typename T> std::optional<T> try_from_string(std::string_view text);

// The component list a trait's `requires` names -- a type list, expanded
// by entities_with_trait<Trait>() into the row it hands back.
template <typename... Component_T> struct component_list_t {};

// Every enum below is DENSE and starts at 0, so its _COUNT is both the
// number of declared names and one past the largest value -- which is
// what makes it safe as an array size. The DSL has no explicit or
// sparse enum values today; the day it grows them, every _COUNT user
// has to be revisited.

enum class Spawn_Type : uint8_t
{
  Human = 0,
  Bot = 1,
};

constexpr uint32_t Spawn_Type_COUNT = 2;

const char* to_string(Spawn_Type value);
template <> std::optional<Spawn_Type> try_from_string<Spawn_Type>(std::string_view text);

enum class Team_Allegiance : uint8_t
{
  Red = 0,
  Blu = 1,
  Free_For_All = 2,
};

constexpr uint32_t Team_Allegiance_COUNT = 3;

const char* to_string(Team_Allegiance value);
template <> std::optional<Team_Allegiance> try_from_string<Team_Allegiance>(std::string_view text);

enum class Weapon : uint8_t
{
  Knife = 0,
  Scout = 1,
  Rocket_Launcher = 2,
  Dash = 3,
  Swapper = 4,
  Hook = 5,
  Bubble = 6,
};

constexpr uint32_t Weapon_COUNT = 7;

const char* to_string(Weapon value);
template <> std::optional<Weapon> try_from_string<Weapon>(std::string_view text);

enum class Fire_Resolution : uint8_t
{
  Hitscan = 0,
  Projectile = 1,
  Self_Impulse = 2,
};

constexpr uint32_t Fire_Resolution_COUNT = 3;

const char* to_string(Fire_Resolution value);
template <> std::optional<Fire_Resolution> try_from_string<Fire_Resolution>(std::string_view text);

enum class Inventory_Slot : uint8_t
{
  Primary = 0,
  Secondary = 1,
  Melee = 2,
  Utility_1 = 3,
  Utility_2 = 4,
};

constexpr uint32_t Inventory_Slot_COUNT = 5;

const char* to_string(Inventory_Slot value);
template <> std::optional<Inventory_Slot> try_from_string<Inventory_Slot>(std::string_view text);

enum class Damage_Type : uint8_t
{
  Normal = 0,
  Orange = 1,
  Teal = 2,
};

constexpr uint32_t Damage_Type_COUNT = 3;

const char* to_string(Damage_Type value);
template <> std::optional<Damage_Type> try_from_string<Damage_Type>(std::string_view text);

enum class Shader_Type : uint8_t
{
  Lit = 0,
  Unlit = 1,
};

constexpr uint32_t Shader_Type_COUNT = 2;

const char* to_string(Shader_Type value);
template <> std::optional<Shader_Type> try_from_string<Shader_Type>(std::string_view text);

enum class Shape_Kind : uint8_t
{
  Sphere = 0,
  Box = 1,
};

constexpr uint32_t Shape_Kind_COUNT = 2;

const char* to_string(Shape_Kind value);
template <> std::optional<Shape_Kind> try_from_string<Shape_Kind>(std::string_view text);

enum class Light_Mode : uint8_t
{
  Baked = 0,
  Mixed = 1,
  Dynamic = 2,
};

constexpr uint32_t Light_Mode_COUNT = 3;

const char* to_string(Light_Mode value);
template <> std::optional<Light_Mode> try_from_string<Light_Mode>(std::string_view text);

enum class Aim_Pose : uint8_t
{
  Forward = 0,
  Upward = 1,
  Downward = 2,
  Left = 3,
  Right = 4,
};

constexpr uint32_t Aim_Pose_COUNT = 5;

const char* to_string(Aim_Pose value);
template <> std::optional<Aim_Pose> try_from_string<Aim_Pose>(std::string_view text);

enum class Round_Phase : uint8_t
{
  Warmup = 0,
  Countdown = 1,
  Freeze = 2,
  Live = 3,
  Round_End = 4,
  Game_Over = 5,
};

constexpr uint32_t Round_Phase_COUNT = 6;

const char* to_string(Round_Phase value);
template <> std::optional<Round_Phase> try_from_string<Round_Phase>(std::string_view text);

enum class Game_Mode : uint8_t
{
  deathmatch = 0,
  rounds = 1,
  speedrun = 2,
};

constexpr uint32_t Game_Mode_COUNT = 3;

const char* to_string(Game_Mode value);
template <> std::optional<Game_Mode> try_from_string<Game_Mode>(std::string_view text);

enum class Round_End_Reason : uint8_t
{
  None = 0,
  Timeout = 1,
  Frag_Limit = 2,
  Team_Elimination = 3,
  Objective = 4,
  Requested = 5,
};

constexpr uint32_t Round_End_Reason_COUNT = 6;

const char* to_string(Round_End_Reason value);
template <> std::optional<Round_End_Reason> try_from_string<Round_End_Reason>(std::string_view text);

enum class Match_Request : uint8_t
{
  None = 0,
  Start_Match = 1,
  End_Round = 2,
  Restart_Round = 3,
  End_Match = 4,
};

constexpr uint32_t Match_Request_COUNT = 5;

const char* to_string(Match_Request value);
template <> std::optional<Match_Request> try_from_string<Match_Request>(std::string_view text);

enum class Easing : uint8_t
{
  Linear = 0,
  Smooth = 1,
};

constexpr uint32_t Easing_COUNT = 2;

const char* to_string(Easing value);
template <> std::optional<Easing> try_from_string<Easing>(std::string_view text);

enum class Movement_Override : uint8_t
{
  None = 0,
  Reel = 1,
};

constexpr uint32_t Movement_Override_COUNT = 2;

const char* to_string(Movement_Override value);
template <> std::optional<Movement_Override> try_from_string<Movement_Override>(std::string_view text);

enum class enum_type : uint16_t
{
  Spawn_Type = 0,
  Team_Allegiance = 1,
  Weapon = 2,
  Fire_Resolution = 3,
  Inventory_Slot = 4,
  Damage_Type = 5,
  Shader_Type = 6,
  Shape_Kind = 7,
  Light_Mode = 8,
  Aim_Pose = 9,
  Round_Phase = 10,
  Game_Mode = 11,
  Round_End_Reason = 12,
  Match_Request = 13,
  Easing = 14,
  Movement_Override = 15,
};

constexpr uint32_t ENUM_TYPE_COUNT = 16;

const enum_type_info_t& enum_info(enum_type type);

extern const enum_type_info_t ENUM_INFOS[ENUM_TYPE_COUNT];

// Invalid is 0 so that zeroed memory never looks like a valid entity.
enum class entity_type : uint16_t
{
  Invalid = 0,
  Player_Spawn_Entity = 1,
  Player_Spectate_Entity = 2,
  Player_Entity = 3,
  Weapon_Entity = 4,
  Rocket_Entity = 5,
  Hook_Entity = 6,
  Bubble_Entity = 7,
  Physics_Body_Entity = 8,
  Damageable_Entity = 9,
  Particle_Emitter_Entity = 10,
  Sound_Emitter_Entity = 11,
  Point_Light_Entity = 12,
  Spot_Light_Entity = 13,
  Directional_Light_Entity = 14,
  Trigger_Volume_Entity = 15,
  Jump_Pad_Entity = 16,
  Reflection_Volume_Entity = 17,
  Game_Rules_Entity = 18,
  Logic_Counter_Entity = 19,
  Geometry_Owner_Entity = 20,
  Ping_Marker_Entity = 21,
  Logic_Timer_Entity = 22,
  Path_Node_Entity = 23,
  Mover_Entity = 24,
};

// Not a member of the enum above, so `switch` over an
// entity_type still warns on an unhandled case.
constexpr uint32_t ENTITY_TYPE_COUNT = 25;

enum class component_type : uint16_t
{
  Box_Volume = 0,
  Enabled = 1,
  Playback = 2,
  Projectile = 3,
  Health = 4,
  Counter = 5,
  Material = 6,
  Render = 7,
  Light = 8,
  Movement = 9,
  Inventory = 10,
  Timer_State = 11,
  Match = 12,
  Path_Follow = 13,
};

constexpr uint32_t COMPONENT_TYPE_COUNT = 14;

} // namespace entities


// --- Enum_Array support ---------------------------------------------
//
// Global scope on purpose: enum_traits is declared in shared/array.hpp,
// which knows nothing about this namespace. `count` is what sizes an
// Enum_Array<entities::Foo, T>, so adding a value to the .def resizes
// every table over that enum. It does not fill the new row -- see
// rows_in_enum_order in array.hpp for the check that catches that.

template <> struct enum_traits<entities::Spawn_Type>
{
  static constexpr uint32_t count = entities::Spawn_Type_COUNT;
  static constexpr entities::enum_type type = entities::enum_type::Spawn_Type;
};

template <> struct enum_traits<entities::Team_Allegiance>
{
  static constexpr uint32_t count = entities::Team_Allegiance_COUNT;
  static constexpr entities::enum_type type = entities::enum_type::Team_Allegiance;
};

template <> struct enum_traits<entities::Weapon>
{
  static constexpr uint32_t count = entities::Weapon_COUNT;
  static constexpr entities::enum_type type = entities::enum_type::Weapon;
};

template <> struct enum_traits<entities::Fire_Resolution>
{
  static constexpr uint32_t count = entities::Fire_Resolution_COUNT;
  static constexpr entities::enum_type type = entities::enum_type::Fire_Resolution;
};

template <> struct enum_traits<entities::Inventory_Slot>
{
  static constexpr uint32_t count = entities::Inventory_Slot_COUNT;
  static constexpr entities::enum_type type = entities::enum_type::Inventory_Slot;
};

template <> struct enum_traits<entities::Damage_Type>
{
  static constexpr uint32_t count = entities::Damage_Type_COUNT;
  static constexpr entities::enum_type type = entities::enum_type::Damage_Type;
};

template <> struct enum_traits<entities::Shader_Type>
{
  static constexpr uint32_t count = entities::Shader_Type_COUNT;
  static constexpr entities::enum_type type = entities::enum_type::Shader_Type;
};

template <> struct enum_traits<entities::Shape_Kind>
{
  static constexpr uint32_t count = entities::Shape_Kind_COUNT;
  static constexpr entities::enum_type type = entities::enum_type::Shape_Kind;
};

template <> struct enum_traits<entities::Light_Mode>
{
  static constexpr uint32_t count = entities::Light_Mode_COUNT;
  static constexpr entities::enum_type type = entities::enum_type::Light_Mode;
};

template <> struct enum_traits<entities::Aim_Pose>
{
  static constexpr uint32_t count = entities::Aim_Pose_COUNT;
  static constexpr entities::enum_type type = entities::enum_type::Aim_Pose;
};

template <> struct enum_traits<entities::Round_Phase>
{
  static constexpr uint32_t count = entities::Round_Phase_COUNT;
  static constexpr entities::enum_type type = entities::enum_type::Round_Phase;
};

template <> struct enum_traits<entities::Game_Mode>
{
  static constexpr uint32_t count = entities::Game_Mode_COUNT;
  static constexpr entities::enum_type type = entities::enum_type::Game_Mode;
};

template <> struct enum_traits<entities::Round_End_Reason>
{
  static constexpr uint32_t count = entities::Round_End_Reason_COUNT;
  static constexpr entities::enum_type type = entities::enum_type::Round_End_Reason;
};

template <> struct enum_traits<entities::Match_Request>
{
  static constexpr uint32_t count = entities::Match_Request_COUNT;
  static constexpr entities::enum_type type = entities::enum_type::Match_Request;
};

template <> struct enum_traits<entities::Easing>
{
  static constexpr uint32_t count = entities::Easing_COUNT;
  static constexpr entities::enum_type type = entities::enum_type::Easing;
};

template <> struct enum_traits<entities::Movement_Override>
{
  static constexpr uint32_t count = entities::Movement_Override_COUNT;
  static constexpr entities::enum_type type = entities::enum_type::Movement_Override;
};

template <> struct enum_traits<entities::enum_type>
{
  static constexpr uint32_t count = entities::ENUM_TYPE_COUNT;
};

template <> struct enum_traits<entities::entity_type>
{
  static constexpr uint32_t count = entities::ENTITY_TYPE_COUNT;
};

template <> struct enum_traits<entities::component_type>
{
  static constexpr uint32_t count = entities::COMPONENT_TYPE_COUNT;
};

namespace entities
{

struct Box_Volume
{
  static constexpr component_type static_component = component_type::Box_Volume;

  linalg::vec3f position = {0.0f, 0.0f, 0.0f};
  linalg::vec3f half_extents = {1.0f, 1.0f, 1.0f};
};

struct Enabled
{
  static constexpr component_type static_component = component_type::Enabled;

  bool value = true;
};

struct Playback
{
  static constexpr component_type static_component = component_type::Playback;

  uint32_t play_count = 0;
  uint32_t stop_count = 0;
};

struct Projectile
{
  static constexpr component_type static_component = component_type::Projectile;

  Weapon weapon_id = Weapon::Rocket_Launcher;
  linalg::vec3f velocity = {};
  shared::entity_uid_t owner_uid = {};
};

struct Health
{
  static constexpr component_type static_component = component_type::Health;

  int32_t current_health = 100;
  int32_t max_health = 100;
};

struct Counter
{
  static constexpr component_type static_component = component_type::Counter;

  int32_t value = 0;
  int32_t limit = 0;
};

struct Material
{
  static constexpr component_type static_component = component_type::Material;

  Shader_Type shader_type = Shader_Type::Lit;
  linalg::vec3f color = {1.0f, 1.0f, 1.0f};
};

struct Render
{
  static constexpr component_type static_component = component_type::Render;

  assets::mesh_asset mesh = assets::mesh_asset::Missing;
  bool visible = true;
  bool is_wireframe = false;
  linalg::vec3f offset = {0.0f, 0.0f, 0.0f};
  linalg::vec3f scale = {1.0f, 1.0f, 1.0f};
  linalg::quatf rotation = {0.0f, 0.0f, 0.0f, 1.0f};
  Material material = {};
};

struct Light
{
  static constexpr component_type static_component = component_type::Light;

  linalg::vec3f color = {1.0f, 1.0f, 1.0f};
  float intensity = 1.0f;
  Light_Mode mode = Light_Mode::Baked;
  float source_radius = 0.0f;
  bool casts_shadows = true;
};

struct Movement
{
  static constexpr component_type static_component = component_type::Movement;

  uint8_t air_jumps_used = {};
  bool is_grounded = {};
  float time_since_grounded_seconds = {};
  bool jump_was_held = {};
  float seconds_until_impulse_ready = {};
  float seconds_until_speed_returns_to_base_speed = {};
  linalg::vec3f momentum = {};
  uint32_t pad_contact_uid = {};
  shared::entity_uid_t ground_mover_uid = {};
  Movement_Override active_override = Movement_Override::None;
  shared::entity_uid_t override_target_uid = {};
  linalg::vec3f override_target_position = {};
  float override_seconds_remaining = {};
  float override_speed = {};
  float override_arrive_radius = {};
};

struct Inventory
{
  static constexpr component_type static_component = component_type::Inventory;

  Enum_Array<Inventory_Slot, uint32_t> weapons = {};
  Inventory_Slot active_slot = Inventory_Slot::Melee;
  uint64_t deploy_complete_time = {};
};

struct Timer_State
{
  static constexpr component_type static_component = component_type::Timer_State;

  float duration_seconds = 1.0f;
  bool repeat = false;
  bool running = false;
  uint32_t deadline_tick = 0;
  uint32_t paused_remaining_ticks = 0;
};

struct Match
{
  static constexpr component_type static_component = component_type::Match;

  Game_Mode mode = Game_Mode::deathmatch;
  Round_Phase phase = Round_Phase::Warmup;
  uint32_t phase_start_tick = 0;
  uint32_t phase_end_tick = 0;
  uint32_t round_number = 0;
  Round_End_Reason end_reason = Round_End_Reason::None;
  Team_Allegiance winning_team = Team_Allegiance::Free_For_All;
  bool objective_reached = false;
  Match_Request requested = Match_Request::None;
};

struct Path_Follow
{
  static constexpr component_type static_component = component_type::Path_Follow;

  shared::entity_uid_t from = {};
  uint32_t segment_start_tick = 0;
  int32_t direction = 1;
  uint32_t frozen_at_tick = 0;
};

struct Entity
{
  // Set by each derived type's constructor. entity_as<T> compares it.
  entity_type type = entity_type::Invalid;

  shared::entity_uid_t entity_id = {};
  linalg::vec3f position = {};
  linalg::quatf orientation = {0.0f, 0.0f, 0.0f, 1.0f};
  network::pascal_string_t<32> name = {};
};

} // namespace entities
