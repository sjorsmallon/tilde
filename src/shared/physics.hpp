#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include "map.hpp" // shared::entity_uid_t

#include <map>

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
                          vec3f position, vec3f half_extents);

// Register a dynamic sphere body for a spawned game entity (e.g. Rocket_Entity).
// Only call this on the server — dynamic simulation is authoritative.
void register_dynamic_sphere(physics_state_t &state, shared::entity_uid_t uid,
                              vec3f position, float radius,
                              vec3f initial_velocity = {});

void register_dynamic_box(physics_state_t &state, shared::entity_uid_t uid,
                             vec3f position, vec3f half_extents,
                             vec3f initial_velocity = {});

// Remove a body. No-op if uid is not registered.
void unregister_physics_body(physics_state_t &state, shared::entity_uid_t uid);
