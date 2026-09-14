#pragma once

#include "../../shared/disabled_geometry.hpp"
#include "../../shared/linalg.hpp"
#include "../../shared/span.hpp"
#include "../server_context.hpp"

namespace server
{

// Cast the pinger's aim at the map and stand a marker where it landed. Returns
// false when the ray reached nothing, which is a legitimate outcome -- pinging
// the sky places nothing and makes no sound.
bool try_place_ping(server_context_t& context, entities::Player_Entity& player, vec3f eye,
                    vec3f aim_direction, Span<const uint8_t> disabled_geometry);

// Count every marker down and remove the ones that ran out.
void update_ping_markers(server_context_t& context, float dt);

} // namespace server
