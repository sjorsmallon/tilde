#pragma once

// The component STRUCTS are generated now (entities::Box_Volume, Material,
// Render, Hitbox in entities.def). What is left here is the behavior that was
// never part of the declaration: hitbox overlap tests.
//
// The declarations that used to live here died with the macro system:
//   material_t          -> entities::Material
//   render_component_t  -> entities::Render  (mesh_path string -> mesh id)
//   hitbox_component_t  -> entities::Hitbox  (shape_type string -> Shape_Kind)
//   set_primitive_render -> assign render.mesh directly; there is no lazy
//                           primitive init left to trigger.

#include "../entities/generated/entities_generated.hpp"
#include "../network/network_types.hpp"

namespace shared
{

using network::vec3f;

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

// High-level hitbox vs hitbox test.
//
// Every pairing of the three shapes is now handled. Under the string-typed
// shape this dispatched on strcmp against "sphere"/"capsule"/"aabb", and a
// physics cube -- whose hitbox was written the string "box" from the same
// source as its Jolt shape -- fell through every comparison to `return false`
// and could never be hit. The merged Shape_Kind enum makes that state
// unrepresentable; cubes becoming hittable is the visible consequence.
bool test_hitbox_collision(const vec3f& entity_a_pos, const entities::Hitbox& hitbox_a,
                           const vec3f& entity_b_pos, const entities::Hitbox& hitbox_b);

} // namespace shared
