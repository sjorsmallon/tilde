#pragma once
// Quake's MOVETYPE_BOUNCE: a sphere under gravity, swept through the one
// collision world, reflected off what it hits with a restitution, slowed along
// a floor by a friction, and at rest once it is slow on something floor-like.
// It is what a dropped weapon and a console crate are (collision_world_plan.md
// section 2 E), and it is deliberately not a rigid body: bodies pass through
// each other, nothing stacks, and the tumble is cosmetic.

#include "collision_detection.hpp"
#include "entities/generated/entities_generated.hpp"
#include "linalg.hpp"
#include "predicted_world.hpp"

namespace shared
{

// Below this speed on a floor-like contact a body stops for good; a write to
// its velocity through wake_bounce_body starts it again.
constexpr float BOUNCE_REST_SPEED    = 20.f;
constexpr float BOUNCE_REST_NORMAL_Y = 0.7f;
// Contacts resolved inside one step before the remainder of the step is dropped.
constexpr int BOUNCE_MAX_CONTACTS_PER_STEP = 4;
// How far off a face a contact leaves the body, in world units.
constexpr float BOUNCE_CONTACT_EPSILON = 0.01f;

struct bounce_body_t
{
  linalg::vec3f    position;
  linalg::quatf    orientation = linalg::quatf::identity();
  entities::Bounce bounce;
};

// One tick. A body at rest comes back unchanged; angular_velocity is in
// degrees per second about a world axis and stops with the body.
[[nodiscard]] bounce_body_t bounce_step(const Bounding_Volume_Hierarchy& bvh,
                                        const predicted_world_t& world, const bounce_body_t& body,
                                        float radius, float gravity, float dt);

// The ONE way anything outside the step writes a velocity: a throw, a blast,
// a hit. Adds the delta and takes the body out of rest.
inline void wake_bounce_body(entities::Bounce& bounce, const linalg::vec3f& velocity_delta)
{
  bounce.velocity = bounce.velocity + velocity_delta;
  bounce.at_rest  = false;
}

// A spin for a thrown body, from the tick and the uid so the server keeps no
// random state for it: a fixed rate about an axis those two hash to.
[[nodiscard]] linalg::vec3f throw_spin_for(uint32_t tick, uint32_t uid, float degrees_per_second);

} // namespace shared
