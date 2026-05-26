#pragma once

// =============================================================================
// Trigger Action Name List
// =============================================================================
// Single source of truth for the set of valid trigger action names. Both the
// client (editor inspector dropdown) and the server (registration + dispatch)
// read from this list.
//
// Names only — no function pointers, no behaviors. The action *bodies* live
// in src/server/trigger_actions.cpp because they need server_context_t /
// inflict_damage / physics access, none of which are available to game_shared.
// Keeping just the names here means this header stays clean of any
// server-only types and can be included by the client editor.
//
// To add a new trigger action:
//   1. Add one line to TRIGGER_ACTION_LIST below.
//   2. Add a function definition in src/server/trigger_actions.cpp:
//          void action_<name>(server_context_t&,
//                             network::Trigger_Volume_Entity&,
//                             network::Player_Entity&);
//   3. Register it at the bottom of that file via Trigger_Action_Registration.
//
// The X-macro pattern matches entity_list.hpp's SHARED_ENTITIES_LIST — see
// that header for the same trick applied to entity types.
// =============================================================================

// X(symbol, "string_name")
//   symbol      — C++ identifier used only for unique-name purposes (e.g. when
//                 grepping). Not the wire/save name.
//   string_name — the stable string the trigger entity stores in its
//                 `action_name` field. Renaming this breaks every saved map
//                 that referenced the old name.
#define TRIGGER_ACTION_LIST            \
    X(kill,           "kill")          \
    X(set_health,     "set_health")    \
    X(print_message,  "print_message") \
    X(warp_to_spawn,  "warp_to_spawn")
