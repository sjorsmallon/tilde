// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
//
// The entity family's CORE: the declared enums, the components and
// the base every entity derives from. Everything ONE entity's struct
// is built out of, and nothing that spans the set -- the tables and
// the per-type headers are in entities_generated.hpp beside this one.
#pragma once

#include "array.hpp"
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
};

constexpr uint32_t Weapon_COUNT = 5;

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

enum class Light_Mode : uint8_t
{
  Baked = 0,
  Mixed = 1,
  Dynamic = 2,
};

constexpr uint32_t Light_Mode_COUNT = 3;

const char* to_string(Light_Mode value);
template <> std::optional<Light_Mode> try_from_string<Light_Mode>(std::string_view text);

enum class Damage_Type : uint8_t
{
  Normal = 0,
  Orange = 1,
  Teal = 2,
};

constexpr uint32_t Damage_Type_COUNT = 3;

const char* to_string(Damage_Type value);
template <> std::optional<Damage_Type> try_from_string<Damage_Type>(std::string_view text);

enum class enum_type : uint16_t
{
  Spawn_Type = 0,
  Team_Allegiance = 1,
  Weapon = 2,
  Fire_Resolution = 3,
  Inventory_Slot = 4,
  Shader_Type = 5,
  Shape_Kind = 6,
  Aim_Pose = 7,
  Light_Mode = 8,
  Damage_Type = 9,
};

constexpr uint32_t ENUM_TYPE_COUNT = 10;

const enum_type_info_t& enum_info(enum_type type);

extern const enum_type_info_t ENUM_INFOS[ENUM_TYPE_COUNT];

// Invalid is 0 so that zeroed memory never looks like a valid entity.
enum class entity_type : uint16_t
{
  Invalid = 0,
  Reflection_Volume_Entity = 1,
  Player_Spawn_Entity = 2,
  Player_Spectate_Entity = 3,
  Player_Entity = 4,
  Weapon_Entity = 5,
  Rocket_Entity = 6,
  Particle_Emitter_Entity = 7,
  Game_Rules_Entity = 8,
  Damageable_Entity = 9,
  Trigger_Volume_Entity = 10,
  Sound_Emitter_Entity = 11,
  Point_Light_Entity = 12,
  Spot_Light_Entity = 13,
  Directional_Light_Entity = 14,
  Physics_Body_Entity = 15,
  Logic_Counter_Entity = 16,
  Jump_Pad_Entity = 17,
};

// Not a member of the enum above, so `switch` over an
// entity_type still warns on an unhandled case.
constexpr uint32_t ENTITY_TYPE_COUNT = 18;

enum class component_type : uint16_t
{
  Box_Volume = 0,
  Enabled = 1,
  Playback = 2,
  Health = 3,
  Counter = 4,
  Material = 5,
  Render = 6,
  Light = 7,
  Movement = 8,
  Inventory = 9,
};

constexpr uint32_t COMPONENT_TYPE_COUNT = 10;

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

template <> struct enum_traits<entities::Aim_Pose>
{
  static constexpr uint32_t count = entities::Aim_Pose_COUNT;
  static constexpr entities::enum_type type = entities::enum_type::Aim_Pose;
};

template <> struct enum_traits<entities::Light_Mode>
{
  static constexpr uint32_t count = entities::Light_Mode_COUNT;
  static constexpr entities::enum_type type = entities::enum_type::Light_Mode;
};

template <> struct enum_traits<entities::Damage_Type>
{
  static constexpr uint32_t count = entities::Damage_Type_COUNT;
  static constexpr entities::enum_type type = entities::enum_type::Damage_Type;
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

  uint32_t value = 0;
  uint32_t limit = 0;
};

struct Material
{
  static constexpr component_type static_component = component_type::Material;

  Shader_Type shader_type = Shader_Type::Lit;
  linalg::vec3f color = {1.0f, 1.0f, 1.0f};
  float roughness = 0.5f;
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
};

struct Inventory
{
  static constexpr component_type static_component = component_type::Inventory;

  Enum_Array<Inventory_Slot, uint32_t> weapons = {};
  Inventory_Slot active_slot = Inventory_Slot::Melee;
  uint64_t deploy_complete_time = {};
};

struct Entity
{
  // Set by each derived type's constructor. entity_as<T> compares it.
  entity_type type = entity_type::Invalid;

  uint32_t entity_id = {};
  linalg::vec3f position = {};
  linalg::quatf orientation = {0.0f, 0.0f, 0.0f, 1.0f};
  network::pascal_string_t<32> name = {};
};

} // namespace entities
