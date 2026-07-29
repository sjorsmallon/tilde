// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by def_gen. Do not edit.
#pragma once

#include "linalg.hpp"
#include "network/network_types.hpp"
#include "span.hpp"
#include <cstdint>

namespace entities
{

// Missing is 0: an asset field that was never assigned resolves to the
// placeholder, which is loudly wrong, rather than to whichever asset
// happened to sort first, which would look plausible.
enum class mesh_asset : uint16_t
{
  Missing = 0,
  Isosphere = 1,
  Pyramid = 2,
  Box = 3,
  Arrow = 4,
  Sphere = 5,
  Cylinder = 6,
  Cone = 7,
  Wedge = 8,
};

constexpr uint32_t mesh_asset_COUNT = 9;

const char* to_string(mesh_asset value);
bool from_string(const char* text, mesh_asset* out_value);

// Missing is 0: an asset field that was never assigned resolves to the
// placeholder, which is loudly wrong, rather than to whichever asset
// happened to sort first, which would look plausible.
enum class sprite_asset : uint16_t
{
  Missing = 0,
  Smoke = 1,
};

constexpr uint32_t sprite_asset_COUNT = 2;

const char* to_string(sprite_asset value);
bool from_string(const char* text, sprite_asset* out_value);

// Where an asset's bytes come from. This exists for the asset system's
// init and for nothing else -- if you are reaching for it anywhere
// else, the code wants an asset id, not a source.
enum asset_source_kind_t : uint8_t
{
  ASSET_SOURCE_MISSING = 0, // no asset assigned; `source` is empty
  ASSET_SOURCE_FILE,        // `source` is a path, relative to the working dir
  ASSET_SOURCE_PROCEDURAL,  // `source` is a generator key
};

struct asset_info_t
{
  const char*         name;
  const char*         source;
  asset_source_kind_t source_kind;
};

// The complete mesh_asset manifest, indexed by id. Populate every entry at
// init: registration must NOT be lazy, or an id resolves to nothing
// depending on what ran first.
Span<const asset_info_t> mesh_asset_manifest();

// The complete sprite_asset manifest, indexed by id. Populate every entry at
// init: registration must NOT be lazy, or an id resolves to nothing
// depending on what ran first.
Span<const asset_info_t> sprite_asset_manifest();

// The manifest a field_info_t::asset_class_id refers to. Empty span for
// an id no asset class owns, which is a caller bug -- check the column
// is not NOT_AN_ASSET_CLASS before calling.
Span<const asset_info_t> asset_class_manifest(int32_t asset_class_id);

enum class Light_Type : uint8_t
{
  Point = 0,
  Spot = 1,
  Directional = 2,
};

const char* to_string(Light_Type value);
bool from_string(const char* text, Light_Type* out_value);

enum class Spawn_Type : uint8_t
{
  Human = 0,
  Bot = 1,
};

const char* to_string(Spawn_Type value);
bool from_string(const char* text, Spawn_Type* out_value);

enum class Shader_Type : uint8_t
{
  Lit = 0,
  Unlit = 1,
};

const char* to_string(Shader_Type value);
bool from_string(const char* text, Shader_Type* out_value);

enum class Shape_Kind : uint8_t
{
  Sphere = 0,
  Capsule = 1,
  Box = 2,
};

const char* to_string(Shape_Kind value);
bool from_string(const char* text, Shape_Kind* out_value);

enum class Trigger_Action : uint8_t
{
  Kill = 0,
  Set_Health = 1,
  Print_Message = 2,
  Warp_To_Spawn = 3,
};

const char* to_string(Trigger_Action value);
bool from_string(const char* text, Trigger_Action* out_value);

enum class Fire_Mode : uint8_t
{
  On_Enter = 0,
  Every_Tick = 1,
};

const char* to_string(Fire_Mode value);
bool from_string(const char* text, Fire_Mode* out_value);

enum class enum_type : uint16_t
{
  Light_Type = 0,
  Spawn_Type = 1,
  Shader_Type = 2,
  Shape_Kind = 3,
  Trigger_Action = 4,
  Fire_Mode = 5,
};

constexpr uint32_t ENUM_TYPE_COUNT = 6;

struct enum_type_info_t
{
  const char*                 name;
  // Indexed by the enum's own numeric value; the values are dense and
  // start at 0, so `size()` is also the count of valid values.
  Span<const char* const>     value_names;
};

const enum_type_info_t& enum_info(enum_type type);

// Invalid is 0 so that zeroed memory never looks like a valid entity.
enum class entity_type : uint16_t
{
  Invalid = 0,
  Player_Spawn_Entity = 1,
  Player_Entity = 2,
  Weapon_Entity = 3,
  Rocket_Entity = 4,
  Particle_Emitter_Entity = 5,
  Trigger_Volume_Entity = 6,
  Light_Entity = 7,
  Physics_Body_Entity = 8,
};

// Not a member of the enum above, so `switch` over an
// entity_type still warns on an unhandled case.
constexpr uint32_t ENTITY_TYPE_COUNT = 9;

enum class component_type : uint16_t
{
  Box_Volume = 0,
  Material = 1,
  Render = 2,
  Hitbox = 3,
};

constexpr uint32_t COMPONENT_TYPE_COUNT = 4;

struct Box_Volume
{
  linalg::vec3f position = {0.0f, 0.0f, 0.0f};
  linalg::vec3f half_extents = {1.0f, 1.0f, 1.0f};
};

struct Material
{
  Shader_Type shader_type = Shader_Type::Lit;
  linalg::vec3f color = {1.0f, 1.0f, 1.0f};
  float roughness = 0.5f;
};

struct Render
{
  mesh_asset mesh = mesh_asset::Missing;
  bool visible = true;
  bool is_wireframe = false;
  linalg::vec3f offset = {0.0f, 0.0f, 0.0f};
  linalg::vec3f scale = {1.0f, 1.0f, 1.0f};
  linalg::vec3f rotation = {0.0f, 0.0f, 0.0f};
  Material material = {};
};

struct Hitbox
{
  Shape_Kind shape = Shape_Kind::Sphere;
  linalg::vec3f size = {8.0f, 8.0f, 8.0f};
  linalg::vec3f offset = {0.0f, 0.0f, 0.0f};
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
};

struct Player_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Player_Entity;

  Player_Entity() { type = entity_type::Player_Entity; }

  float view_angle_yaw = {};
  float view_angle_pitch = {};
  int32_t health = {};
  int32_t ammo = {};
  int32_t active_weapon_id = {};
  int32_t client_slot_index = {};
  linalg::vec3f velocity = {};
  Render render = {};
  Hitbox hitbox = {};
};

struct Weapon_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Weapon_Entity;

  Weapon_Entity() { type = entity_type::Weapon_Entity; }

  int32_t ammo = {};
  int32_t active_weapon_id = {};
  Render render = {};
};

struct Rocket_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Rocket_Entity;

  Rocket_Entity() { type = entity_type::Rocket_Entity; }

  linalg::vec3f velocity = {};
  float lifetime = {};
  float damage_radius = {};
  float damage_amount = {};
  float knockback_force = {};
  uint32_t owner_id = {};
  Render render = {};
  Hitbox hitbox = {};
};

struct Particle_Emitter_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Particle_Emitter_Entity;

  Particle_Emitter_Entity() { type = entity_type::Particle_Emitter_Entity; }

  sprite_asset sprite = sprite_asset::Smoke;
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

struct Trigger_Volume_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Trigger_Volume_Entity;

  Trigger_Volume_Entity() { type = entity_type::Trigger_Volume_Entity; }

  Box_Volume volume = {};
  Trigger_Action action = Trigger_Action::Kill;
  Fire_Mode fire_mode = Fire_Mode::On_Enter;
  network::pascal_string_t<64> param_target_name = {};
  network::pascal_string_t<128> param_string = {};
  float param_float = 0.0f;
};

struct Light_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Light_Entity;

  Light_Entity() { type = entity_type::Light_Entity; }

  linalg::vec3f direction = {};
  linalg::vec3f color = {1.0f, 1.0f, 1.0f};
  float intensity = 1.0f;
  float range = {};
  float spot_inner_degrees = {};
  float spot_outer_degrees = {};
  Light_Type kind = Light_Type::Point;
};

struct Physics_Body_Entity : Entity
{
  static constexpr entity_type static_type = entity_type::Physics_Body_Entity;

  Physics_Body_Entity() { type = entity_type::Physics_Body_Entity; }

  Shape_Kind shape = Shape_Kind::Box;
  linalg::vec3f size = {};
  linalg::vec3f velocity = {};
  float mass = {};
  Render render = {};
  Hitbox hitbox = {};
};

enum field_type_t : uint8_t
{
  FIELD_TYPE_INVALID = 0,
  FIELD_TYPE_F32, FIELD_TYPE_F64,
  FIELD_TYPE_U8, FIELD_TYPE_U16, FIELD_TYPE_U32, FIELD_TYPE_U64,
  FIELD_TYPE_I8, FIELD_TYPE_I16, FIELD_TYPE_I32, FIELD_TYPE_I64,
  FIELD_TYPE_BOOL,
  FIELD_TYPE_V3, FIELD_TYPE_V4, FIELD_TYPE_V4I,
  FIELD_TYPE_STRING, FIELD_TYPE_ASSET, FIELD_TYPE_ENUM, FIELD_TYPE_COMPONENT,
};

enum field_flags_t : uint32_t
{
  FIELD_FLAG_NONE      = 0,
  FIELD_FLAG_NETWORKED = 1 << 0,
  FIELD_FLAG_EDITABLE  = 1 << 1,
  FIELD_FLAG_SAVEABLE  = 1 << 2,
};

// A field of one struct. Offsets are relative to THAT struct, so walking
// into a component composes them:
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
// Four of field_info_t's columns are meaningful only for their own
// FIELD_TYPE. These name what "not that type" looks like, so a reader
// never has to remember whether absent is -1 or 0 -- and so a check
// says what it means rather than testing a magic number.
constexpr int32_t  NOT_A_COMPONENT    = -1;
constexpr uint32_t NOT_A_STRING       = 0;
constexpr int32_t  NOT_AN_ASSET_CLASS = -1;
constexpr int32_t  NOT_AN_ENUM        = -1;

struct field_info_t
{
  const char*  name;
  field_type_t type;
  uint32_t     offset;
  uint32_t     size_in_bytes;
  uint32_t     flags;
  int32_t      component_id;    // FIELD_TYPE_COMPONENT only, else NOT_A_COMPONENT
  uint32_t     string_capacity; // FIELD_TYPE_STRING only, else NOT_A_STRING
  int32_t      asset_class_id;  // FIELD_TYPE_ASSET only, else NOT_AN_ASSET_CLASS
  int32_t      enum_id;         // FIELD_TYPE_ENUM only, else NOT_AN_ENUM
};

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
