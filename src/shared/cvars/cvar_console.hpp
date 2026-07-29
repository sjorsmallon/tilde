#pragma once

// cvar_console -- the one place a line of console text turns into a cvar write
// or a command invocation. Replaces CVarSystem::Execute.
//
// It is a free function over (state, table, line) rather than a method on a
// registry, because after the .def cutover there IS no registry: the names live
// in the generated constexpr tables, the values live in the launcher's one
// cvar_state_t, and the handlers live in the launcher's one command_table_t.
// Both the client console and the server's remote-command inbox call this, so
// `spawn_bot` typed locally and `spawn_bot` forwarded over the wire take
// exactly the same path.
//
// GRAMMAR of a console line:
//
//   line       := ws* [ name { ws+ argument } ] ws*
//   name       := token                  // resolved against CVAR_INFOS, then
//                                        // COMMAND_INFOS -- one flat namespace
//   argument   := token
//   token      := (any run of non-whitespace)
//   ws         := " " | "\t" | "\r" | "\n"
//
// This layer only splits; MEANING comes from the resolved name. A cvar takes
// one value token (a string<N> cvar takes the whole trimmed remainder). A
// command's tokens are handed to its generated argument binder, which parses
// them against the signature declared in cvars.def -- types, defaults, and a
// trailing `string...` parameter that consumes the line's untokenized tail
// (how `bind w spawn_bot chase` keeps its inner spaces).
//
// There are no quotes and no escapes. Quoting earns its place the day a
// command needs an argument with a space in it that is NOT its last -- the
// rest parameter already covers the last one.
//
// See cvar_def.md at the repo root for the design.

#include "cvars/generated/cvars_generated.hpp"

#include <string>
#include <string_view>

namespace cvars
{

// What happened to the line. The caller decides how to REPORT it -- the console
// prints, the server replies over the wire -- so nothing here writes to a
// console. `out_reply` carries the human-readable half.
enum class console_result_t
{
  ok,            // a cvar was printed or set, or a command ran
  empty,         // blank line; nothing to do and nothing to report
  unknown_name,  // the first token is neither a cvar nor a command
  forwarded,     // @Server name on a networked client: the line went upstream
  not_connected, // @Server name, but no forwarder is installed
  bad_arguments, // wrong argument count, or text that does not parse
  no_handler,    // a @Client command reaching a process with no client module
};

// Executes one line. `context.caller_slot` identifies who asked (a network slot,
// or -1 for local/server-initiated).
//
// `out_reply` may be null. When non-null it receives the text the caller should
// show: the value echo for a bare cvar read, or the reason for every non-ok
// result. It is left untouched for `empty` and for a successful command (a
// command that wants to say something says it itself, through log_terminal or
// its own reply path).
//
// Forwarding: a @Server or @Mirrored cvar being SET, and a @Server command,
// execute locally unless `table.forward_to_server` is installed -- which only a
// NETWORKED client does. That single pointer is the whole "am I allowed to run
// this myself" test: the dedicated server and the integrated build leave it
// null and run everything in-process, which is correct because in the
// integrated build the client and the server share this very cvar_state_t.
console_result_t execute_console_line(cvar_state_t&           state,
                                      const command_table_t&  table,
                                      std::string_view        line,
                                      const command_context_t& context,
                                      std::string*             out_reply);

// "[SERVER] ", "[CLIENT] ", "[MIRRORED] " -- the flag decoration the console
// prints next to a value. Empty for an unflagged (shared-local) cvar.
std::string describe_cvar_flags(uint32_t flags);

} // namespace cvars
