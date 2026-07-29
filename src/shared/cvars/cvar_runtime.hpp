#pragma once

// cvar_runtime -- the three types the generated cvar tables need but cannot
// derive from cvars.def.
//
// This is the handwritten half, and it is deliberately tiny. Everything that
// CAN be derived from the .def is generated (generated/cvars_generated.hpp):
// the state struct, the ids, the info tables, the text conversion, the handler
// declarations and command_table_t itself. What is left here is the shape of a
// handler and the shape of a caller -- facts about how the console CALLS
// things, which no declaration in the .def implies.
//
// It cannot include the generated header: the generated header includes THIS
// one, because it declares handlers taking a command_context_t. That direction
// is the reason command_table_t lives on the generated side -- it needs
// COMMAND_COUNT.
//
// See cvar_def.md at the repo root for the design.

#include "span.hpp"

#include <string_view>

namespace cvars
{

// Per-invocation context handed to a console command. The cvar system treats
// caller_slot as an opaque integer -- interpretation is left to the caller. In
// this game it is a network player slot index (>= 0), or -1 when the command
// was invoked locally (client console) or by the server itself (no human
// caller).
struct command_context_t
{
  int caller_slot = -1;
};

// Every command handler has exactly this signature. Declaring a command in
// cvars.def obligates the owning side to define `cvars::commands::<name>` with
// it; the generated per-side binder TU references that symbol directly, so a
// mismatch is a link error rather than a runtime surprise.
//
// A plain function pointer, not std::function: a handler is a free function
// with no captured state, and the table is a fixed array indexed by command_id.
using command_handler_t = void (*)(Span<std::string_view> args,
                                   const command_context_t& context);

// Set by a networked client. A @Server cvar or command typed into a client
// console is forwarded as a whole line rather than executed locally -- the
// server owns it, and the client's copy of the table exists only so the console
// can resolve the name and know to forward.
using forward_line_fn_t = void (*)(std::string_view line);

} // namespace cvars
