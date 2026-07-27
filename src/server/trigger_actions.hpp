#pragma once

// =============================================================================
// Built-in Trigger Actions (server-side)
// =============================================================================
// One function, one exhaustive switch over entities::Trigger_Action.
//
// WHAT THIS REPLACED, and why the trade is worth taking: a string-keyed
// registry (Trigger_Action_Registry) whose entries self-registered at static
// init from an X-macro list of names (trigger_action_list.hpp), looked up by
// strcmp at fire time. Three pieces of machinery -- an X-macro, a singleton, a
// per-fire string hash -- to answer a question the .def now answers with a
// closed enum.
//
// The registry's stated advantage was that the NAME was the identity, so
// reordering could not silently rebind a saved trigger. That property survives:
// map files store the enum by NAME too (field_to_text writes "Warp_To_Spawn"),
// so reordering Trigger_Action in entities.def still rebinds nothing. What is
// gone is the failure mode where a map named an action nothing had registered
// and the server found out at fire time -- the value now either parses at LOAD
// time or is reported there.
//
// To add an action: add a value to Trigger_Action in entities.def and handle it
// in the switch. Forgetting the second half is a -Wswitch warning, which is
// strictly better than the missing registration it replaces.
//
// Lives in src/server/ because actions legitimately touch server-only state --
// inflict_damage, physics, the gameplay event queue. The editor inspector needs
// no counterpart header any more: it reads the value names straight out of the
// generated enum table.
// =============================================================================

#include "../shared/entities/entity_reflection.hpp"

namespace server
{

struct server_context_t;

// Runs `trigger`'s configured action against `player`. Reports and does nothing
// if the trigger carries a Trigger_Action value outside the enum, which can only
// come from memory corruption -- a bad value in a map file is caught at load.
void fire_trigger_action(server_context_t &context,
                         entities::Trigger_Volume_Entity &trigger,
                         entities::Player_Entity &player);

} // namespace server
