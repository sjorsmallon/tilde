#pragma once

#include "../network/schema.hpp"

namespace network
{

// Material component — shader selection and surface properties.
struct material_t
{
  // "lit" (default) or "unlit" — selects the rendering pipeline.
  SCHEMA_FIELD_DEFAULT(pascal_string, shader_type,
                      Schema_Flags::Networked | Schema_Flags::Editable | Schema_Flags::Saveable,
                      "lit");
  // Base surface color (RGB, 0-1 range).
  SCHEMA_FIELD_DEFAULT(vec3f, color,
                      Schema_Flags::Networked | Schema_Flags::Editable | Schema_Flags::Saveable,
                      (vec3f{1, 1, 1}));
  // Roughness (unused for now, reserved for future PBR).
  SCHEMA_FIELD_DEFAULT(float32, roughness,
                      Schema_Flags::Networked | Schema_Flags::Editable | Schema_Flags::Saveable,
                      0.5f);

  DECLARE_COMPONENT_SCHEMA(material_t)
};

SCHEMA_NAME_FOR_TYPE(material_t)

template <> struct Schema_Type_Info<material_t>
{
  static constexpr Field_Type type = Field_Type::NestedSchema;
};

// Render component — embeddable in any entity via SCHEMA_FIELD.
// Bundles mesh reference, visibility, and a local transform.
// This is now a composable schema component!
struct render_component_t
{
  SCHEMA_FIELD_DEFAULT(int32, mesh_id,
                      Schema_Flags::Networked | Schema_Flags::Editable | Schema_Flags::Saveable,
                      -1);
  SCHEMA_FIELD(pascal_string, mesh_path,
              Schema_Flags::Networked | Schema_Flags::Editable | Schema_Flags::Saveable);
  SCHEMA_FIELD_DEFAULT(bool, visible,
                      Schema_Flags::Networked | Schema_Flags::Editable | Schema_Flags::Saveable,
                      true);
  SCHEMA_FIELD_DEFAULT(bool, is_wireframe,
                      Schema_Flags::Networked | Schema_Flags::Editable | Schema_Flags::Saveable,
                      false);
  SCHEMA_FIELD_DEFAULT(vec3f, offset,
                      Schema_Flags::Networked | Schema_Flags::Editable | Schema_Flags::Saveable,
                      (vec3f{0, 0, 0}));
  SCHEMA_FIELD_DEFAULT(vec3f, scale,
                      Schema_Flags::Networked | Schema_Flags::Editable | Schema_Flags::Saveable,
                      (vec3f{1, 1, 1}));
  SCHEMA_FIELD_DEFAULT(vec3f, rotation,
                      Schema_Flags::Networked | Schema_Flags::Editable | Schema_Flags::Saveable,
                      (vec3f{0, 0, 0}));
  SCHEMA_FIELD(material_t, material,
              Schema_Flags::Networked | Schema_Flags::Editable | Schema_Flags::Saveable);

  DECLARE_COMPONENT_SCHEMA(render_component_t)
};

// Schema name registration (must be at namespace scope)
SCHEMA_NAME_FOR_TYPE(render_component_t)

// Explicit Schema_Type_Info specialization (for backward compat and clarity)
template <> struct Schema_Type_Info<render_component_t>
{
  static constexpr Field_Type type = Field_Type::NestedSchema;
};

// Hitbox component — defines combat/interaction collision bounds.
// Separate from physics collision used in player_move.
// Used for: projectile hits, melee attacks, damage areas.
struct hitbox_component_t
{
  // Shape type: "sphere", "capsule", "aabb"
  SCHEMA_FIELD_DEFAULT(pascal_string, shape_type,
                      Schema_Flags::Networked | Schema_Flags::Editable | Schema_Flags::Saveable,
                      "sphere");

  // Size interpretation depends on shape:
  // - sphere: x = radius, y/z ignored
  // - capsule: x/z = radius, y = half_height (cylinder portion only, caps add radius to height)
  // - aabb: x/y/z = half_extents
  SCHEMA_FIELD_DEFAULT(vec3f, size,
                      Schema_Flags::Networked | Schema_Flags::Editable | Schema_Flags::Saveable,
                      (vec3f{8, 8, 8}));

  // Local offset from entity position (useful for non-centered hitboxes)
  SCHEMA_FIELD_DEFAULT(vec3f, offset,
                      Schema_Flags::Networked | Schema_Flags::Editable | Schema_Flags::Saveable,
                      (vec3f{0, 0, 0}));

  DECLARE_COMPONENT_SCHEMA(hitbox_component_t)
};

// Schema name registration (must be at namespace scope)
SCHEMA_NAME_FOR_TYPE(hitbox_component_t)

// Explicit Schema_Type_Info specialization
template <> struct Schema_Type_Info<hitbox_component_t>
{
  static constexpr Field_Type type = Field_Type::NestedSchema;
};

// --- Helper functions ---

// Set up a render component to use a procedurally generated primitive.
// Available primitives: "box", "arrow", "sphere", "cylinder", "cone", "pyramid", "wedge"
// The 'size' parameter controls the scale (primitives are generated at unit size).
void set_primitive_render(render_component_t& rc, const char* primitive_name,
                          vec3f size = vec3f{1, 1, 1});

// --- Hitbox collision helpers ---

// Test sphere vs sphere collision
// sphere_a_pos/radius: first sphere
// sphere_b_pos/radius: second sphere
// Returns true if spheres overlap
bool test_sphere_sphere(const vec3f& sphere_a_pos, float radius_a,
                        const vec3f& sphere_b_pos, float radius_b);

// Test sphere vs capsule collision
// Capsule is defined as a line segment (p0 to p1) with radius
// sphere_pos/sphere_radius: the sphere
// capsule_p0/p1: endpoints of capsule's central line segment
// capsule_radius: radius of the capsule
// Returns true if sphere and capsule overlap
bool test_sphere_capsule(const vec3f& sphere_pos, float sphere_radius,
                         const vec3f& capsule_p0, const vec3f& capsule_p1,
                         float capsule_radius);

// Test sphere vs AABB collision
bool test_sphere_aabb(const vec3f& sphere_pos, float sphere_radius,
                      const vec3f& aabb_min, const vec3f& aabb_max);

// High-level hitbox vs hitbox test
// Tests collision between two entities with hitbox components
// entity_a_pos: world position of entity A
// hitbox_a: hitbox component of entity A
// entity_b_pos: world position of entity B
// hitbox_b: hitbox component of entity B
// Returns true if hitboxes overlap
bool test_hitbox_collision(const vec3f& entity_a_pos, const hitbox_component_t& hitbox_a,
                           const vec3f& entity_b_pos, const hitbox_component_t& hitbox_b);

} // namespace network
