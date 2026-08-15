#pragma once

#include "../shared/effects/generated/effects_generated.hpp"
#include "../shared/events/generated/events_generated.hpp"
#include "client_context.hpp"

namespace client
{

// The receiving side's seam, and the whole of it.
//
// Both functions are DEFINED by a generated bindings TU (client_effects_bindings.cpp,
// client_events_bindings.cpp), which switches over its channel's closed enum and
// calls one hand-written function per member -- `client::effects::on_<name>` and
// `client::game_events::on_<name>`, one file each under src/client/effects/ and
// src/client/game_events/.
//
// There is no registry, no table and no bind step. Adding a member to a .def
// adds a case to a generated switch that references a symbol nobody has written
// yet, so the failure is a LINK error naming it. That link step is the assert:
// "forgot to register" is not representable, because there is nothing to
// register.
//
// To add a CONSUMER to an existing event, add the call to that event's
// hand-written file. The consumer list lives there rather than in a registry,
// so grep finds it.
//
// The two are now the same shape: both read straight off a bitstream into a
// typed stack local per record. They differ only in which packet the batch rode
// in on -- effects are spliced into the snapshot, gameplay events get their own
// message.
//
// `log_received` is cl_event_debug, passed rather than read so this half stays
// free of the cvar family.

void dispatch_received_effects(client_context_t& context, network::Bit_Reader& reader,
                               bool log_received);

void dispatch_received_game_events(client_context_t& context, network::Bit_Reader& reader,
                                   bool log_received);

} // namespace client
