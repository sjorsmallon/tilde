// Generated from C:/Users/sjors/Desktop/Projects/tilde/tilde/src/shared/cvars/cvars.def by def_gen. Do not edit.
//
// Binds every @Server command. Each slot gets the command's generated ARGUMENT
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

// spawn_bot [mode: idle|chase|regular]
bool invoke_spawn_bot(Span<std::string_view> args, const command_context_t& context,
     std::string* out_reply)
{
  if (args.size() > 1u)
  {
    usage_error(out_reply, command_id::spawn_bot, args.size());
    return false;
  }

  Bot_Mode mode = Bot_Mode::idle;
  if (args.size() > 0u)
  {
    const std::optional<Bot_Mode> parsed_mode = try_from_string<Bot_Mode>(args[0]);
    if (!parsed_mode)
    {
      bad_argument(out_reply, command_id::spawn_bot, "mode", args[0]);
      return false;
    }
    mode = *parsed_mode;
  }

  commands::spawn_bot(mode, context);
  return true;
}

// spawn_cube
bool invoke_spawn_cube(Span<std::string_view> args, const command_context_t& context,
     std::string* out_reply)
{
  if (args.size() != 0u)
  {
    usage_error(out_reply, command_id::spawn_cube, args.size());
    return false;
  }

  commands::spawn_cube(context);
  return true;
}

// spawn_sphere
bool invoke_spawn_sphere(Span<std::string_view> args, const command_context_t& context,
     std::string* out_reply)
{
  if (args.size() != 0u)
  {
    usage_error(out_reply, command_id::spawn_sphere, args.size());
    return false;
  }

  commands::spawn_sphere(context);
  return true;
}

// map <path>
bool invoke_map(Span<std::string_view> args, const command_context_t& context,
     std::string* out_reply)
{
  if (args.size() != 1u)
  {
    usage_error(out_reply, command_id::map, args.size());
    return false;
  }

  std::string_view path = args[0];

  commands::map(path, context);
  return true;
}

// noclip [enabled]
bool invoke_noclip(Span<std::string_view> args, const command_context_t& context,
     std::string* out_reply)
{
  if (args.size() > 1u)
  {
    usage_error(out_reply, command_id::noclip, args.size());
    return false;
  }

  bool enabled = false;
  if (args.size() > 0u)
  {
    const std::optional<bool> parsed_enabled = try_parse_bool_token(args[0]);
    if (!parsed_enabled)
    {
      bad_argument(out_reply, command_id::noclip, "enabled", args[0]);
      return false;
    }
    enabled = *parsed_enabled;
  }

  commands::noclip(enabled, context);
  return true;
}

// join_game
bool invoke_join_game(Span<std::string_view> args, const command_context_t& context,
     std::string* out_reply)
{
  if (args.size() != 0u)
  {
    usage_error(out_reply, command_id::join_game, args.size());
    return false;
  }

  commands::join_game(context);
  return true;
}

// spectate
bool invoke_spectate(Span<std::string_view> args, const command_context_t& context,
     std::string* out_reply)
{
  if (args.size() != 0u)
  {
    usage_error(out_reply, command_id::spectate, args.size());
    return false;
  }

  commands::spectate(context);
  return true;
}

// sv_mem_report [top]
bool invoke_sv_mem_report(Span<std::string_view> args, const command_context_t& context,
     std::string* out_reply)
{
  if (args.size() > 1u)
  {
    usage_error(out_reply, command_id::sv_mem_report, args.size());
    return false;
  }

  int32_t top = 20;
  if (args.size() > 0u)
  {
    const std::optional<int32_t> parsed_top = try_parse_whole<int32_t>(args[0]);
    if (!parsed_top)
    {
      bad_argument(out_reply, command_id::sv_mem_report, "top", args[0]);
      return false;
    }
    top = *parsed_top;
  }

  commands::sv_mem_report(top, context);
  return true;
}

// sv_frame_report
bool invoke_sv_frame_report(Span<std::string_view> args, const command_context_t& context,
     std::string* out_reply)
{
  if (args.size() != 0u)
  {
    usage_error(out_reply, command_id::sv_frame_report, args.size());
    return false;
  }

  commands::sv_frame_report(context);
  return true;
}

// sv_hitch_report [top]
bool invoke_sv_hitch_report(Span<std::string_view> args, const command_context_t& context,
     std::string* out_reply)
{
  if (args.size() > 1u)
  {
    usage_error(out_reply, command_id::sv_hitch_report, args.size());
    return false;
  }

  int32_t top = 15;
  if (args.size() > 0u)
  {
    const std::optional<int32_t> parsed_top = try_parse_whole<int32_t>(args[0]);
    if (!parsed_top)
    {
      bad_argument(out_reply, command_id::sv_hitch_report, "top", args[0]);
      return false;
    }
    top = *parsed_top;
  }

  commands::sv_hitch_report(top, context);
  return true;
}

} // namespace

void bind_server_commands(command_table_t& table)
{
  table.binders[(uint32_t)command_id::spawn_bot] = &invoke_spawn_bot;
  table.binders[(uint32_t)command_id::spawn_cube] = &invoke_spawn_cube;
  table.binders[(uint32_t)command_id::spawn_sphere] = &invoke_spawn_sphere;
  table.binders[(uint32_t)command_id::map] = &invoke_map;
  table.binders[(uint32_t)command_id::noclip] = &invoke_noclip;
  table.binders[(uint32_t)command_id::join_game] = &invoke_join_game;
  table.binders[(uint32_t)command_id::spectate] = &invoke_spectate;
  table.binders[(uint32_t)command_id::sv_mem_report] = &invoke_sv_mem_report;
  table.binders[(uint32_t)command_id::sv_frame_report] = &invoke_sv_frame_report;
  table.binders[(uint32_t)command_id::sv_hitch_report] = &invoke_sv_hitch_report;
}

} // namespace cvars
