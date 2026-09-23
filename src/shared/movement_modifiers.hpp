#pragma once

// The third thing the movement volumes cut produces: boxes that SCALE the
// settings a step runs under. Read when the step OPENS, where a pad is tested
// after it, because friction, gravity and the accelerate all need their number
// before they run.

#include "aabb.hpp"
#include "entities/generated/entities_generated.hpp"
#include "entity_uid.hpp"
#include "movement_settings.hpp"
#include "span.hpp"

#include <vector>

namespace shared
{

struct Entity_System;
// Forward-declared rather than included: fixed_arc_flight.hpp reaches weapons.hpp, which reaches back here.
struct fixed_arc_flight_settings_t;

struct movement_modifier_t
{
  entity_uid_t  uid     = null_entity_uid;
  aabb_bounds_t bounds  = {};
  bool          enabled = true;

  float gravity_scale    = 1.f;
  float run_speed_scale  = 1.f;
  float jump_speed_scale = 1.f;
  float friction_scale   = 1.f;
  float control_scale    = 1.f;
};

// Active from the tick after it was spawned until lifetime_seconds later; a spawned_tick of 0 is one the server
// has not stamped yet. Both sides ask this, so the client predicts the expiry as it predicts a platform's.
[[nodiscard]] bool timed_movement_modifier_is_active_at(
    const entities::Timed_Movement_Modifier_Entity& modifier, uint32_t tick,
    float tick_interval_seconds);

// Every map-placed Movement_Modifier_Entity, then every Timed_Movement_Modifier_Entity, the latter enabled
// only while active at `tick` and centred where its flight puts it at `tick`.
void collect_movement_modifiers(Entity_System& system, uint32_t tick,
                                const fixed_arc_flight_settings_t& flight,
                                std::vector<movement_modifier_t>& out);

// Overlapping modifiers multiply.
[[nodiscard]] movement_settings_t modified_movement_settings(
    const movement_settings_t& settings, Span<const movement_modifier_t> modifiers,
    const aabb_bounds_t& hull);

} // namespace shared
