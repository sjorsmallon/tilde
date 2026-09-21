#pragma once

// The third thing the movement volumes cut produces: boxes that SCALE the
// settings a step runs under. Read when the step OPENS, where a pad is tested
// after it, because friction, gravity and the accelerate all need their number
// before they run.

#include "aabb.hpp"
#include "entity_uid.hpp"
#include "movement_settings.hpp"
#include "span.hpp"

#include <vector>

namespace shared
{

struct Entity_System;

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

void collect_movement_modifiers(Entity_System& system, std::vector<movement_modifier_t>& out);

// Overlapping modifiers multiply.
[[nodiscard]] movement_settings_t modified_movement_settings(
    const movement_settings_t& settings, Span<const movement_modifier_t> modifiers,
    const aabb_bounds_t& hull);

} // namespace shared
