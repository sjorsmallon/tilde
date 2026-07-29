// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/cvars/cvars.def by def_gen. Do not edit.
//
// Binds every @Client command. Each slot gets the command's generated ARGUMENT
// BINDER, which parses the console tokens against the declared signature
// and calls the typed handler `commands::<name>` -- so this TU references
// each handler symbol directly, and a missing, misspelled or wrongly typed
// handler fails at LINK time, naming the symbol. That link step is the
// assert -- there is no runtime "did everyone register?" check because
// there is nothing to register.
#include "cvars_generated.hpp"

#include <format>
#include <string>

namespace cvars
{

namespace
{

// Both error replies quote command_info().usage, which is derived from
// the same signature this binder was generated from -- the message can
// never drift from what the binder actually accepts.
void usage_error(std::string* out_reply, command_id id, uint32_t got)
{
  if (out_reply)
    *out_reply = std::format("[error] {}: wrong number of arguments (got {})\nusage: {}",
                             command_info(id).name, got, command_info(id).usage);
}

// bind <key> <command...>
bool invoke_bind(Span<std::string_view> args, const command_context_t& context,
     std::string* out_reply)
{
  if (args.size() < 2u)
  {
    usage_error(out_reply, command_id::bind, args.size());
    return false;
  }

  std::string_view key = args[0];

  // 'command' is 'string...': the untokenized rest of the line. Every view
  // in args points into ONE contiguous line buffer (see command_binder_t),
  // so the span from this parameter's first token to the end of the last
  // token is the original text, interior whitespace intact.
  std::string_view command(args[1].data(),
      (size_t)(args[args.size() - 1].data() + args[args.size() - 1].size() -
               args[1].data()));

  commands::bind(key, command, context);
  return true;
}

} // namespace

void bind_client_commands(command_table_t& table)
{
  table.binders[(uint32_t)command_id::bind] = &invoke_bind;
}

} // namespace cvars
