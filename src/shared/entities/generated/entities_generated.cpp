// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by entity_gen. Do not edit.
#include "entities_generated.hpp"
#include <cassert>
#include <cstddef>
#include <cstring>
#include <new>

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#elif defined(_MSC_VER)
#pragma warning(disable : 4841)
#endif

namespace entities
{

namespace
{

constexpr field_info_t Box_Volume_FIELDS[] = {
  {"position", FIELD_TYPE_V3, (uint32_t)offsetof(Box_Volume, position), (uint32_t)sizeof(Box_Volume::position), 6u, -1, 0, -1},
  {"half_extents", FIELD_TYPE_V3, (uint32_t)offsetof(Box_Volume, half_extents), (uint32_t)sizeof(Box_Volume::half_extents), 6u, -1, 0, -1},
};

constexpr field_info_t Material_FIELDS[] = {
  {"shader_type", FIELD_TYPE_ENUM, (uint32_t)offsetof(Material, shader_type), (uint32_t)sizeof(Material::shader_type), 7u, -1, 0, -1},
  {"color", FIELD_TYPE_V3, (uint32_t)offsetof(Material, color), (uint32_t)sizeof(Material::color), 7u, -1, 0, -1},
  {"roughness", FIELD_TYPE_F32, (uint32_t)offsetof(Material, roughness), (uint32_t)sizeof(Material::roughness), 7u, -1, 0, -1},
};

constexpr field_info_t Render_FIELDS[] = {
  {"mesh", FIELD_TYPE_ASSET, (uint32_t)offsetof(Render, mesh), (uint32_t)sizeof(Render::mesh), 7u, -1, 0, 0},
  {"visible", FIELD_TYPE_BOOL, (uint32_t)offsetof(Render, visible), (uint32_t)sizeof(Render::visible), 7u, -1, 0, -1},
  {"is_wireframe", FIELD_TYPE_BOOL, (uint32_t)offsetof(Render, is_wireframe), (uint32_t)sizeof(Render::is_wireframe), 7u, -1, 0, -1},
  {"offset", FIELD_TYPE_V3, (uint32_t)offsetof(Render, offset), (uint32_t)sizeof(Render::offset), 7u, -1, 0, -1},
  {"scale", FIELD_TYPE_V3, (uint32_t)offsetof(Render, scale), (uint32_t)sizeof(Render::scale), 7u, -1, 0, -1},
  {"rotation", FIELD_TYPE_V3, (uint32_t)offsetof(Render, rotation), (uint32_t)sizeof(Render::rotation), 7u, -1, 0, -1},
  {"material", FIELD_TYPE_COMPONENT, (uint32_t)offsetof(Render, material), (uint32_t)sizeof(Render::material), 0u, 1, 0, -1},
};

constexpr field_info_t Hitbox_FIELDS[] = {
  {"shape", FIELD_TYPE_ENUM, (uint32_t)offsetof(Hitbox, shape), (uint32_t)sizeof(Hitbox::shape), 7u, -1, 0, -1},
  {"size", FIELD_TYPE_V3, (uint32_t)offsetof(Hitbox, size), (uint32_t)sizeof(Hitbox::size), 7u, -1, 0, -1},
  {"offset", FIELD_TYPE_V3, (uint32_t)offsetof(Hitbox, offset), (uint32_t)sizeof(Hitbox::offset), 7u, -1, 0, -1},
};

constexpr field_info_t Player_Spawn_Entity_FIELDS[] = {
  {"entity_id", FIELD_TYPE_U32, (uint32_t)offsetof(Player_Spawn_Entity, entity_id), (uint32_t)sizeof(Player_Spawn_Entity::entity_id), 1u, -1, 0, -1},
  {"position", FIELD_TYPE_V3, (uint32_t)offsetof(Player_Spawn_Entity, position), (uint32_t)sizeof(Player_Spawn_Entity::position), 7u, -1, 0, -1},
  {"orientation", FIELD_TYPE_V3, (uint32_t)offsetof(Player_Spawn_Entity, orientation), (uint32_t)sizeof(Player_Spawn_Entity::orientation), 7u, -1, 0, -1},
  {"spawn_type", FIELD_TYPE_ENUM, (uint32_t)offsetof(Player_Spawn_Entity, spawn_type), (uint32_t)sizeof(Player_Spawn_Entity::spawn_type), 6u, -1, 0, -1},
};

constexpr field_info_t Player_Entity_FIELDS[] = {
  {"entity_id", FIELD_TYPE_U32, (uint32_t)offsetof(Player_Entity, entity_id), (uint32_t)sizeof(Player_Entity::entity_id), 1u, -1, 0, -1},
  {"position", FIELD_TYPE_V3, (uint32_t)offsetof(Player_Entity, position), (uint32_t)sizeof(Player_Entity::position), 7u, -1, 0, -1},
  {"orientation", FIELD_TYPE_V3, (uint32_t)offsetof(Player_Entity, orientation), (uint32_t)sizeof(Player_Entity::orientation), 7u, -1, 0, -1},
  {"view_angle_yaw", FIELD_TYPE_F32, (uint32_t)offsetof(Player_Entity, view_angle_yaw), (uint32_t)sizeof(Player_Entity::view_angle_yaw), 1u, -1, 0, -1},
  {"view_angle_pitch", FIELD_TYPE_F32, (uint32_t)offsetof(Player_Entity, view_angle_pitch), (uint32_t)sizeof(Player_Entity::view_angle_pitch), 1u, -1, 0, -1},
  {"health", FIELD_TYPE_I32, (uint32_t)offsetof(Player_Entity, health), (uint32_t)sizeof(Player_Entity::health), 1u, -1, 0, -1},
  {"ammo", FIELD_TYPE_I32, (uint32_t)offsetof(Player_Entity, ammo), (uint32_t)sizeof(Player_Entity::ammo), 1u, -1, 0, -1},
  {"active_weapon_id", FIELD_TYPE_I32, (uint32_t)offsetof(Player_Entity, active_weapon_id), (uint32_t)sizeof(Player_Entity::active_weapon_id), 1u, -1, 0, -1},
  {"client_slot_index", FIELD_TYPE_I32, (uint32_t)offsetof(Player_Entity, client_slot_index), (uint32_t)sizeof(Player_Entity::client_slot_index), 1u, -1, 0, -1},
  {"velocity", FIELD_TYPE_V3, (uint32_t)offsetof(Player_Entity, velocity), (uint32_t)sizeof(Player_Entity::velocity), 1u, -1, 0, -1},
  {"render", FIELD_TYPE_COMPONENT, (uint32_t)offsetof(Player_Entity, render), (uint32_t)sizeof(Player_Entity::render), 0u, 2, 0, -1},
  {"hitbox", FIELD_TYPE_COMPONENT, (uint32_t)offsetof(Player_Entity, hitbox), (uint32_t)sizeof(Player_Entity::hitbox), 0u, 3, 0, -1},
};

constexpr field_info_t Weapon_Entity_FIELDS[] = {
  {"entity_id", FIELD_TYPE_U32, (uint32_t)offsetof(Weapon_Entity, entity_id), (uint32_t)sizeof(Weapon_Entity::entity_id), 1u, -1, 0, -1},
  {"position", FIELD_TYPE_V3, (uint32_t)offsetof(Weapon_Entity, position), (uint32_t)sizeof(Weapon_Entity::position), 7u, -1, 0, -1},
  {"orientation", FIELD_TYPE_V3, (uint32_t)offsetof(Weapon_Entity, orientation), (uint32_t)sizeof(Weapon_Entity::orientation), 7u, -1, 0, -1},
  {"ammo", FIELD_TYPE_I32, (uint32_t)offsetof(Weapon_Entity, ammo), (uint32_t)sizeof(Weapon_Entity::ammo), 1u, -1, 0, -1},
  {"active_weapon_id", FIELD_TYPE_I32, (uint32_t)offsetof(Weapon_Entity, active_weapon_id), (uint32_t)sizeof(Weapon_Entity::active_weapon_id), 1u, -1, 0, -1},
  {"render", FIELD_TYPE_COMPONENT, (uint32_t)offsetof(Weapon_Entity, render), (uint32_t)sizeof(Weapon_Entity::render), 0u, 2, 0, -1},
};

constexpr field_info_t Rocket_Entity_FIELDS[] = {
  {"entity_id", FIELD_TYPE_U32, (uint32_t)offsetof(Rocket_Entity, entity_id), (uint32_t)sizeof(Rocket_Entity::entity_id), 1u, -1, 0, -1},
  {"position", FIELD_TYPE_V3, (uint32_t)offsetof(Rocket_Entity, position), (uint32_t)sizeof(Rocket_Entity::position), 7u, -1, 0, -1},
  {"orientation", FIELD_TYPE_V3, (uint32_t)offsetof(Rocket_Entity, orientation), (uint32_t)sizeof(Rocket_Entity::orientation), 7u, -1, 0, -1},
  {"velocity", FIELD_TYPE_V3, (uint32_t)offsetof(Rocket_Entity, velocity), (uint32_t)sizeof(Rocket_Entity::velocity), 1u, -1, 0, -1},
  {"lifetime", FIELD_TYPE_F32, (uint32_t)offsetof(Rocket_Entity, lifetime), (uint32_t)sizeof(Rocket_Entity::lifetime), 0u, -1, 0, -1},
  {"damage_radius", FIELD_TYPE_F32, (uint32_t)offsetof(Rocket_Entity, damage_radius), (uint32_t)sizeof(Rocket_Entity::damage_radius), 0u, -1, 0, -1},
  {"damage_amount", FIELD_TYPE_F32, (uint32_t)offsetof(Rocket_Entity, damage_amount), (uint32_t)sizeof(Rocket_Entity::damage_amount), 0u, -1, 0, -1},
  {"knockback_force", FIELD_TYPE_F32, (uint32_t)offsetof(Rocket_Entity, knockback_force), (uint32_t)sizeof(Rocket_Entity::knockback_force), 0u, -1, 0, -1},
  {"owner_id", FIELD_TYPE_U32, (uint32_t)offsetof(Rocket_Entity, owner_id), (uint32_t)sizeof(Rocket_Entity::owner_id), 0u, -1, 0, -1},
  {"render", FIELD_TYPE_COMPONENT, (uint32_t)offsetof(Rocket_Entity, render), (uint32_t)sizeof(Rocket_Entity::render), 0u, 2, 0, -1},
  {"hitbox", FIELD_TYPE_COMPONENT, (uint32_t)offsetof(Rocket_Entity, hitbox), (uint32_t)sizeof(Rocket_Entity::hitbox), 0u, 3, 0, -1},
};

constexpr field_info_t Particle_Emitter_Entity_FIELDS[] = {
  {"entity_id", FIELD_TYPE_U32, (uint32_t)offsetof(Particle_Emitter_Entity, entity_id), (uint32_t)sizeof(Particle_Emitter_Entity::entity_id), 1u, -1, 0, -1},
  {"position", FIELD_TYPE_V3, (uint32_t)offsetof(Particle_Emitter_Entity, position), (uint32_t)sizeof(Particle_Emitter_Entity::position), 7u, -1, 0, -1},
  {"orientation", FIELD_TYPE_V3, (uint32_t)offsetof(Particle_Emitter_Entity, orientation), (uint32_t)sizeof(Particle_Emitter_Entity::orientation), 7u, -1, 0, -1},
  {"sprite", FIELD_TYPE_ASSET, (uint32_t)offsetof(Particle_Emitter_Entity, sprite), (uint32_t)sizeof(Particle_Emitter_Entity::sprite), 6u, -1, 0, 1},
  {"emit_rate", FIELD_TYPE_F32, (uint32_t)offsetof(Particle_Emitter_Entity, emit_rate), (uint32_t)sizeof(Particle_Emitter_Entity::emit_rate), 6u, -1, 0, -1},
  {"max_particles", FIELD_TYPE_I32, (uint32_t)offsetof(Particle_Emitter_Entity, max_particles), (uint32_t)sizeof(Particle_Emitter_Entity::max_particles), 6u, -1, 0, -1},
  {"lifetime_min", FIELD_TYPE_F32, (uint32_t)offsetof(Particle_Emitter_Entity, lifetime_min), (uint32_t)sizeof(Particle_Emitter_Entity::lifetime_min), 6u, -1, 0, -1},
  {"lifetime_max", FIELD_TYPE_F32, (uint32_t)offsetof(Particle_Emitter_Entity, lifetime_max), (uint32_t)sizeof(Particle_Emitter_Entity::lifetime_max), 6u, -1, 0, -1},
  {"velocity_min", FIELD_TYPE_F32, (uint32_t)offsetof(Particle_Emitter_Entity, velocity_min), (uint32_t)sizeof(Particle_Emitter_Entity::velocity_min), 6u, -1, 0, -1},
  {"velocity_max", FIELD_TYPE_F32, (uint32_t)offsetof(Particle_Emitter_Entity, velocity_max), (uint32_t)sizeof(Particle_Emitter_Entity::velocity_max), 6u, -1, 0, -1},
  {"spread", FIELD_TYPE_F32, (uint32_t)offsetof(Particle_Emitter_Entity, spread), (uint32_t)sizeof(Particle_Emitter_Entity::spread), 6u, -1, 0, -1},
  {"gravity", FIELD_TYPE_V3, (uint32_t)offsetof(Particle_Emitter_Entity, gravity), (uint32_t)sizeof(Particle_Emitter_Entity::gravity), 6u, -1, 0, -1},
  {"drag", FIELD_TYPE_F32, (uint32_t)offsetof(Particle_Emitter_Entity, drag), (uint32_t)sizeof(Particle_Emitter_Entity::drag), 6u, -1, 0, -1},
  {"size_start", FIELD_TYPE_F32, (uint32_t)offsetof(Particle_Emitter_Entity, size_start), (uint32_t)sizeof(Particle_Emitter_Entity::size_start), 6u, -1, 0, -1},
  {"size_end", FIELD_TYPE_F32, (uint32_t)offsetof(Particle_Emitter_Entity, size_end), (uint32_t)sizeof(Particle_Emitter_Entity::size_end), 6u, -1, 0, -1},
  {"rotation_speed_min", FIELD_TYPE_F32, (uint32_t)offsetof(Particle_Emitter_Entity, rotation_speed_min), (uint32_t)sizeof(Particle_Emitter_Entity::rotation_speed_min), 6u, -1, 0, -1},
  {"rotation_speed_max", FIELD_TYPE_F32, (uint32_t)offsetof(Particle_Emitter_Entity, rotation_speed_max), (uint32_t)sizeof(Particle_Emitter_Entity::rotation_speed_max), 6u, -1, 0, -1},
  {"color_start", FIELD_TYPE_V3, (uint32_t)offsetof(Particle_Emitter_Entity, color_start), (uint32_t)sizeof(Particle_Emitter_Entity::color_start), 6u, -1, 0, -1},
  {"color_end", FIELD_TYPE_V3, (uint32_t)offsetof(Particle_Emitter_Entity, color_end), (uint32_t)sizeof(Particle_Emitter_Entity::color_end), 6u, -1, 0, -1},
  {"alpha_start", FIELD_TYPE_F32, (uint32_t)offsetof(Particle_Emitter_Entity, alpha_start), (uint32_t)sizeof(Particle_Emitter_Entity::alpha_start), 6u, -1, 0, -1},
  {"alpha_end", FIELD_TYPE_F32, (uint32_t)offsetof(Particle_Emitter_Entity, alpha_end), (uint32_t)sizeof(Particle_Emitter_Entity::alpha_end), 6u, -1, 0, -1},
  {"emitter_lifetime", FIELD_TYPE_F32, (uint32_t)offsetof(Particle_Emitter_Entity, emitter_lifetime), (uint32_t)sizeof(Particle_Emitter_Entity::emitter_lifetime), 6u, -1, 0, -1},
  {"parent_entity_id", FIELD_TYPE_U32, (uint32_t)offsetof(Particle_Emitter_Entity, parent_entity_id), (uint32_t)sizeof(Particle_Emitter_Entity::parent_entity_id), 0u, -1, 0, -1},
};

constexpr field_info_t Trigger_Volume_Entity_FIELDS[] = {
  {"entity_id", FIELD_TYPE_U32, (uint32_t)offsetof(Trigger_Volume_Entity, entity_id), (uint32_t)sizeof(Trigger_Volume_Entity::entity_id), 1u, -1, 0, -1},
  {"position", FIELD_TYPE_V3, (uint32_t)offsetof(Trigger_Volume_Entity, position), (uint32_t)sizeof(Trigger_Volume_Entity::position), 7u, -1, 0, -1},
  {"orientation", FIELD_TYPE_V3, (uint32_t)offsetof(Trigger_Volume_Entity, orientation), (uint32_t)sizeof(Trigger_Volume_Entity::orientation), 7u, -1, 0, -1},
  {"volume", FIELD_TYPE_COMPONENT, (uint32_t)offsetof(Trigger_Volume_Entity, volume), (uint32_t)sizeof(Trigger_Volume_Entity::volume), 0u, 0, 0, -1},
  {"action", FIELD_TYPE_ENUM, (uint32_t)offsetof(Trigger_Volume_Entity, action), (uint32_t)sizeof(Trigger_Volume_Entity::action), 6u, -1, 0, -1},
  {"fire_mode", FIELD_TYPE_ENUM, (uint32_t)offsetof(Trigger_Volume_Entity, fire_mode), (uint32_t)sizeof(Trigger_Volume_Entity::fire_mode), 6u, -1, 0, -1},
  {"param_target_name", FIELD_TYPE_STRING, (uint32_t)offsetof(Trigger_Volume_Entity, param_target_name), (uint32_t)sizeof(Trigger_Volume_Entity::param_target_name), 6u, -1, 64, -1},
  {"param_string", FIELD_TYPE_STRING, (uint32_t)offsetof(Trigger_Volume_Entity, param_string), (uint32_t)sizeof(Trigger_Volume_Entity::param_string), 6u, -1, 128, -1},
  {"param_float", FIELD_TYPE_F32, (uint32_t)offsetof(Trigger_Volume_Entity, param_float), (uint32_t)sizeof(Trigger_Volume_Entity::param_float), 6u, -1, 0, -1},
};

constexpr field_info_t Light_Entity_FIELDS[] = {
  {"entity_id", FIELD_TYPE_U32, (uint32_t)offsetof(Light_Entity, entity_id), (uint32_t)sizeof(Light_Entity::entity_id), 1u, -1, 0, -1},
  {"position", FIELD_TYPE_V3, (uint32_t)offsetof(Light_Entity, position), (uint32_t)sizeof(Light_Entity::position), 7u, -1, 0, -1},
  {"orientation", FIELD_TYPE_V3, (uint32_t)offsetof(Light_Entity, orientation), (uint32_t)sizeof(Light_Entity::orientation), 7u, -1, 0, -1},
  {"direction", FIELD_TYPE_V3, (uint32_t)offsetof(Light_Entity, direction), (uint32_t)sizeof(Light_Entity::direction), 6u, -1, 0, -1},
  {"color", FIELD_TYPE_V3, (uint32_t)offsetof(Light_Entity, color), (uint32_t)sizeof(Light_Entity::color), 6u, -1, 0, -1},
  {"intensity", FIELD_TYPE_F32, (uint32_t)offsetof(Light_Entity, intensity), (uint32_t)sizeof(Light_Entity::intensity), 6u, -1, 0, -1},
  {"range", FIELD_TYPE_F32, (uint32_t)offsetof(Light_Entity, range), (uint32_t)sizeof(Light_Entity::range), 6u, -1, 0, -1},
  {"spot_inner_degrees", FIELD_TYPE_F32, (uint32_t)offsetof(Light_Entity, spot_inner_degrees), (uint32_t)sizeof(Light_Entity::spot_inner_degrees), 6u, -1, 0, -1},
  {"spot_outer_degrees", FIELD_TYPE_F32, (uint32_t)offsetof(Light_Entity, spot_outer_degrees), (uint32_t)sizeof(Light_Entity::spot_outer_degrees), 6u, -1, 0, -1},
  {"kind", FIELD_TYPE_ENUM, (uint32_t)offsetof(Light_Entity, kind), (uint32_t)sizeof(Light_Entity::kind), 6u, -1, 0, -1},
};

constexpr field_info_t Physics_Body_Entity_FIELDS[] = {
  {"entity_id", FIELD_TYPE_U32, (uint32_t)offsetof(Physics_Body_Entity, entity_id), (uint32_t)sizeof(Physics_Body_Entity::entity_id), 1u, -1, 0, -1},
  {"position", FIELD_TYPE_V3, (uint32_t)offsetof(Physics_Body_Entity, position), (uint32_t)sizeof(Physics_Body_Entity::position), 7u, -1, 0, -1},
  {"orientation", FIELD_TYPE_V3, (uint32_t)offsetof(Physics_Body_Entity, orientation), (uint32_t)sizeof(Physics_Body_Entity::orientation), 7u, -1, 0, -1},
  {"shape", FIELD_TYPE_ENUM, (uint32_t)offsetof(Physics_Body_Entity, shape), (uint32_t)sizeof(Physics_Body_Entity::shape), 6u, -1, 0, -1},
  {"size", FIELD_TYPE_V3, (uint32_t)offsetof(Physics_Body_Entity, size), (uint32_t)sizeof(Physics_Body_Entity::size), 6u, -1, 0, -1},
  {"velocity", FIELD_TYPE_V3, (uint32_t)offsetof(Physics_Body_Entity, velocity), (uint32_t)sizeof(Physics_Body_Entity::velocity), 1u, -1, 0, -1},
  {"mass", FIELD_TYPE_F32, (uint32_t)offsetof(Physics_Body_Entity, mass), (uint32_t)sizeof(Physics_Body_Entity::mass), 6u, -1, 0, -1},
  {"render", FIELD_TYPE_COMPONENT, (uint32_t)offsetof(Physics_Body_Entity, render), (uint32_t)sizeof(Physics_Body_Entity::render), 0u, 2, 0, -1},
  {"hitbox", FIELD_TYPE_COMPONENT, (uint32_t)offsetof(Physics_Body_Entity, hitbox), (uint32_t)sizeof(Physics_Body_Entity::hitbox), 0u, 3, 0, -1},
};

constexpr component_type_info_t COMPONENT_INFOS[] = {
  {"Box_Volume", {Box_Volume_FIELDS, 2}, (uint32_t)sizeof(Box_Volume)},
  {"Material", {Material_FIELDS, 3}, (uint32_t)sizeof(Material)},
  {"Render", {Render_FIELDS, 7}, (uint32_t)sizeof(Render)},
  {"Hitbox", {Hitbox_FIELDS, 3}, (uint32_t)sizeof(Hitbox)},
};

Entity* construct_Player_Spawn_Entity(void* memory) { return new (memory) Player_Spawn_Entity(); }
Entity* construct_Player_Entity(void* memory) { return new (memory) Player_Entity(); }
Entity* construct_Weapon_Entity(void* memory) { return new (memory) Weapon_Entity(); }
Entity* construct_Rocket_Entity(void* memory) { return new (memory) Rocket_Entity(); }
Entity* construct_Particle_Emitter_Entity(void* memory) { return new (memory) Particle_Emitter_Entity(); }
Entity* construct_Trigger_Volume_Entity(void* memory) { return new (memory) Trigger_Volume_Entity(); }
Entity* construct_Light_Entity(void* memory) { return new (memory) Light_Entity(); }
Entity* construct_Physics_Body_Entity(void* memory) { return new (memory) Physics_Body_Entity(); }

constexpr entity_type_info_t ENTITY_INFOS[] = {
  {"", "", {}, 0, 0, 0, false, nullptr}, // Invalid
  {"player_spawn_entity", "Player Spawn", {Player_Spawn_Entity_FIELDS, 4}, (uint32_t)sizeof(Player_Spawn_Entity), (uint32_t)alignof(Player_Spawn_Entity), 0u, false, construct_Player_Spawn_Entity},
  {"player_entity", "Player", {Player_Entity_FIELDS, 12}, (uint32_t)sizeof(Player_Entity), (uint32_t)alignof(Player_Entity), 12u, true, construct_Player_Entity},
  {"weapon_entity", "Weapon", {Weapon_Entity_FIELDS, 6}, (uint32_t)sizeof(Weapon_Entity), (uint32_t)alignof(Weapon_Entity), 4u, true, construct_Weapon_Entity},
  {"rocket_entity", "Rocket", {Rocket_Entity_FIELDS, 11}, (uint32_t)sizeof(Rocket_Entity), (uint32_t)alignof(Rocket_Entity), 12u, true, construct_Rocket_Entity},
  {"particle_emitter_entity", "Particle Emitter", {Particle_Emitter_Entity_FIELDS, 23}, (uint32_t)sizeof(Particle_Emitter_Entity), (uint32_t)alignof(Particle_Emitter_Entity), 0u, false, construct_Particle_Emitter_Entity},
  {"trigger_volume_entity", "Trigger Volume", {Trigger_Volume_Entity_FIELDS, 9}, (uint32_t)sizeof(Trigger_Volume_Entity), (uint32_t)alignof(Trigger_Volume_Entity), 1u, false, construct_Trigger_Volume_Entity},
  {"light_entity", "Light", {Light_Entity_FIELDS, 10}, (uint32_t)sizeof(Light_Entity), (uint32_t)alignof(Light_Entity), 0u, false, construct_Light_Entity},
  {"physics_body_entity", "Physics Body", {Physics_Body_Entity_FIELDS, 9}, (uint32_t)sizeof(Physics_Body_Entity), (uint32_t)alignof(Physics_Body_Entity), 12u, false, construct_Physics_Body_Entity},
};

constexpr int32_t COMPONENT_OFFSETS[][4] = {
  {-1, -1, -1, -1}, // Invalid
  {-1, -1, -1, -1}, // Player_Spawn_Entity
  {-1, -1, (int32_t)offsetof(Player_Entity, render), (int32_t)offsetof(Player_Entity, hitbox)}, // Player_Entity
  {-1, -1, (int32_t)offsetof(Weapon_Entity, render), -1}, // Weapon_Entity
  {-1, -1, (int32_t)offsetof(Rocket_Entity, render), (int32_t)offsetof(Rocket_Entity, hitbox)}, // Rocket_Entity
  {-1, -1, -1, -1}, // Particle_Emitter_Entity
  {(int32_t)offsetof(Trigger_Volume_Entity, volume), -1, -1, -1}, // Trigger_Volume_Entity
  {-1, -1, -1, -1}, // Light_Entity
  {-1, -1, (int32_t)offsetof(Physics_Body_Entity, render), (int32_t)offsetof(Physics_Body_Entity, hitbox)}, // Physics_Body_Entity
};

constexpr uint32_t PLACEABLE_ENTITY_TYPE_COUNT = 5;
constexpr entity_type PLACEABLE_ENTITY_TYPES[] = {
  entity_type::Player_Spawn_Entity,
  entity_type::Particle_Emitter_Entity,
  entity_type::Trigger_Volume_Entity,
  entity_type::Light_Entity,
  entity_type::Physics_Body_Entity,
};

constexpr asset_info_t mesh_asset_MANIFEST[] = {
  {"Missing", "resources/obj/error.obj", ASSET_SOURCE_FILE},
  {"Isosphere", "resources/obj/isosphere.obj", ASSET_SOURCE_FILE},
  {"Pyramid", "resources/obj/pyramid.obj", ASSET_SOURCE_FILE},
  {"Box", "box", ASSET_SOURCE_PROCEDURAL},
  {"Arrow", "arrow", ASSET_SOURCE_PROCEDURAL},
  {"Sphere", "sphere", ASSET_SOURCE_PROCEDURAL},
  {"Cylinder", "cylinder", ASSET_SOURCE_PROCEDURAL},
  {"Cone", "cone", ASSET_SOURCE_PROCEDURAL},
  {"Wedge", "wedge", ASSET_SOURCE_PROCEDURAL},
};

constexpr asset_info_t sprite_asset_MANIFEST[] = {
  {"Missing", "", ASSET_SOURCE_MISSING},
  {"Smoke", "resources/sprites/smoke.png", ASSET_SOURCE_FILE},
};

} // namespace

Span<const asset_info_t> mesh_asset_manifest()
{
  return {mesh_asset_MANIFEST, mesh_asset_COUNT};
}

const char* to_string(mesh_asset value)
{
  assert((uint32_t)value < mesh_asset_COUNT);
  return mesh_asset_MANIFEST[(uint16_t)value].name;
}

bool from_string(const char* text, mesh_asset* out_value)
{
  for (uint32_t index = 0; index < mesh_asset_COUNT; ++index)
  {
    if (strcmp(mesh_asset_MANIFEST[index].name, text) != 0)
      continue;
    *out_value = (mesh_asset)index;
    return true;
  }
  return false;
}

Span<const asset_info_t> sprite_asset_manifest()
{
  return {sprite_asset_MANIFEST, sprite_asset_COUNT};
}

const char* to_string(sprite_asset value)
{
  assert((uint32_t)value < sprite_asset_COUNT);
  return sprite_asset_MANIFEST[(uint16_t)value].name;
}

bool from_string(const char* text, sprite_asset* out_value)
{
  for (uint32_t index = 0; index < sprite_asset_COUNT; ++index)
  {
    if (strcmp(sprite_asset_MANIFEST[index].name, text) != 0)
      continue;
    *out_value = (sprite_asset)index;
    return true;
  }
  return false;
}

const char* to_string(Light_Type value)
{
  switch (value)
  {
    case Light_Type::Point: return "Point";
    case Light_Type::Spot: return "Spot";
    case Light_Type::Directional: return "Directional";
  }
  assert(false && "invalid Light_Type");
  return "";
}

bool from_string(const char* text, Light_Type* out_value)
{
  if (strcmp(text, "Point") == 0) { *out_value = Light_Type::Point; return true; }
  if (strcmp(text, "Spot") == 0) { *out_value = Light_Type::Spot; return true; }
  if (strcmp(text, "Directional") == 0) { *out_value = Light_Type::Directional; return true; }
  return false;
}

const char* to_string(Spawn_Type value)
{
  switch (value)
  {
    case Spawn_Type::Human: return "Human";
    case Spawn_Type::Bot: return "Bot";
  }
  assert(false && "invalid Spawn_Type");
  return "";
}

bool from_string(const char* text, Spawn_Type* out_value)
{
  if (strcmp(text, "Human") == 0) { *out_value = Spawn_Type::Human; return true; }
  if (strcmp(text, "Bot") == 0) { *out_value = Spawn_Type::Bot; return true; }
  return false;
}

const char* to_string(Shader_Type value)
{
  switch (value)
  {
    case Shader_Type::Lit: return "Lit";
    case Shader_Type::Unlit: return "Unlit";
  }
  assert(false && "invalid Shader_Type");
  return "";
}

bool from_string(const char* text, Shader_Type* out_value)
{
  if (strcmp(text, "Lit") == 0) { *out_value = Shader_Type::Lit; return true; }
  if (strcmp(text, "Unlit") == 0) { *out_value = Shader_Type::Unlit; return true; }
  return false;
}

const char* to_string(Shape_Kind value)
{
  switch (value)
  {
    case Shape_Kind::Sphere: return "Sphere";
    case Shape_Kind::Capsule: return "Capsule";
    case Shape_Kind::Box: return "Box";
  }
  assert(false && "invalid Shape_Kind");
  return "";
}

bool from_string(const char* text, Shape_Kind* out_value)
{
  if (strcmp(text, "Sphere") == 0) { *out_value = Shape_Kind::Sphere; return true; }
  if (strcmp(text, "Capsule") == 0) { *out_value = Shape_Kind::Capsule; return true; }
  if (strcmp(text, "Box") == 0) { *out_value = Shape_Kind::Box; return true; }
  return false;
}

const char* to_string(Trigger_Action value)
{
  switch (value)
  {
    case Trigger_Action::Kill: return "Kill";
    case Trigger_Action::Set_Health: return "Set_Health";
    case Trigger_Action::Print_Message: return "Print_Message";
    case Trigger_Action::Warp_To_Spawn: return "Warp_To_Spawn";
  }
  assert(false && "invalid Trigger_Action");
  return "";
}

bool from_string(const char* text, Trigger_Action* out_value)
{
  if (strcmp(text, "Kill") == 0) { *out_value = Trigger_Action::Kill; return true; }
  if (strcmp(text, "Set_Health") == 0) { *out_value = Trigger_Action::Set_Health; return true; }
  if (strcmp(text, "Print_Message") == 0) { *out_value = Trigger_Action::Print_Message; return true; }
  if (strcmp(text, "Warp_To_Spawn") == 0) { *out_value = Trigger_Action::Warp_To_Spawn; return true; }
  return false;
}

const char* to_string(Fire_Mode value)
{
  switch (value)
  {
    case Fire_Mode::On_Enter: return "On_Enter";
    case Fire_Mode::Every_Tick: return "Every_Tick";
  }
  assert(false && "invalid Fire_Mode");
  return "";
}

bool from_string(const char* text, Fire_Mode* out_value)
{
  if (strcmp(text, "On_Enter") == 0) { *out_value = Fire_Mode::On_Enter; return true; }
  if (strcmp(text, "Every_Tick") == 0) { *out_value = Fire_Mode::Every_Tick; return true; }
  return false;
}

const entity_type_info_t& entity_info(entity_type type)
{
  assert(type > entity_type::Invalid && (uint32_t)type < ENTITY_TYPE_COUNT);
  return ENTITY_INFOS[(uint16_t)type];
}

const component_type_info_t& component_info(component_type component)
{
  assert((uint32_t)component < COMPONENT_TYPE_COUNT);
  return COMPONENT_INFOS[(uint16_t)component];
}

entity_type entity_type_from_classname(const char* classname)
{
  for (uint32_t index = 1; index < ENTITY_TYPE_COUNT; ++index)
  {
    if (strcmp(ENTITY_INFOS[index].classname, classname) == 0)
      return (entity_type)index;
  }
  return entity_type::Invalid;
}

bool has_component(entity_type type, component_type component)
{
  return (entity_info(type).component_mask & (1u << (uint16_t)component)) != 0;
}

int32_t component_byte_offset(entity_type type, component_type component)
{
  assert(type > entity_type::Invalid && (uint32_t)type < ENTITY_TYPE_COUNT);
  assert((uint32_t)component < COMPONENT_TYPE_COUNT);
  return COMPONENT_OFFSETS[(uint16_t)type][(uint16_t)component];
}

Entity* create_entity(entity_type type)
{
  switch (type)
  {
    case entity_type::Invalid: break;
    case entity_type::Player_Spawn_Entity: return new Player_Spawn_Entity();
    case entity_type::Player_Entity: return new Player_Entity();
    case entity_type::Weapon_Entity: return new Weapon_Entity();
    case entity_type::Rocket_Entity: return new Rocket_Entity();
    case entity_type::Particle_Emitter_Entity: return new Particle_Emitter_Entity();
    case entity_type::Trigger_Volume_Entity: return new Trigger_Volume_Entity();
    case entity_type::Light_Entity: return new Light_Entity();
    case entity_type::Physics_Body_Entity: return new Physics_Body_Entity();
  }
  assert(false && "create_entity: not a valid entity_type");
  return nullptr;
}

Entity* entity_from_classname(const char* classname)
{
  entity_type type = entity_type_from_classname(classname);
  if (type == entity_type::Invalid)
    return nullptr; // unknown classname: the caller reports it
  return create_entity(type);
}

void destroy_entity(Entity* entity)
{
  if (entity == nullptr)
    return;

  switch (entity->type)
  {
    case entity_type::Invalid: break;
    case entity_type::Player_Spawn_Entity: delete static_cast<Player_Spawn_Entity*>(entity); return;
    case entity_type::Player_Entity: delete static_cast<Player_Entity*>(entity); return;
    case entity_type::Weapon_Entity: delete static_cast<Weapon_Entity*>(entity); return;
    case entity_type::Rocket_Entity: delete static_cast<Rocket_Entity*>(entity); return;
    case entity_type::Particle_Emitter_Entity: delete static_cast<Particle_Emitter_Entity*>(entity); return;
    case entity_type::Trigger_Volume_Entity: delete static_cast<Trigger_Volume_Entity*>(entity); return;
    case entity_type::Light_Entity: delete static_cast<Light_Entity*>(entity); return;
    case entity_type::Physics_Body_Entity: delete static_cast<Physics_Body_Entity*>(entity); return;
  }
  assert(false && "destroy_entity: entity carries an invalid tag");
}

Span<const entity_type> placeable_entity_types()
{
  return {PLACEABLE_ENTITY_TYPES, PLACEABLE_ENTITY_TYPE_COUNT};
}

const uint32_t SCHEMA_HASH = 0x38f5102eu;

} // namespace entities
