// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
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
};

constexpr uint32_t Weapon_COUNT = 4;

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

enum class Trigger_Action : uint8_t
{
  Kill = 0,
  Set_Health = 1,
  Print_Message = 2,
  Warp_To_Spawn = 3,
  Complete_Level = 4,
  Checkpoint = 5,
  Grant_Weapon = 6,
  Set_Velocity = 7,
};

constexpr uint32_t Trigger_Action_COUNT = 8;

const char* to_string(Trigger_Action value);
template <> std::optional<Trigger_Action> try_from_string<Trigger_Action>(std::string_view text);

enum class Fire_Mode : uint8_t
{
  On_Enter = 0,
  Every_Tick = 1,
};

constexpr uint32_t Fire_Mode_COUNT = 2;

const char* to_string(Fire_Mode value);
template <> std::optional<Fire_Mode> try_from_string<Fire_Mode>(std::string_view text);

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

enum class enum_type : uint16_t
{
  Spawn_Type = 0,
  Team_Allegiance = 1,
  Weapon = 2,
  Fire_Resolution = 3,
  Inventory_Slot = 4,
  Shader_Type = 5,
  Shape_Kind = 6,
  Trigger_Action = 7,
  Fire_Mode = 8,
  Aim_Pose = 9,
};

constexpr uint32_t ENUM_TYPE_COUNT = 10;

const enum_type_info_t& enum_info(enum_type type);

// Invalid is 0 so that zeroed memory never looks like a valid entity.
enum class entity_type : uint16_t
{
  Invalid = 0,
  Player_Spawn_Entity = 1,
  Player_Spectate_Entity = 2,
  Player_Entity = 3,
  Weapon_Entity = 4,
  Rocket_Entity = 5,
  Particle_Emitter_Entity = 6,
  Damageable_Entity = 7,
  Trigger_Volume_Entity = 8,
  Point_Light_Entity = 9,
  Spot_Light_Entity = 10,
  Directional_Light_Entity = 11,
  Physics_Body_Entity = 12,
};

// Not a member of the enum above, so `switch` over an
// entity_type still warns on an unhandled case.
constexpr uint32_t ENTITY_TYPE_COUNT = 13;

enum class component_type : uint16_t
{
  Box_Volume = 0,
  Material = 1,
  Render = 2,
  Light = 3,
  Movement = 4,
  Inventory = 5,
};

constexpr uint32_t COMPONENT_TYPE_COUNT = 6;

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

template <> struct enum_traits<entities::Trigger_Action>
{
  static constexpr uint32_t count = entities::Trigger_Action_COUNT;
  static constexpr entities::enum_type type = entities::enum_type::Trigger_Action;
};

template <> struct enum_traits<entities::Fire_Mode>
{
  static constexpr uint32_t count = entities::Fire_Mode_COUNT;
  static constexpr entities::enum_type type = entities::enum_type::Fire_Mode;
};

template <> struct enum_traits<entities::Aim_Pose>
{
  static constexpr uint32_t count = entities::Aim_Pose_COUNT;
  static constexpr entities::enum_type type = entities::enum_type::Aim_Pose;
};

namespace entities
{

struct Box_Volume
{
  static constexpr component_type static_component = component_type::Box_Volume;

  linalg::vec3f position = {0.0f, 0.0f, 0.0f};
  linalg::vec3f half_extents = {1.0f, 1.0f, 1.0f};
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
  linalg::vec3f rotation = {0.0f, 0.0f, 0.0f};
  Material material = {};
};

struct Light
{
  static constexpr component_type static_component = component_type::Light;

  linalg::vec3f color = {1.0f, 1.0f, 1.0f};
  float intensity = 1.0f;
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
  linalg::vec3f orientation = {};
};

struct Player_Spawn_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Player_Spawn_Entity;

  Player_Spawn_Entity() { type = entity_type::Player_Spawn_Entity; }

  Spawn_Type spawn_type = Spawn_Type::Human;
  Team_Allegiance team_allegiance = Team_Allegiance::Free_For_All;
};

struct Player_Spectate_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Player_Spectate_Entity;

  Player_Spectate_Entity() { type = entity_type::Player_Spectate_Entity; }

};

struct Player_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Player_Entity;

  Player_Entity() { type = entity_type::Player_Entity; }

  float view_angle_yaw = {};
  float view_angle_pitch = {};
  float body_yaw = {};
  int32_t health = {};
  uint32_t death_tick = {};
  uint32_t last_fire_tick = {};
  Weapon last_fire_weapon = Weapon::Knife;
  uint64_t reload_complete_time = {};
  uint32_t last_empty_fire_warning_tick = {};
  uint32_t checkpoint_uid = {};
  uint32_t last_hit_tick = {};
  bool last_hit_was_headshot = {};
  int32_t client_slot_index = {};
  network::pascal_string_t<32> name = {};
  int32_t kills = {};
  int32_t deaths = {};
  linalg::vec3f velocity = {};
  Inventory inventory = {};
  Movement movement = {};
  Render render = {.mesh = assets::mesh_asset::Leet_Full};
  Team_Allegiance team_allegiance = Team_Allegiance::Free_For_All;
};

struct Weapon_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Weapon_Entity;

  Weapon_Entity() { type = entity_type::Weapon_Entity; }

  int32_t ammo = {};
  Weapon weapon_id = {};
  uint32_t owner_uid = {};
  uint64_t next_fire_time = {};
  Render render = {};
};

struct Rocket_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Rocket_Entity;

  Rocket_Entity() { type = entity_type::Rocket_Entity; }

  linalg::vec3f velocity = {};
  float lifetime = 5.0f;
  float damage_amount = 50.0f;
  float damage_radius = 120.0f;
  float knockback_force = 600.0f;
  uint32_t owner_id = {};
  float collision_radius = 12.0f;
  Render render = {};
};

struct Particle_Emitter_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Particle_Emitter_Entity;

  Particle_Emitter_Entity() { type = entity_type::Particle_Emitter_Entity; }

  assets::texture_asset sprite = assets::texture_asset::Smoke;
  float emit_rate = 20.0f;
  int32_t max_particles = 64;
  float lifetime_min = 0.5f;
  float lifetime_max = 1.5f;
  float velocity_min = 2.0f;
  float velocity_max = 5.0f;
  float spread = 0.5f;
  linalg::vec3f gravity = {0.0f, 0.5f, 0.0f};
  float drag = 0.3f;
  float size_start = 0.5f;
  float size_end = 2.0f;
  float rotation_speed_min = -1.0f;
  float rotation_speed_max = 1.0f;
  linalg::vec3f color_start = {1.0f, 1.0f, 1.0f};
  linalg::vec3f color_end = {0.5f, 0.5f, 0.5f};
  float alpha_start = 0.8f;
  float alpha_end = 0.0f;
  float emitter_lifetime = 0.0f;
  uint32_t parent_entity_id = 0;
};

struct Damageable_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Damageable_Entity;

  Damageable_Entity() { type = entity_type::Damageable_Entity; }

  int32_t max_health = 100;
  int32_t health = 100;
  linalg::vec3f hitbox_half_extents = {16.0f, 32.0f, 16.0f};
  Render render = {};
};

struct Trigger_Volume_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Trigger_Volume_Entity;

  Trigger_Volume_Entity() { type = entity_type::Trigger_Volume_Entity; }

  Box_Volume volume = {.half_extents = {64.0f, 64.0f, 64.0f}};
  Trigger_Action action = Trigger_Action::Kill;
  Fire_Mode fire_mode = Fire_Mode::On_Enter;
  network::pascal_string_t<64> param_target_name = {};
  network::pascal_string_t<128> param_string = {};
  float param_float = 0.0f;
};

struct Point_Light_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Point_Light_Entity;

  Point_Light_Entity() { type = entity_type::Point_Light_Entity; }

  Light light = {};
  float range = 256.0f;
};

struct Spot_Light_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Spot_Light_Entity;

  Spot_Light_Entity() { type = entity_type::Spot_Light_Entity; }

  Light light = {};
  float range = 512.0f;
  float inner_degrees = 20.0f;
  float outer_degrees = 35.0f;
};

struct Directional_Light_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Directional_Light_Entity;

  Directional_Light_Entity() { type = entity_type::Directional_Light_Entity; }

  Light light = {};
};

struct Physics_Body_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Physics_Body_Entity;

  Physics_Body_Entity() { type = entity_type::Physics_Body_Entity; }

  Shape_Kind shape = Shape_Kind::Box;
  linalg::vec3f size = {};
  linalg::vec3f velocity = {};
  float mass = 10.0f;
  Render render = {};
};

// The entity pool is a byte buffer: it copies with memcpy and runs no
// destructor. A field that breaks either of these corrupts or leaks
// silently, so the check lives here rather than in a test nobody runs
// before the pool does.
static_assert(std::is_trivially_copyable_v<Player_Spawn_Entity>,
              "Player_Spawn_Entity must stay trivially copyable: pooled storage, snapshot "
              "baselines and undo all copy entities with memcpy");
static_assert(std::is_trivially_destructible_v<Player_Spawn_Entity>,
              "Player_Spawn_Entity must stay trivially destructible: the entity pool frees a "
              "slot by overwriting it and runs no destructor");
static_assert(std::is_base_of_v<Entity, Player_Spawn_Entity>,
              "Player_Spawn_Entity must derive from Entity: the generated tables hand out "
              "Entity* for every entity type");

static_assert(std::is_trivially_copyable_v<Player_Spectate_Entity>,
              "Player_Spectate_Entity must stay trivially copyable: pooled storage, snapshot "
              "baselines and undo all copy entities with memcpy");
static_assert(std::is_trivially_destructible_v<Player_Spectate_Entity>,
              "Player_Spectate_Entity must stay trivially destructible: the entity pool frees a "
              "slot by overwriting it and runs no destructor");
static_assert(std::is_base_of_v<Entity, Player_Spectate_Entity>,
              "Player_Spectate_Entity must derive from Entity: the generated tables hand out "
              "Entity* for every entity type");

static_assert(std::is_trivially_copyable_v<Player_Entity>,
              "Player_Entity must stay trivially copyable: pooled storage, snapshot "
              "baselines and undo all copy entities with memcpy");
static_assert(std::is_trivially_destructible_v<Player_Entity>,
              "Player_Entity must stay trivially destructible: the entity pool frees a "
              "slot by overwriting it and runs no destructor");
static_assert(std::is_base_of_v<Entity, Player_Entity>,
              "Player_Entity must derive from Entity: the generated tables hand out "
              "Entity* for every entity type");

static_assert(std::is_trivially_copyable_v<Weapon_Entity>,
              "Weapon_Entity must stay trivially copyable: pooled storage, snapshot "
              "baselines and undo all copy entities with memcpy");
static_assert(std::is_trivially_destructible_v<Weapon_Entity>,
              "Weapon_Entity must stay trivially destructible: the entity pool frees a "
              "slot by overwriting it and runs no destructor");
static_assert(std::is_base_of_v<Entity, Weapon_Entity>,
              "Weapon_Entity must derive from Entity: the generated tables hand out "
              "Entity* for every entity type");

static_assert(std::is_trivially_copyable_v<Rocket_Entity>,
              "Rocket_Entity must stay trivially copyable: pooled storage, snapshot "
              "baselines and undo all copy entities with memcpy");
static_assert(std::is_trivially_destructible_v<Rocket_Entity>,
              "Rocket_Entity must stay trivially destructible: the entity pool frees a "
              "slot by overwriting it and runs no destructor");
static_assert(std::is_base_of_v<Entity, Rocket_Entity>,
              "Rocket_Entity must derive from Entity: the generated tables hand out "
              "Entity* for every entity type");

static_assert(std::is_trivially_copyable_v<Particle_Emitter_Entity>,
              "Particle_Emitter_Entity must stay trivially copyable: pooled storage, snapshot "
              "baselines and undo all copy entities with memcpy");
static_assert(std::is_trivially_destructible_v<Particle_Emitter_Entity>,
              "Particle_Emitter_Entity must stay trivially destructible: the entity pool frees a "
              "slot by overwriting it and runs no destructor");
static_assert(std::is_base_of_v<Entity, Particle_Emitter_Entity>,
              "Particle_Emitter_Entity must derive from Entity: the generated tables hand out "
              "Entity* for every entity type");

static_assert(std::is_trivially_copyable_v<Damageable_Entity>,
              "Damageable_Entity must stay trivially copyable: pooled storage, snapshot "
              "baselines and undo all copy entities with memcpy");
static_assert(std::is_trivially_destructible_v<Damageable_Entity>,
              "Damageable_Entity must stay trivially destructible: the entity pool frees a "
              "slot by overwriting it and runs no destructor");
static_assert(std::is_base_of_v<Entity, Damageable_Entity>,
              "Damageable_Entity must derive from Entity: the generated tables hand out "
              "Entity* for every entity type");

static_assert(std::is_trivially_copyable_v<Trigger_Volume_Entity>,
              "Trigger_Volume_Entity must stay trivially copyable: pooled storage, snapshot "
              "baselines and undo all copy entities with memcpy");
static_assert(std::is_trivially_destructible_v<Trigger_Volume_Entity>,
              "Trigger_Volume_Entity must stay trivially destructible: the entity pool frees a "
              "slot by overwriting it and runs no destructor");
static_assert(std::is_base_of_v<Entity, Trigger_Volume_Entity>,
              "Trigger_Volume_Entity must derive from Entity: the generated tables hand out "
              "Entity* for every entity type");

static_assert(std::is_trivially_copyable_v<Point_Light_Entity>,
              "Point_Light_Entity must stay trivially copyable: pooled storage, snapshot "
              "baselines and undo all copy entities with memcpy");
static_assert(std::is_trivially_destructible_v<Point_Light_Entity>,
              "Point_Light_Entity must stay trivially destructible: the entity pool frees a "
              "slot by overwriting it and runs no destructor");
static_assert(std::is_base_of_v<Entity, Point_Light_Entity>,
              "Point_Light_Entity must derive from Entity: the generated tables hand out "
              "Entity* for every entity type");

static_assert(std::is_trivially_copyable_v<Spot_Light_Entity>,
              "Spot_Light_Entity must stay trivially copyable: pooled storage, snapshot "
              "baselines and undo all copy entities with memcpy");
static_assert(std::is_trivially_destructible_v<Spot_Light_Entity>,
              "Spot_Light_Entity must stay trivially destructible: the entity pool frees a "
              "slot by overwriting it and runs no destructor");
static_assert(std::is_base_of_v<Entity, Spot_Light_Entity>,
              "Spot_Light_Entity must derive from Entity: the generated tables hand out "
              "Entity* for every entity type");

static_assert(std::is_trivially_copyable_v<Directional_Light_Entity>,
              "Directional_Light_Entity must stay trivially copyable: pooled storage, snapshot "
              "baselines and undo all copy entities with memcpy");
static_assert(std::is_trivially_destructible_v<Directional_Light_Entity>,
              "Directional_Light_Entity must stay trivially destructible: the entity pool frees a "
              "slot by overwriting it and runs no destructor");
static_assert(std::is_base_of_v<Entity, Directional_Light_Entity>,
              "Directional_Light_Entity must derive from Entity: the generated tables hand out "
              "Entity* for every entity type");

static_assert(std::is_trivially_copyable_v<Physics_Body_Entity>,
              "Physics_Body_Entity must stay trivially copyable: pooled storage, snapshot "
              "baselines and undo all copy entities with memcpy");
static_assert(std::is_trivially_destructible_v<Physics_Body_Entity>,
              "Physics_Body_Entity must stay trivially destructible: the entity pool frees a "
              "slot by overwriting it and runs no destructor");
static_assert(std::is_base_of_v<Entity, Physics_Body_Entity>,
              "Physics_Body_Entity must derive from Entity: the generated tables hand out "
              "Entity* for every entity type");

enum field_flags_t : uint32_t
{
  FIELD_FLAG_NONE      = 0,
  FIELD_FLAG_NETWORKED = 1 << 0,
  FIELD_FLAG_EDITABLE  = 1 << 1,
  FIELD_FLAG_SAVEABLE  = 1 << 2,
};

// Entity field tables NEST -- a component-typed field's insides live in
// another table, and a walk composes the offsets:
//
//   for (field : entity_info(type).fields)
//     if (field.type == FIELD_TYPE_COMPONENT)
//       for (inner : component_info((component_type)field.component_id).fields)
//         byte_offset = field.offset + inner.offset;
//
// A component-typed field's own size_in_bytes spans the whole nested
// struct, so a consumer that does NOT care about the inside (undo's
// memcmp diffing, a whole-struct copy) can treat it as one opaque blob
// and never recurse at all.

struct entity_type_info_t
{
  const char*         classname;
  const char*         display_name;
  Span<const field_info_t> fields;
  uint32_t            size_in_bytes;
  uint32_t            alignment;
  uint32_t            component_mask;
  bool                runtime_only;

  // Writes a default constructed entity of this type into `memory`, which
  // must be at least size_in_bytes wide and `alignment` aligned. Allocates
  // nothing -- this is the type-erased hook for callers that already own
  // their storage: undo snapshots, network baselines, pooled storage.
  Entity* (*construct_at)(void* memory);

  // Reaches the base of an ALREADY CONSTRUCTED entity of this type, given
  // untyped storage. Deliberately not a cast at the call site: an entity
  // and its base both have data members, so they are not
  // pointer-interconvertible and `(Entity*)memory` is a bet on a layout
  // C++ does not promise. This thunk is emitted where the concrete type is
  // complete, so the compiler applies whatever adjustment the ABI wants.
  //
  // For pooled storage, which addresses its elements as bytes. `memory`
  // must already hold a live entity of this type -- construct_at first.
  Entity* (*as_base)(void* memory);
};

struct component_type_info_t
{
  const char*         name;
  Span<const field_info_t> fields;
  uint32_t            size_in_bytes;
};

// The tables are an implementation detail of the generated TU. Everything
// callers need goes through these free functions, which assert on bad tags.
const entity_type_info_t&    entity_info(entity_type type);
const component_type_info_t& component_info(component_type component);
entity_type                  entity_type_from_classname(const char* classname);
bool has_component(entity_type type, component_type component);
int32_t component_byte_offset(entity_type type, component_type component);

// Heap factory. Asserts on entity_type::Invalid -- reaching it with an
// invalid tag is a caller bug, not a data error.
Entity* create_entity(entity_type type);

// The map loader's entry point: classname off disk to a live instance.
// Returns nullptr for an unknown classname, which IS a data error -- the
// caller must report it rather than skipping the entity quietly.
Entity* entity_from_classname(const char* classname);

// The counterpart to create_entity. Entities have no virtual destructor
// (they have no virtuals at all), so `delete` through a base pointer is
// wrong; this recovers the concrete type from the tag first. Null safe.
void destroy_entity(Entity* entity);

// Every entity type the editor may place: the ones the .def did NOT mark
// @runtime_only, in declaration order. Contiguous and stable, so a
// placement menu can index it directly.
Span<const entity_type> placeable_entity_types();

// Digest of every declaration in EVERY .def of the generator run --
// entity layout, the resolved asset manifest, and the cvar/command
// tables. Exchanged at connect; a mismatch means the two sides
// disagree about what the bytes mean. It lives in this namespace for
// historical reasons and is the ONE such value -- cvars_generated.hpp
// deliberately does not emit a second one.
extern const uint32_t SCHEMA_HASH;

} // namespace entities
