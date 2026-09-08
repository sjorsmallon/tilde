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

#include <charconv>
#include <format>
#include <string>
#include <optional>

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

void bad_argument(std::string* out_reply, command_id id, const char* parameter,
                  std::string_view text)
{
  if (out_reply)
    *out_reply = std::format("[error] {}: '{}' is not a valid {}\nusage: {}",
                             command_info(id).name, text, parameter,
                             command_info(id).usage);
}

// Requires the WHOLE token to parse, same rule as a cvar write: accepting
// 320 from '320abc' would pass a value the caller never typed.
template <typename T> std::optional<T> try_parse_whole(std::string_view text)
{
  T           value  = {};
  const char* begin  = text.data();
  const char* end    = text.data() + text.size();
  auto        result = std::from_chars(begin, end, value);
  if (result.ec != std::errc{} || result.ptr != end)
    return std::nullopt;
  return value;
}

// The same closed set as a bool cvar write: unrecognised text is a
// rejection, never false.
std::optional<bool> try_parse_bool_token(std::string_view text)
{
  if (text == "1" || text == "true" || text == "yes" || text == "on")
    return true;
  if (text == "0" || text == "false" || text == "no" || text == "off")
    return false;
  return std::nullopt;
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

// connect <address>
bool invoke_connect(Span<std::string_view> args, const command_context_t& context,
     std::string* out_reply)
{
  if (args.size() != 1u)
  {
    usage_error(out_reply, command_id::connect, args.size());
    return false;
  }

  std::string_view address = args[0];

  commands::connect(address, context);
  return true;
}

// announce <text...>
bool invoke_announce(Span<std::string_view> args, const command_context_t& context,
     std::string* out_reply)
{
  if (args.size() < 1u)
  {
    usage_error(out_reply, command_id::announce, args.size());
    return false;
  }

  // 'text' is 'string...': the untokenized rest of the line. Every view
  // in args points into ONE contiguous line buffer (see command_binder_t),
  // so the span from this parameter's first token to the end of the last
  // token is the original text, interior whitespace intact.
  std::string_view text(args[0].data(),
      (size_t)(args[args.size() - 1].data() + args[args.size() - 1].size() -
               args[0].data()));

  commands::announce(text, context);
  return true;
}

// mem_report [top]
bool invoke_mem_report(Span<std::string_view> args, const command_context_t& context,
     std::string* out_reply)
{
  if (args.size() > 1u)
  {
    usage_error(out_reply, command_id::mem_report, args.size());
    return false;
  }

  int32_t top = 20;
  if (args.size() > 0u)
  {
    const std::optional<int32_t> parsed_top = try_parse_whole<int32_t>(args[0]);
    if (!parsed_top)
    {
      bad_argument(out_reply, command_id::mem_report, "top", args[0]);
      return false;
    }
    top = *parsed_top;
  }

  commands::mem_report(top, context);
  return true;
}

// mem_frame
bool invoke_mem_frame(Span<std::string_view> args, const command_context_t& context,
     std::string* out_reply)
{
  if (args.size() != 0u)
  {
    usage_error(out_reply, command_id::mem_frame, args.size());
    return false;
  }

  commands::mem_frame(context);
  return true;
}

// mem_stacks [capture]
bool invoke_mem_stacks(Span<std::string_view> args, const command_context_t& context,
     std::string* out_reply)
{
  if (args.size() > 1u)
  {
    usage_error(out_reply, command_id::mem_stacks, args.size());
    return false;
  }

  bool capture = true;
  if (args.size() > 0u)
  {
    const std::optional<bool> parsed_capture = try_parse_bool_token(args[0]);
    if (!parsed_capture)
    {
      bad_argument(out_reply, command_id::mem_stacks, "capture", args[0]);
      return false;
    }
    capture = *parsed_capture;
  }

  commands::mem_stacks(capture, context);
  return true;
}

// frame_report
bool invoke_frame_report(Span<std::string_view> args, const command_context_t& context,
     std::string* out_reply)
{
  if (args.size() != 0u)
  {
    usage_error(out_reply, command_id::frame_report, args.size());
    return false;
  }

  commands::frame_report(context);
  return true;
}

// frame_reset
bool invoke_frame_reset(Span<std::string_view> args, const command_context_t& context,
     std::string* out_reply)
{
  if (args.size() != 0u)
  {
    usage_error(out_reply, command_id::frame_reset, args.size());
    return false;
  }

  commands::frame_reset(context);
  return true;
}

// hitch_report [top]
bool invoke_hitch_report(Span<std::string_view> args, const command_context_t& context,
     std::string* out_reply)
{
  if (args.size() > 1u)
  {
    usage_error(out_reply, command_id::hitch_report, args.size());
    return false;
  }

  int32_t top = 15;
  if (args.size() > 0u)
  {
    const std::optional<int32_t> parsed_top = try_parse_whole<int32_t>(args[0]);
    if (!parsed_top)
    {
      bad_argument(out_reply, command_id::hitch_report, "top", args[0]);
      return false;
    }
    top = *parsed_top;
  }

  commands::hitch_report(top, context);
  return true;
}

} // namespace

void bind_client_commands(command_table_t& table)
{
  table.binders[(uint32_t)command_id::bind] = &invoke_bind;
  table.binders[(uint32_t)command_id::connect] = &invoke_connect;
  table.binders[(uint32_t)command_id::announce] = &invoke_announce;
  table.binders[(uint32_t)command_id::mem_report] = &invoke_mem_report;
  table.binders[(uint32_t)command_id::mem_frame] = &invoke_mem_frame;
  table.binders[(uint32_t)command_id::mem_stacks] = &invoke_mem_stacks;
  table.binders[(uint32_t)command_id::frame_report] = &invoke_frame_report;
  table.binders[(uint32_t)command_id::frame_reset] = &invoke_frame_reset;
  table.binders[(uint32_t)command_id::hitch_report] = &invoke_hitch_report;
}

} // namespace cvars
