#include "physics.hpp"

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>

#include "log.hpp"

static JPH::Vec3  to_jolt(vec3f v)  { return {v.x, v.y, v.z}; }
static JPH::RVec3 to_jolt_r(vec3f v){ return {v.x, v.y, v.z}; }

void jolt_init()
{
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();
}

void init_physics(physics_state_t &state)
{
    state.job_system.Init(JPH::cMaxPhysicsJobs);

    //@FIXME: promote these limits to config parameters.
    // these limits are not obvious, actually?
    constexpr JPH::uint max_bodies             = 1024;
    constexpr JPH::uint num_body_mutexes       = 0;    // 0 = auto
    constexpr JPH::uint max_body_pairs         = 1024;
    constexpr JPH::uint max_contact_constraints = 1024;

    state.physics_system.Init(
        max_bodies,
        num_body_mutexes,
        max_body_pairs,
        max_contact_constraints,
        state.broad_phase_layer_interface,
        state.object_vs_broad_phase_layer_filter,
        state.object_layer_pair_filter);

    JPH::PhysicsSettings settings;
    settings.mDeterministicSimulation = true;
    state.physics_system.SetPhysicsSettings(settings);
}

void step_physics(physics_state_t &state, float dt)
{
    // collision_steps = 1 is correct for a fixed 60 Hz tick.
    // Increase to 2 if you run at variable dt or need sub-step accuracy.
    constexpr int collision_steps = 1;
    state.physics_system.Update(dt, collision_steps, &state.scratch_allocator, &state.job_system);
}

void register_static_box(physics_state_t &state, shared::entity_uid_t uid,
                          vec3f position, vec3f half_extents)
{
    JPH::BodyCreationSettings settings(
        new JPH::BoxShape(to_jolt(half_extents)),
        to_jolt_r(position),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Static,
        Physics_Layers::STATIC);

    JPH::BodyInterface &body_interface = state.physics_system.GetBodyInterface();
    JPH::Body *body = body_interface.CreateBody(settings);
    if (!body)
    {
        log_error("register_static_box: body limit reached for uid {}", uid);
        return;
    }
    body_interface.AddBody(body->GetID(), JPH::EActivation::DontActivate);
    state.entity_body_map[uid]             = body->GetID();
    state.body_entity_map[body->GetID()]   = uid;
}

void register_dynamic_sphere(physics_state_t &state, shared::entity_uid_t uid,
                               vec3f position, float radius, vec3f initial_velocity)
{
    JPH::BodyCreationSettings settings(
        new JPH::SphereShape(radius),
        to_jolt_r(position),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Dynamic,
        Physics_Layers::DYNAMIC);
    settings.mLinearVelocity = to_jolt(initial_velocity);

    JPH::BodyInterface &body_interface = state.physics_system.GetBodyInterface();
    JPH::Body *body = body_interface.CreateBody(settings);
    if (!body)
    {
        log_error("register_dynamic_sphere: body limit reached for uid {}", uid);
        return;
    }
    body_interface.AddBody(body->GetID(), JPH::EActivation::Activate);
    state.entity_body_map[uid]             = body->GetID();
    state.body_entity_map[body->GetID()]   = uid;
}

void unregister_physics_body(physics_state_t &state, shared::entity_uid_t uid)
{
    auto it = state.entity_body_map.find(uid);
    if (it == state.entity_body_map.end())
        return;

    JPH::BodyID body_id = it->second;
    JPH::BodyInterface &body_interface = state.physics_system.GetBodyInterface();
    body_interface.RemoveBody(body_id);
    body_interface.DestroyBody(body_id);
    state.body_entity_map.erase(body_id);
    state.entity_body_map.erase(it);
}
