#include "bubble_system.hpp"
#include "entities/generated/entities/bubble_entity_generated.hpp"
#include "entities/generated/entities/player_entity_generated.hpp"
#include "entities/generated/entities_tables_generated.hpp"

#include "../../shared/collision/fixed_arc_flight.hpp"
#include "../../shared/movement/bubble.hpp"
#include "../entity_lifecycle.hpp"
#include "../server_api.hpp"
#include "fixed_arc_flight_launch.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace server
{

namespace
{

linalg::vec3f hashed_unit_vector(uint32_t seed)
{
  const float u        = std::fmod(static_cast<float>(seed) * 0.618034f, 1.0f);
  const float v        = std::fmod(static_cast<float>(seed) * 0.324718f, 1.0f);
  const float z        = 2.0f * u - 1.0f;
  const float azimuth  = 2.0f * 3.14159265f * v;
  const float ring     = std::sqrt(std::max(0.0f, 1.0f - z * z));
  return {ring * std::cos(azimuth), z, ring * std::sin(azimuth)};
}

linalg::vec3f compute_bubble_peel_direction(const server_context_t& context, const entities::Bubble_Entity& bubble,
                                shared::entity_uid_t popped_by)
{
  const entities::Player_Entity* popper =
      context.world.session.entity_system.get<entities::Player_Entity>(popped_by);
  if (popper == nullptr)
    return hashed_unit_vector(bubble.entity_id);
  const shared::fixed_arc_flight_settings_t flight{.tickrate_hz = context.cvars->sv_tickrate,
                                                   .gravity     = context.cvars->g_gravity};
  const linalg::vec3f offset = popper->position - shared::bubble_position_at_tick(bubble, context.tick_number, flight);
  if (linalg::length(offset) < 1e-3f)
    return {0.0f, 1.0f, 0.0f};
  return linalg::normalize(offset);
}

} // namespace

void update_bubbles(server_context_t& context, const shared::predicted_world_storage_t& world)
{
  const uint32_t tickrate_hz = context.cvars->sv_tickrate;
  const shared::fixed_arc_flight_settings_t flight{.tickrate_hz = tickrate_hz,
                                                   .gravity = context.cvars->g_gravity};

  std::vector<shared::entity_uid_t> expired;

  for (entities::Bubble_Entity& bubble :
       context.world.session.entity_system.entities_of_type<entities::Bubble_Entity>())
  {
    if (bubble.flight.launch_tick == 0)
      launch_fixed_arc_flight(context, bubble.projectile, bubble.position,
                              {.flight_seconds = bubble.flight_seconds, .clearance = bubble.radius},
                              flight, world, bubble.flight);

    if (bubble.popped_tick != 0)
    {
      if (shared::bubble_has_expired_at_tick(bubble, context.tick_number, tickrate_hz))
        expired.push_back(bubble.entity_id);
      continue;
    }

    if (context.tick_number >= shared::bubble_timed_pop_tick(bubble, tickrate_hz))
      pop_bubble(context, bubble.entity_id, shared::null_entity_uid);
  }

  for (const shared::entity_uid_t uid : expired)
    destroy_entity(context, uid);
}

void pop_bubble(server_context_t& context, shared::entity_uid_t bubble_uid,
                shared::entity_uid_t popped_by)
{
  auto* bubble = context.world.session.entity_system.get<entities::Bubble_Entity>(bubble_uid);
  if (bubble == nullptr || bubble->popped_tick != 0)
    return;

  bubble->popped_tick      = context.tick_number;
  bubble->popped_by        = popped_by;
  bubble->popped_direction = compute_bubble_peel_direction(context, *bubble, popped_by);
}

} // namespace server
