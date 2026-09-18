#pragma once

namespace server
{

struct server_context_t;

// Every mover's path starts at the tick the map loads, frozen when authored switched off.
void install_movers(server_context_t& context);

// Advances every running follow through this tick and emits Node_Reached. After the cut, before the drain.
void advance_movers(server_context_t& context);

// Turns a switch into a freeze or a resume. After the drain, so the snapshot carries it.
void latch_mover_switches(server_context_t& context);

} // namespace server
