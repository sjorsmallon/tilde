#include "physics.hpp"

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Body/BodyLock.h>

#include "log.hpp"

using namespace linalg;

static JPH::Vec3  to_jolt(vec3f v)  { return {v.x, v.y, v.z}; }
static JPH::RVec3 to_jolt_r(vec3f v){ return {v.x, v.y, v.z}; }
static vec3f from_jolt(JPH::Vec3 v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
static vec3f from_jolt_r(JPH::RVec3 v) { return {(float)v.GetX(), (float)v.GetY(), (float)v.GetZ()}; }

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
    state.physics_system.SetGravity({0, -800.f, 0});
}

std::unique_ptr<physics_state_t> make_physics_state()
{
    std::unique_ptr<physics_state_t> state = std::make_unique<physics_state_t>();
    init_physics(*state);
    return state;
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

void register_dynamic_box(physics_state_t &state, shared::entity_uid_t uid,
                             vec3f position, vec3f half_extents, vec3f initial_velocity)
{
    JPH::BodyCreationSettings settings(
        new JPH::BoxShape(to_jolt(half_extents)),
        to_jolt_r(position),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Dynamic,
        Physics_Layers::DYNAMIC);
    settings.mLinearVelocity = to_jolt(initial_velocity);

    JPH::BodyInterface &body_interface = state.physics_system.GetBodyInterface();
    JPH::Body *body = body_interface.CreateBody(settings);
    if (!body)
    {
        log_error("register_dynamic_box: body limit reached for uid {}", uid);
        return;
    }
    body_interface.AddBody(body->GetID(), JPH::EActivation::Activate);
    state.entity_body_map[uid]             = body->GetID();
    state.body_entity_map[body->GetID()]   = uid;
}

void register_kinematic_capsule(physics_state_t &state, shared::entity_uid_t uid,
                                vec3f position, float radius, float half_height)
{
    JPH::BodyCreationSettings settings(
        new JPH::CapsuleShape(half_height, radius),
        to_jolt_r(position),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Kinematic,
        Physics_Layers::DYNAMIC);

    JPH::BodyInterface &body_interface = state.physics_system.GetBodyInterface();
    JPH::Body *body = body_interface.CreateBody(settings);
    if (!body)
    {
        log_error("register_kinematic_capsule: body limit reached for uid {}", uid);
        return;
    }
    body_interface.AddBody(body->GetID(), JPH::EActivation::Activate);
    state.entity_body_map[uid]           = body->GetID();
    state.body_entity_map[body->GetID()] = uid;
}

void set_kinematic_pose(physics_state_t &state, shared::entity_uid_t uid,
                        vec3f position, vec3f velocity)
{
    auto it = state.entity_body_map.find(uid);
    if (it == state.entity_body_map.end())
    {
        log_error("set_kinematic_pose: no body for uid {}", uid);
        return;
    }
    JPH::BodyInterface &body_interface = state.physics_system.GetBodyInterface();
    body_interface.SetPositionAndRotation(it->second, to_jolt_r(position),
                                          JPH::Quat::sIdentity(),
                                          JPH::EActivation::Activate);
    body_interface.SetLinearVelocity(it->second, to_jolt(velocity));
}

void apply_impulse(physics_state_t &state, shared::entity_uid_t uid, vec3f impulse)
{
    auto it = state.entity_body_map.find(uid);
    if (it == state.entity_body_map.end())
    {
        log_error("apply_impulse: no body for uid {}", uid);
        return;
    }
    JPH::BodyInterface &body_interface = state.physics_system.GetBodyInterface();
    body_interface.AddImpulse(it->second, to_jolt(impulse));
}

void add_linear_velocity(physics_state_t &state, shared::entity_uid_t uid, vec3f delta)
{
    auto it = state.entity_body_map.find(uid);
    if (it == state.entity_body_map.end())
    {
        log_error("add_linear_velocity: no body for uid {}", uid);
        return;
    }
    JPH::BodyInterface &body_interface = state.physics_system.GetBodyInterface();
    body_interface.ActivateBody(it->second);
    body_interface.AddLinearVelocity(it->second, to_jolt(delta));
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

// --- query_filter_t -> Jolt's three filter objects ---
//
// Jolt takes the two axes separately too (an ObjectLayerFilter for category, a
// BodyFilter for identity), so these are thin adapters rather than a
// translation. Each is a no-op in its permissive setting, which is what lets
// one pair of cast functions serve every combination.

// Identity axis. A default-constructed BodyID is invalid and equals no real
// body, so "ignore nothing" needs no branch in the hot ShouldCollide -- this is
// called once per candidate body in the narrow phase.
class Ignore_Single_Body_Filter final : public JPH::BodyFilter
{
public:
    JPH::BodyID ignore_id;
    explicit Ignore_Single_Body_Filter(JPH::BodyID id) : ignore_id(id) {}
    bool ShouldCollide(const JPH::BodyID &id) const override { return id != ignore_id; }
};

// Category axis, broad phase. `static_only == false` admits everything.
class Layer_Broad_Phase_Filter final : public JPH::BroadPhaseLayerFilter
{
public:
    bool static_only;
    explicit Layer_Broad_Phase_Filter(bool static_only) : static_only(static_only) {}
    bool ShouldCollide(JPH::BroadPhaseLayer layer) const override
    {
        return !static_only || layer == Broad_Phase_Layers::STATIC;
    }
};

// Category axis, narrow phase. Must agree with Layer_Broad_Phase_Filter: the
// broad phase only prunes, so a body in a STATIC broad-phase bucket is still
// re-checked here.
class Layer_Object_Filter final : public JPH::ObjectLayerFilter
{
public:
    bool static_only;
    explicit Layer_Object_Filter(bool static_only) : static_only(static_only) {}
    bool ShouldCollide(JPH::ObjectLayer layer) const override
    {
        return !static_only || layer == Physics_Layers::STATIC;
    }
};

static JPH::BodyID resolve_ignored_body(physics_state_t &state,
                                        shared::entity_uid_t ignore_uid)
{
    if (ignore_uid == shared::null_entity_uid) return {};
    auto it = state.entity_body_map.find(ignore_uid);
    return (it != state.entity_body_map.end()) ? it->second : JPH::BodyID{};
}

static JPH::EBackFaceMode to_jolt_back_face_mode(back_face_mode_t mode)
{
    return mode == back_face_mode_t::Collide ? JPH::EBackFaceMode::CollideWithBackFaces
                                             : JPH::EBackFaceMode::IgnoreBackFaces;
}

// entity_id is 0 when the hit body has no entity mapping -- static world
// geometry legitimately has none, so this is not an error path.
static shared::entity_uid_t entity_for_body(physics_state_t &state, JPH::BodyID body_id)
{
    auto it = state.body_entity_map.find(body_id);
    return (it != state.body_entity_map.end()) ? it->second : shared::null_entity_uid;
}

bool cast_ray(physics_state_t &state,
              vec3f from, vec3f to,
              const query_filter_t &filter,
              hit_result_t &out)
{
    const vec3f    segment = to - from;
    JPH::RRayCast  ray{to_jolt_r(from), to_jolt(segment)};

    // Direction carries the full segment, so mFraction is already 0..1 across
    // it and needs no division by a length.
    JPH::RayCastSettings settings;
    settings.SetBackFaceMode(to_jolt_back_face_mode(filter.back_faces));
    // A ray starting inside a convex body reports fraction 0 rather than
    // sailing through it -- a muzzle already overlapping a crate must not shoot
    // out the far side.
    settings.mTreatConvexAsSolid = true;

    JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> collector;

    const bool static_only = filter.layers == query_layers_t::Static_Only;
    Layer_Broad_Phase_Filter   broad_phase_filter(static_only);
    Layer_Object_Filter        object_layer_filter(static_only);
    Ignore_Single_Body_Filter  body_filter(resolve_ignored_body(state, filter.ignore_uid));

    state.physics_system.GetNarrowPhaseQuery().CastRay(
        ray, settings, collector,
        broad_phase_filter, object_layer_filter, body_filter);

    if (!collector.HadHit()) return false;

    const JPH::BodyID hit_id = collector.mHit.mBodyID;
    out.entity_id = entity_for_body(state, hit_id);
    out.fraction  = collector.mHit.mFraction;
    out.position  = from + segment * collector.mHit.mFraction;

    // Unlike a shape cast there is no penetration axis to fall back on: the
    // surface normal has to come off the body, which means locking it.
    out.normal = {0.f, 0.f, 0.f};
    JPH::BodyLockRead lock(state.physics_system.GetBodyLockInterface(), hit_id);
    if (lock.Succeeded())
    {
        out.normal = from_jolt(lock.GetBody().GetWorldSpaceSurfaceNormal(
            collector.mHit.mSubShapeID2, to_jolt_r(out.position)));
    }
    else
    {
        log_error("cast_ray hit body {} but could not lock it for a surface "
                  "normal; reporting a zero normal",
                  hit_id.GetIndex());
    }
    return true;
}

bool cast_sphere(physics_state_t &state,
                 vec3f from, vec3f to, float radius,
                 const query_filter_t &filter,
                 hit_result_t &out)
{
    JPH::SphereShape sphere(radius);
    sphere.SetEmbedded(); // stack-allocated, must not be reference-counted

    JPH::Vec3 motion = to_jolt(to - from);
    JPH::RShapeCast cast(&sphere, JPH::Vec3::sReplicate(1.0f),
                         JPH::RMat44::sTranslation(to_jolt_r(from)),
                         motion);

    JPH::ShapeCastSettings settings;
    const JPH::EBackFaceMode back_face_mode = to_jolt_back_face_mode(filter.back_faces);
    settings.mBackFaceModeTriangles = back_face_mode;
    settings.mBackFaceModeConvex    = back_face_mode;

    JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;

    const bool static_only = filter.layers == query_layers_t::Static_Only;
    Layer_Broad_Phase_Filter   broad_phase_filter(static_only);
    Layer_Object_Filter        object_layer_filter(static_only);
    Ignore_Single_Body_Filter  body_filter(resolve_ignored_body(state, filter.ignore_uid));

    state.physics_system.GetNarrowPhaseQuery().CastShape(
        cast, settings, JPH::RVec3::sZero(), collector,
        broad_phase_filter, object_layer_filter, body_filter);

    if (!collector.HadHit()) return false;

    JPH::BodyID hit_id = collector.mHit.mBodyID2;
    out.entity_id = entity_for_body(state, hit_id);
    out.fraction  = collector.mHit.mFraction;
    out.position  = from + (to - from) * collector.mHit.mFraction;
    // ContactPointOn2 + surface normal need the body to be locked; the closest-hit
    // collector stores mPenetrationAxis which points from body2 into body1 (the cast).
    JPH::Vec3 n = collector.mHit.mPenetrationAxis.Normalized();
    out.normal = from_jolt(n);
    return true;
}

std::vector<hit_result_t> find_all_bodies_overlapping_sphere(physics_state_t &state,
                    vec3f center, float radius)
{
    JPH::SphereShape sphere(radius);
    sphere.SetEmbedded();

    JPH::CollideShapeSettings settings;
    settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;

    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;

    state.physics_system.GetNarrowPhaseQuery().CollideShape(
        &sphere, JPH::Vec3::sReplicate(1.0f),
        JPH::RMat44::sTranslation(to_jolt_r(center)),
        settings, JPH::RVec3::sZero(), collector);

    std::vector<hit_result_t> hits;
    hits.reserve(collector.mHits.size());
    for (const JPH::CollideShapeResult &h : collector.mHits)
    {
        hit_result_t r{};
        auto e_it = state.body_entity_map.find(h.mBodyID2);
        r.entity_id = (e_it != state.body_entity_map.end()) ? e_it->second : 0;
        r.position  = from_jolt_r(h.mContactPointOn2);
        JPH::Vec3 n = h.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY());
        r.normal    = from_jolt(n);
        r.fraction  = 0.f;
        hits.push_back(r);
    }
    return hits;
}
