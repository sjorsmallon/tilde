// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/entities/entities.def by entity_gen. Do not edit.
#include "entities_generated.hpp"
#include <cassert>
#include <cstddef>
#include <cstring>

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
  {"position", FIELD_TYPE_V3, (uint32_t)offsetof(Box_Volume, position), (uint32_t)sizeof(Box_Volume::position), 7u, -1, 0},
  {"half_extents", FIELD_TYPE_V3, (uint32_t)offsetof(Box_Volume, half_extents), (uint32_t)sizeof(Box_Volume::half_extents), 7u, -1, 0},
};

constexpr field_info_t Material_FIELDS[] = {
  {"shader_type", FIELD_TYPE_ENUM, (uint32_t)offsetof(Material, shader_type), (uint32_t)sizeof(Material::shader_type), 7u, -1, 0},
  {"color", FIELD_TYPE_V3, (uint32_t)offsetof(Material, color), (uint32_t)sizeof(Material::color), 7u, -1, 0},
  {"roughness", FIELD_TYPE_F32, (uint32_t)offsetof(Material, roughness), (uint32_t)sizeof(Material::roughness), 7u, -1, 0},
};

constexpr field_info_t Render_FIELDS[] = {
  {"mesh", FIELD_TYPE_ASSET, (uint32_t)offsetof(Render, mesh), (uint32_t)sizeof(Render::mesh), 7u, -1, 0},
  {"visible", FIELD_TYPE_BOOL, (uint32_t)offsetof(Render, visible), (uint32_t)sizeof(Render::visible), 7u, -1, 0},
  {"is_wireframe", FIELD_TYPE_BOOL, (uint32_t)offsetof(Render, is_wireframe), (uint32_t)sizeof(Render::is_wireframe), 7u, -1, 0},
  {"offset", FIELD_TYPE_V3, (uint32_t)offsetof(Render, offset), (uint32_t)sizeof(Render::offset), 7u, -1, 0},
  {"scale", FIELD_TYPE_V3, (uint32_t)offsetof(Render, scale), (uint32_t)sizeof(Render::scale), 7u, -1, 0},
  {"rotation", FIELD_TYPE_V3, (uint32_t)offsetof(Render, rotation), (uint32_t)sizeof(Render::rotation), 7u, -1, 0},
  {"material", FIELD_TYPE_COMPONENT, (uint32_t)offsetof(Render, material), (uint32_t)sizeof(Render::material), 0u, 1, 0},
};

constexpr field_info_t Hitbox_FIELDS[] = {
  {"shape", FIELD_TYPE_ENUM, (uint32_t)offsetof(Hitbox, shape), (uint32_t)sizeof(Hitbox::shape), 7u, -1, 0},
  {"size", FIELD_TYPE_V3, (uint32_t)offsetof(Hitbox, size), (uint32_t)sizeof(Hitbox::size), 7u, -1, 0},
  {"offset", FIELD_TYPE_V3, (uint32_t)offsetof(Hitbox, offset), (uint32_t)sizeof(Hitbox::offset), 7u, -1, 0},
};

constexpr field_info_t Player_Spawn_Entity_FIELDS[] = {
  {"entity_id", FIELD_TYPE_U32, (uint32_t)offsetof(Player_Spawn_Entity, entity_id), (uint32_t)sizeof(Player_Spawn_Entity::entity_id), 1u, -1, 0},
  {"position", FIELD_TYPE_V3, (uint32_t)offsetof(Player_Spawn_Entity, position), (uint32_t)sizeof(Player_Spawn_Entity::position), 7u, -1, 0},
  {"orientation", FIELD_TYPE_V3, (uint32_t)offsetof(Player_Spawn_Entity, orientation), (uint32_t)sizeof(Player_Spawn_Entity::orientation), 7u, -1, 0},
  {"spawn_type", FIELD_TYPE_ENUM, (uint32_t)offsetof(Player_Spawn_Entity, spawn_type), (uint32_t)sizeof(Player_Spawn_Entity::spawn_type), 6u, -1, 0},
};

constexpr field_info_t Player_Entity_FIELDS[] = {
  {"entity_id", FIELD_TYPE_U32, (uint32_t)offsetof(Player_Entity, entity_id), (uint32_t)sizeof(Player_Entity::entity_id), 1u, -1, 0},
  {"position", FIELD_TYPE_V3, (uint32_t)offsetof(Player_Entity, position), (uint32_t)sizeof(Player_Entity::position), 7u, -1, 0},
  {"orientation", FIELD_TYPE_V3, (uint32_t)offsetof(Player_Entity, orientation), (uint32_t)sizeof(Player_Entity::orientation), 7u, -1, 0},
  {"view_angle_yaw", FIELD_TYPE_F32, (uint32_t)offsetof(Player_Entity, view_angle_yaw), (uint32_t)sizeof(Player_Entity::view_angle_yaw), 3u, -1, 0},
  {"view_angle_pitch", FIELD_TYPE_F32, (uint32_t)offsetof(Player_Entity, view_angle_pitch), (uint32_t)sizeof(Player_Entity::view_angle_pitch), 3u, -1, 0},
  {"health", FIELD_TYPE_I32, (uint32_t)offsetof(Player_Entity, health), (uint32_t)sizeof(Player_Entity::health), 3u, -1, 0},
  {"ammo", FIELD_TYPE_I32, (uint32_t)offsetof(Player_Entity, ammo), (uint32_t)sizeof(Player_Entity::ammo), 3u, -1, 0},
  {"active_weapon_id", FIELD_TYPE_I32, (uint32_t)offsetof(Player_Entity, active_weapon_id), (uint32_t)sizeof(Player_Entity::active_weapon_id), 1u, -1, 0},
  {"client_slot_index", FIELD_TYPE_I32, (uint32_t)offsetof(Player_Entity, client_slot_index), (uint32_t)sizeof(Player_Entity::client_slot_index), 1u, -1, 0},
  {"velocity", FIELD_TYPE_V3, (uint32_t)offsetof(Player_Entity, velocity), (uint32_t)sizeof(Player_Entity::velocity), 1u, -1, 0},
  {"render", FIELD_TYPE_COMPONENT, (uint32_t)offsetof(Player_Entity, render), (uint32_t)sizeof(Player_Entity::render), 0u, 2, 0},
  {"hitbox", FIELD_TYPE_COMPONENT, (uint32_t)offsetof(Player_Entity, hitbox), (uint32_t)sizeof(Player_Entity::hitbox), 0u, 3, 0},
};

constexpr field_info_t Weapon_Entity_FIELDS[] = {
  {"entity_id", FIELD_TYPE_U32, (uint32_t)offsetof(Weapon_Entity, entity_id), (uint32_t)sizeof(Weapon_Entity::entity_id), 1u, -1, 0},
  {"position", FIELD_TYPE_V3, (uint32_t)offsetof(Weapon_Entity, position), (uint32_t)sizeof(Weapon_Entity::position), 7u, -1, 0},
  {"orientation", FIELD_TYPE_V3, (uint32_t)offsetof(Weapon_Entity, orientation), (uint32_t)sizeof(Weapon_Entity::orientation), 7u, -1, 0},
  {"ammo", FIELD_TYPE_I32, (uint32_t)offsetof(Weapon_Entity, ammo), (uint32_t)sizeof(Weapon_Entity::ammo), 3u, -1, 0},
  {"active_weapon_id", FIELD_TYPE_I32, (uint32_t)offsetof(Weapon_Entity, active_weapon_id), (uint32_t)sizeof(Weapon_Entity::active_weapon_id), 1u, -1, 0},
  {"render", FIELD_TYPE_COMPONENT, (uint32_t)offsetof(Weapon_Entity, render), (uint32_t)sizeof(Weapon_Entity::render), 0u, 2, 0},
};

constexpr field_info_t Rocket_Entity_FIELDS[] = {
  {"entity_id", FIELD_TYPE_U32, (uint32_t)offsetof(Rocket_Entity, entity_id), (uint32_t)sizeof(Rocket_Entity::entity_id), 1u, -1, 0},
  {"position", FIELD_TYPE_V3, (uint32_t)offsetof(Rocket_Entity, position), (uint32_t)sizeof(Rocket_Entity::position), 7u, -1, 0},
  {"orientation", FIELD_TYPE_V3, (uint32_t)offsetof(Rocket_Entity, orientation), (uint32_t)sizeof(Rocket_Entity::orientation), 7u, -1, 0},
  {"velocity", FIELD_TYPE_V3, (uint32_t)offsetof(Rocket_Entity, velocity), (uint32_t)sizeof(Rocket_Entity::velocity), 3u, -1, 0},
  {"lifetime", FIELD_TYPE_F32, (uint32_t)offsetof(Rocket_Entity, lifetime), (uint32_t)sizeof(Rocket_Entity::lifetime), 3u, -1, 0},
  {"damage_radius", FIELD_TYPE_F32, (uint32_t)offsetof(Rocket_Entity, damage_radius), (uint32_t)sizeof(Rocket_Entity::damage_radius), 3u, -1, 0},
  {"damage_amount", FIELD_TYPE_F32, (uint32_t)offsetof(Rocket_Entity, damage_amount), (uint32_t)sizeof(Rocket_Entity::damage_amount), 3u, -1, 0},
  {"knockback_force", FIELD_TYPE_F32, (uint32_t)offsetof(Rocket_Entity, knockback_force), (uint32_t)sizeof(Rocket_Entity::knockback_force), 3u, -1, 0},
  {"owner_id", FIELD_TYPE_U32, (uint32_t)offsetof(Rocket_Entity, owner_id), (uint32_t)sizeof(Rocket_Entity::owner_id), 1u, -1, 0},
  {"render", FIELD_TYPE_COMPONENT, (uint32_t)offsetof(Rocket_Entity, render), (uint32_t)sizeof(Rocket_Entity::render), 0u, 2, 0},
  {"hitbox", FIELD_TYPE_COMPONENT, (uint32_t)offsetof(Rocket_Entity, hitbox), (uint32_t)sizeof(Rocket_Entity::hitbox), 0u, 3, 0},
};

constexpr field_info_t Particle_Emitter_Entity_FIELDS[] = {
  {"entity_id", FIELD_TYPE_U32, (uint32_t)offsetof(Particle_Emitter_Entity, entity_id), (uint32_t)sizeof(Particle_Emitter_Entity::entity_id), 1u, -1, 0},
  {"position", FIELD_TYPE_V3, (uint32_t)offsetof(Particle_Emitter_Entity, position), (uint32_t)sizeof(Particle_Emitter_Entity::position), 7u, -1, 0},
  {"orientation", FIELD_TYPE_V3, (uint32_t)offsetof(Particle_Emitter_Entity, orientation), (uint32_t)sizeof(Particle_Emitter_Entity::orientation), 7u, -1, 0},
  {"sprite", FIELD_TYPE_ASSET, (uint32_t)offsetof(Particle_Emitter_Entity, sprite), (uint32_t)sizeof(Particle_Emitter_Entity::sprite), 6u, -1, 0},
  {"emit_rate", FIELD_TYPE_F32, (uint32_t)offsetof(Particle_Emitter_Entity, emit_rate), (uint32_t)sizeof(Particle_Emitter_Entity::emit_rate), 6u, -1, 0},
  {"max_particles", FIELD_TYPE_I32, (uint32_t)offsetof(Particle_Emitter_Entity, max_particles), (uint32_t)sizeof(Particle_Emitter_Entity::max_particles), 6u, -1, 0},
  {"lifetime_min", FIELD_TYPE_F32, (uint32_t)offsetof(Particle_Emitter_Entity, lifetime_min), (uint32_t)sizeof(Particle_Emitter_Entity::lifetime_min), 6u, -1, 0},
  {"lifetime_max", FIELD_TYPE_F32, (uint32_t)offsetof(Particle_Emitter_Entity, lifetime_max), (uint32_t)sizeof(Particle_Emitter_Entity::lifetime_max), 6u, -1, 0},
  {"velocity_min", FIELD_TYPE_F32, (uint32_t)offsetof(Particle_Emitter_Entity, velocity_min), (uint32_t)sizeof(Particle_Emitter_Entity::velocity_min), 6u, -1, 0},
  {"velocity_max", FIELD_TYPE_F32, (uint32_t)offsetof(Particle_Emitter_Entity, velocity_max), (uint32_t)sizeof(Particle_Emitter_Entity::velocity_max), 6u, -1, 0},
  {"spread", FIELD_TYPE_F32, (uint32_t)offsetof(Particle_Emitter_Entity, spread), (uint32_t)sizeof(Particle_Emitter_Entity::spread), 6u, -1, 0},
  {"gravity", FIELD_TYPE_V3, (uint32_t)offsetof(Particle_Emitter_Entity, gravity), (uint32_t)sizeof(Particle_Emitter_Entity::gravity), 6u, -1, 0},
  {"drag", FIELD_TYPE_F32, (uint32_t)offsetof(Particle_Emitter_Entity, drag), (uint32_t)sizeof(Particle_Emitter_Entity::drag), 6u, -1, 0},
  {"size_start", FIELD_TYPE_F32, (uint32_t)offsetof(Particle_Emitter_Entity, size_start), (uint32_t)sizeof(Particle_Emitter_Entity::size_start), 6u, -1, 0},
  {"size_end", FIELD_TYPE_F32, (uint32_t)offsetof(Particle_Emitter_Entity, size_end), (uint32_t)sizeof(Particle_Emitter_Entity::size_end), 6u, -1, 0},
  {"rotation_speed_min", FIELD_TYPE_F32, (uint32_t)offsetof(Particle_Emitter_Entity, rotation_speed_min), (uint32_t)sizeof(Particle_Emitter_Entity::rotation_speed_min), 6u, -1, 0},
  {"rotation_speed_max", FIELD_TYPE_F32, (uint32_t)offsetof(Particle_Emitter_Entity, rotation_speed_max), (uint32_t)sizeof(Particle_Emitter_Entity::rotation_speed_max), 6u, -1, 0},
  {"color_start", FIELD_TYPE_V3, (uint32_t)offsetof(Particle_Emitter_Entity, color_start), (uint32_t)sizeof(Particle_Emitter_Entity::color_start), 6u, -1, 0},
  {"color_end", FIELD_TYPE_V3, (uint32_t)offsetof(Particle_Emitter_Entity, color_end), (uint32_t)sizeof(Particle_Emitter_Entity::color_end), 6u, -1, 0},
  {"alpha_start", FIELD_TYPE_F32, (uint32_t)offsetof(Particle_Emitter_Entity, alpha_start), (uint32_t)sizeof(Particle_Emitter_Entity::alpha_start), 6u, -1, 0},
  {"alpha_end", FIELD_TYPE_F32, (uint32_t)offsetof(Particle_Emitter_Entity, alpha_end), (uint32_t)sizeof(Particle_Emitter_Entity::alpha_end), 6u, -1, 0},
  {"emitter_lifetime", FIELD_TYPE_F32, (uint32_t)offsetof(Particle_Emitter_Entity, emitter_lifetime), (uint32_t)sizeof(Particle_Emitter_Entity::emitter_lifetime), 3u, -1, 0},
  {"parent_entity_id", FIELD_TYPE_U32, (uint32_t)offsetof(Particle_Emitter_Entity, parent_entity_id), (uint32_t)sizeof(Particle_Emitter_Entity::parent_entity_id), 3u, -1, 0},
};

constexpr field_info_t Trigger_Volume_Entity_FIELDS[] = {
  {"entity_id", FIELD_TYPE_U32, (uint32_t)offsetof(Trigger_Volume_Entity, entity_id), (uint32_t)sizeof(Trigger_Volume_Entity::entity_id), 1u, -1, 0},
  {"position", FIELD_TYPE_V3, (uint32_t)offsetof(Trigger_Volume_Entity, position), (uint32_t)sizeof(Trigger_Volume_Entity::position), 7u, -1, 0},
  {"orientation", FIELD_TYPE_V3, (uint32_t)offsetof(Trigger_Volume_Entity, orientation), (uint32_t)sizeof(Trigger_Volume_Entity::orientation), 7u, -1, 0},
  {"volume", FIELD_TYPE_COMPONENT, (uint32_t)offsetof(Trigger_Volume_Entity, volume), (uint32_t)sizeof(Trigger_Volume_Entity::volume), 2u, 0, 0},
  {"action", FIELD_TYPE_ENUM, (uint32_t)offsetof(Trigger_Volume_Entity, action), (uint32_t)sizeof(Trigger_Volume_Entity::action), 6u, -1, 0},
  {"fire_mode", FIELD_TYPE_ENUM, (uint32_t)offsetof(Trigger_Volume_Entity, fire_mode), (uint32_t)sizeof(Trigger_Volume_Entity::fire_mode), 6u, -1, 0},
  {"param_target_name", FIELD_TYPE_STRING, (uint32_t)offsetof(Trigger_Volume_Entity, param_target_name), (uint32_t)sizeof(Trigger_Volume_Entity::param_target_name), 6u, -1, 64},
  {"param_string", FIELD_TYPE_STRING, (uint32_t)offsetof(Trigger_Volume_Entity, param_string), (uint32_t)sizeof(Trigger_Volume_Entity::param_string), 6u, -1, 64},
  {"param_float", FIELD_TYPE_F32, (uint32_t)offsetof(Trigger_Volume_Entity, param_float), (uint32_t)sizeof(Trigger_Volume_Entity::param_float), 6u, -1, 0},
};

constexpr field_info_t Light_Entity_FIELDS[] = {
  {"entity_id", FIELD_TYPE_U32, (uint32_t)offsetof(Light_Entity, entity_id), (uint32_t)sizeof(Light_Entity::entity_id), 1u, -1, 0},
  {"position", FIELD_TYPE_V3, (uint32_t)offsetof(Light_Entity, position), (uint32_t)sizeof(Light_Entity::position), 7u, -1, 0},
  {"orientation", FIELD_TYPE_V3, (uint32_t)offsetof(Light_Entity, orientation), (uint32_t)sizeof(Light_Entity::orientation), 7u, -1, 0},
  {"direction", FIELD_TYPE_V3, (uint32_t)offsetof(Light_Entity, direction), (uint32_t)sizeof(Light_Entity::direction), 7u, -1, 0},
  {"color", FIELD_TYPE_V3, (uint32_t)offsetof(Light_Entity, color), (uint32_t)sizeof(Light_Entity::color), 7u, -1, 0},
  {"intensity", FIELD_TYPE_F32, (uint32_t)offsetof(Light_Entity, intensity), (uint32_t)sizeof(Light_Entity::intensity), 7u, -1, 0},
  {"range", FIELD_TYPE_F32, (uint32_t)offsetof(Light_Entity, range), (uint32_t)sizeof(Light_Entity::range), 7u, -1, 0},
  {"spot_inner_degrees", FIELD_TYPE_F32, (uint32_t)offsetof(Light_Entity, spot_inner_degrees), (uint32_t)sizeof(Light_Entity::spot_inner_degrees), 7u, -1, 0},
  {"spot_outer_degrees", FIELD_TYPE_F32, (uint32_t)offsetof(Light_Entity, spot_outer_degrees), (uint32_t)sizeof(Light_Entity::spot_outer_degrees), 7u, -1, 0},
  {"kind", FIELD_TYPE_ENUM, (uint32_t)offsetof(Light_Entity, kind), (uint32_t)sizeof(Light_Entity::kind), 7u, -1, 0},
};

constexpr field_info_t Physics_Body_Entity_FIELDS[] = {
  {"entity_id", FIELD_TYPE_U32, (uint32_t)offsetof(Physics_Body_Entity, entity_id), (uint32_t)sizeof(Physics_Body_Entity::entity_id), 1u, -1, 0},
  {"position", FIELD_TYPE_V3, (uint32_t)offsetof(Physics_Body_Entity, position), (uint32_t)sizeof(Physics_Body_Entity::position), 7u, -1, 0},
  {"orientation", FIELD_TYPE_V3, (uint32_t)offsetof(Physics_Body_Entity, orientation), (uint32_t)sizeof(Physics_Body_Entity::orientation), 7u, -1, 0},
  {"shape", FIELD_TYPE_ENUM, (uint32_t)offsetof(Physics_Body_Entity, shape), (uint32_t)sizeof(Physics_Body_Entity::shape), 7u, -1, 0},
  {"size", FIELD_TYPE_V3, (uint32_t)offsetof(Physics_Body_Entity, size), (uint32_t)sizeof(Physics_Body_Entity::size), 7u, -1, 0},
  {"velocity", FIELD_TYPE_V3, (uint32_t)offsetof(Physics_Body_Entity, velocity), (uint32_t)sizeof(Physics_Body_Entity::velocity), 1u, -1, 0},
  {"mass", FIELD_TYPE_F32, (uint32_t)offsetof(Physics_Body_Entity, mass), (uint32_t)sizeof(Physics_Body_Entity::mass), 6u, -1, 0},
  {"render", FIELD_TYPE_COMPONENT, (uint32_t)offsetof(Physics_Body_Entity, render), (uint32_t)sizeof(Physics_Body_Entity::render), 0u, 2, 0},
  {"hitbox", FIELD_TYPE_COMPONENT, (uint32_t)offsetof(Physics_Body_Entity, hitbox), (uint32_t)sizeof(Physics_Body_Entity::hitbox), 0u, 3, 0},
};

constexpr component_type_info_t COMPONENT_INFOS[] = {
  {"Box_Volume", Box_Volume_FIELDS, 2, (uint32_t)sizeof(Box_Volume)},
  {"Material", Material_FIELDS, 3, (uint32_t)sizeof(Material)},
  {"Render", Render_FIELDS, 7, (uint32_t)sizeof(Render)},
  {"Hitbox", Hitbox_FIELDS, 3, (uint32_t)sizeof(Hitbox)},
};

constexpr entity_type_info_t ENTITY_INFOS[] = {
  {"", "", nullptr, 0, 0, 0, 0, false}, // Invalid
  {"player_spawn_entity", "Player Spawn", Player_Spawn_Entity_FIELDS, 4, (uint32_t)sizeof(Player_Spawn_Entity), (uint32_t)alignof(Player_Spawn_Entity), 0u, false},
  {"player_entity", "Player", Player_Entity_FIELDS, 12, (uint32_t)sizeof(Player_Entity), (uint32_t)alignof(Player_Entity), 12u, true},
  {"weapon_entity", "Weapon", Weapon_Entity_FIELDS, 6, (uint32_t)sizeof(Weapon_Entity), (uint32_t)alignof(Weapon_Entity), 4u, true},
  {"rocket_entity", "Rocket", Rocket_Entity_FIELDS, 11, (uint32_t)sizeof(Rocket_Entity), (uint32_t)alignof(Rocket_Entity), 12u, true},
  {"particle_emitter_entity", "Particle Emitter", Particle_Emitter_Entity_FIELDS, 23, (uint32_t)sizeof(Particle_Emitter_Entity), (uint32_t)alignof(Particle_Emitter_Entity), 0u, false},
  {"trigger_volume_entity", "Trigger Volume", Trigger_Volume_Entity_FIELDS, 9, (uint32_t)sizeof(Trigger_Volume_Entity), (uint32_t)alignof(Trigger_Volume_Entity), 1u, false},
  {"light_entity", "Light", Light_Entity_FIELDS, 10, (uint32_t)sizeof(Light_Entity), (uint32_t)alignof(Light_Entity), 0u, false},
  {"physics_body_entity", "Physics Body", Physics_Body_Entity_FIELDS, 9, (uint32_t)sizeof(Physics_Body_Entity), (uint32_t)alignof(Physics_Body_Entity), 12u, false},
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

} // namespace

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
  assert(type > entity_type::Invalid && type < entity_type::Count);
  return ENTITY_INFOS[(uint16_t)type];
}

const component_type_info_t& component_info(component_type component)
{
  assert(component < component_type::Count);
  return COMPONENT_INFOS[(uint16_t)component];
}

entity_type entity_type_from_classname(const char* classname)
{
  for (uint16_t index = 1; index < (uint16_t)entity_type::Count; ++index)
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
  assert(type > entity_type::Invalid && type < entity_type::Count);
  assert(component < component_type::Count);
  return COMPONENT_OFFSETS[(uint16_t)type][(uint16_t)component];
}

const uint32_t SCHEMA_HASH = 0x9eb28bceu;

} // namespace entities
