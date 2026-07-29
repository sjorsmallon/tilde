#pragma once

// cvar_runtime -- the three types the generated cvar tables need but cannot
// derive from cvars.def.
//
// This is the handwritten half, and it is deliberately tiny. Everything that
// CAN be derived from the .def is generated (generated/cvars_generated.hpp):
// the state struct, the ids, the info tables, the text conversion, the typed
// handler declarations, the argument binders and command_table_t itself. What
// is left here is the shape of a binder and the shape of a caller -- facts
// about how the console CALLS things, which no declaration in the .def
// implies.
//
// It cannot include the generated header: the generated header includes THIS
// one, because it declares handlers taking a command_context_t. That direction
// is the reason command_table_t lives on the generated side -- it needs
// COMMAND_COUNT.
//
// See cvar_def.md at the repo root for the design.

#include "span.hpp"

#include <string>
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

// What the dispatch table holds per command: not the handler, its generated
// ARGUMENT BINDER. The handler itself is typed from the command's declared
// signature in cvars.def (`spawn_bot(Bot_Mode, const command_context_t&)`),
// so no uniform pointer type can name it; the binder is the uniform face --
// it parses the token list against the signature, fills defaults, and either
// calls the typed handler or writes the usage string into out_reply and
// returns false without calling anything.
//
// Contract on `args`: every view points into ONE contiguous line buffer, in
// order. A `string...` rest parameter is recovered as the span from its first
// token to the end of the last, which is what preserves the line's interior
// whitespace -- a tokenizer that copied tokens out would silently break it.
//
// A plain function pointer, not std::function: a binder is a generated free
// function with no captured state, and the table is a fixed array indexed by
// command_id.
using command_binder_t = bool (*)(Span<std::string_view> args,
                                  const command_context_t& context,
                                  std::string* out_reply);

// Set by a networked client. A @Server cvar or command typed into a client
// console is forwarded as a whole line rather than executed locally -- the
// server owns it, and the client's copy of the table exists only so the console
// can resolve the name and know to forward.
using forward_line_fn_t = void (*)(std::string_view line);

} // namespace cvars
