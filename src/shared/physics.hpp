#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include "entity_uid.hpp"
#include "linalg.hpp"

#include <map>
#include <vector>

// these patterns are a bit verbose but they mirror Jolt's internal design
// Object layers: what kind of thing is a body?
namespace Physics_Layers
{
    static constexpr JPH::ObjectLayer STATIC   = 0; // non-moving world geometry
    static constexpr JPH::ObjectLayer DYNAMIC  = 1; // players, props, projectiles
    static constexpr JPH::uint        COUNT      = 2;
}

// Broad-phase layers: coarser bucketing used by the BVH tree.
namespace Broad_Phase_Layers
{
    static constexpr JPH::BroadPhaseLayer STATIC  {0};
    static constexpr JPH::BroadPhaseLayer DYNAMIC {1};
    static constexpr JPH::uint            COUNT      = 2;
}

// Maps object layer → broad-phase layer.
class Broad_Phase_Layer_Interface final : public JPH::BroadPhaseLayerInterface
{
public:
    JPH::uint GetNumBroadPhaseLayers() const override
    {
        return Broad_Phase_Layers::COUNT;
    }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
    {
        return layer == Physics_Layers::STATIC ? Broad_Phase_Layers::STATIC : Broad_Phase_Layers::DYNAMIC;
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char *GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
    {
        return layer == Broad_Phase_Layers::STATIC ? "STATIC" : "DYNAMIC";
    }
#endif
};

// Should a given object layer collide with a given broad-phase layer?
class Object_Vs_Broad_Phase_Layer_Filter final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bp_layer) const override
    {
        // Static bodies never move; no need to test them against anything.
        if (layer == Physics_Layers::STATIC)
            return false;
        // Dynamic bodies collide with both layers.
        return true;
    }
};

// Should two object layers collide with each other?
class Object_Layer_Pair_Filter final : public JPH::ObjectLayerPairFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
    {
        // Static vs static: never.
        return !(a == Physics_Layers::STATIC && b == Physics_Layers::STATIC);
    }
};

// All physics state for one simulation context (server, or client prediction).
// Members are ordered so that bp_layer_interface / filters outlive physics_system
// (Jolt stores raw pointers to them internally).
struct physics_state_t
{
    Broad_Phase_Layer_Interface              broad_phase_layer_interface;
    Object_Vs_Broad_Phase_Layer_Filter       object_vs_broad_phase_layer_filter;
    Object_Layer_Pair_Filter                 object_layer_pair_filter;

    JPH::TempAllocatorImpl      scratch_allocator{10 * 1024 * 1024};
    JPH::JobSystemSingleThreaded job_system; // single-threaded → deterministic

    JPH::PhysicsSystem          physics_system;

    std::map<shared::entity_uid_t, JPH::BodyID> entity_body_map;
    std::map<JPH::BodyID, shared::entity_uid_t> body_entity_map; // reverse lookup for contact callbacks
};

// Call once at program startup (before creating any physics_state_t).
// Registers Jolt's allocator, factory, and all built-in types.
void jolt_init();

// Initialize a physics_state_t. jolt_init() must have been called first.
// Sets mDeterministicSimulation = true so client and server produce
// identical results from identical inputs.
void init_physics(physics_state_t &state);

// Advance the simulation by dt seconds. Use a fixed timestep (e.g. 1/60 s).
void step_physics(physics_state_t &state, float dt);

// Register a static axis-aligned box body for a map entity (AABB_Entity, Wedge_Entity).
// Call this on both server and client after init_session_from_map.
void register_static_box(physics_state_t &state, shared::entity_uid_t uid,
                          linalg::vec3f position, linalg::vec3f half_extents);

// Register a dynamic sphere body for a spawned game entity (e.g. Rocket_Entity).
// Only call this on the server — dynamic simulation is authoritative.
void register_dynamic_sphere(physics_state_t &state, shared::entity_uid_t uid,
                              linalg::vec3f position, float radius,
                              linalg::vec3f initial_velocity = {});

void register_dynamic_box(physics_state_t &state, shared::entity_uid_t uid,
                             linalg::vec3f position, linalg::vec3f half_extents,
                             linalg::vec3f initial_velocity = {});

// Register a kinematic capsule body. Use for players: position/velocity will be
// driven externally each tick via set_kinematic_pose() — Jolt does not integrate
// kinematic bodies from forces, but queries and contacts see the body normally.
void register_kinematic_capsule(physics_state_t &state, shared::entity_uid_t uid,
                                linalg::vec3f position, float radius, float half_height);

// Drive a kinematic body's pose. Call after running your own movement code.
void set_kinematic_pose(physics_state_t &state, shared::entity_uid_t uid,
                        linalg::vec3f position, linalg::vec3f velocity);

// Apply a one-shot impulse (mass*velocity units). No-op for static/kinematic bodies.
void apply_impulse(physics_state_t &state, shared::entity_uid_t uid, linalg::vec3f impulse);

// Add a velocity delta directly (no mass scaling). Use this when you want a
// fixed-magnitude knockback regardless of body mass — semantically what most
// FPS games mean by "knockback force". Activates the body. Kinematic-body
// velocity is clobbered by the next set_kinematic_pose() call, so for
// kinematic-controlled entities (players) update the game-state velocity.
void add_linear_velocity(physics_state_t &state, shared::entity_uid_t uid, linalg::vec3f delta);

// Remove a body. No-op if uid is not registered.
void unregister_physics_body(physics_state_t &state, shared::entity_uid_t uid);

// ----- spatial queries (engine-agnostic types, no Jolt leakage) -----

struct hit_result_t
{
    shared::entity_uid_t entity_id; // 0 if hit body has no entity mapping (shouldn't happen)
    linalg::vec3f position;
    linalg::vec3f normal;
    float fraction;                 // 0..1 along swept path (for casts)
};

// Which CATEGORY of body a query may touch. Filtered in the broad phase, so
// Static_Only is strictly cheaper than All rather than a post-filter.
enum class query_layers_t
{
  All,         // world geometry + dynamic/kinematic bodies (players, props, projectiles)
  Static_Only, // Physics_Layers::STATIC only
};

// Whether a query reports a surface it is LEAVING as well as one it is
// entering. Collide is what a projectile wants (a cast that starts barely
// inside a body still stops); Ignore is what decal placement wants (the front
// face the surface is showing, never a fraction-0 hit with a flipped normal).
enum class back_face_mode_t
{
  Collide,
  Ignore,
};

// The two filtering axes are deliberately SEPARATE parameters rather than
// separate functions. `layers` is a category ("what kind of thing"), `ignore_uid`
// is an identity ("which specific body") -- the shooter is the same category as
// everyone else, so no layer can express it. Encoding either in the function
// name gives a cross-product of near-identical overloads, which is what the old
// cast_sphere / cast_sphere_static pair was; note that pair also silently tied
// back-face mode to the layer choice, which is why it is a field here.
struct query_filter_t
{
  query_layers_t       layers     = query_layers_t::All;
  shared::entity_uid_t ignore_uid = shared::null_entity_uid; // null = ignore nothing
  back_face_mode_t     back_faces = back_face_mode_t::Collide;
};

// Ray from `from` to `to`. Returns true on first hit; `out.fraction` is along
// that segment, so the caller multiplies by the segment length to get a
// distance. Hitscan uses this to find the surface a bullet stops at, then
// clamps resolve_hitscan's max_range to it.
bool cast_ray(physics_state_t &state,
              linalg::vec3f from, linalg::vec3f to,
              const query_filter_t &filter,
              hit_result_t &out);

// Swept sphere from `from` to `to`. Returns true on first hit.
bool cast_sphere(physics_state_t &state,
                 linalg::vec3f from, linalg::vec3f to, float radius,
                 const query_filter_t &filter,
                 hit_result_t &out);

// All bodies overlapping a sphere. Used for explosion splash queries.
std::vector<hit_result_t> find_all_bodies_overlapping_sphere(physics_state_t &state,
                    linalg::vec3f center, float radius);
