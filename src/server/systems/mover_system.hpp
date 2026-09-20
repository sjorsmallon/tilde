#pragma once

#include "../../shared/predicted_world.hpp"

namespace server
{

struct server_context_t;

// Every mover's path starts at the tick the map loads, frozen when authored switched off.
void install_movers(server_context_t& context);

// Carries every player a mover is under, and crushes the ones it closes on.
// Once per tick, however many inputs each client sent: mover_def.md ss12.
void push_players_by_movers(server_context_t& context, const shared::predicted_world_t& world);

// Advances every running follow through this tick and emits Node_Reached. After the cut, before the delivery.
void update_movers(server_context_t& context);

// Turns a switch into a freeze or a resume. After the delivery, so the snapshot carries it.
void update_mover_switches(server_context_t& context);

} // namespace server
