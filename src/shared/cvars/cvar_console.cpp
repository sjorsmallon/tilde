#include "cvars/cvar_console.hpp"

#include "log.hpp"

#include <format>
#include <vector>

namespace cvars
{

namespace
{

bool is_console_whitespace(char c)
{
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Splits on runs of whitespace. The views point INTO `line`, so they live
// exactly as long as the caller's line does -- which is the whole call.
std::vector<std::string_view> tokenize(std::string_view line)
{
  std::vector<std::string_view> tokens;
  size_t                        index = 0;
  while (index < line.size())
  {
    while (index < line.size() && is_console_whitespace(line[index]))
      ++index;
    if (index >= line.size())
      break;
    size_t start = index;
    while (index < line.size() && !is_console_whitespace(line[index]))
      ++index;
    tokens.push_back(line.substr(start, index - start));
  }
  return tokens;
}

// Everything after the first token, with surrounding whitespace stripped. Used
// for a string<N> cvar, whose value is "the rest of the line" rather than one
// token.
std::string_view remainder_after_first_token(std::string_view line)
{
  size_t index = 0;
  while (index < line.size() && is_console_whitespace(line[index]))
    ++index;
  while (index < line.size() && !is_console_whitespace(line[index]))
    ++index;
  while (index < line.size() && is_console_whitespace(line[index]))
    ++index;
  size_t end = line.size();
  while (end > index && is_console_whitespace(line[end - 1]))
    --end;
  return line.substr(index, end - index);
}

void set_reply(std::string* out_reply, std::string text)
{
  if (out_reply)
    *out_reply = std::move(text);
}

} // namespace

std::string describe_cvar_flags(uint32_t flags)
{
  std::string text;
  if (flags & CVAR_FLAG_CLIENT)
    text += "[CLIENT] ";
  if (flags & CVAR_FLAG_SERVER)
    text += "[SERVER] ";
  if (flags & CVAR_FLAG_MIRRORED)
    text += "[MIRRORED] ";
  return text;
}

console_result_t execute_console_line(cvar_state_t&            state,
                                      const command_table_t&   table,
                                      std::string_view         line,
                                      const command_context_t& context,
                                      std::string*             out_reply)
{
  std::vector<std::string_view> tokens = tokenize(line);
  if (tokens.empty())
    return console_result_t::empty;

  const std::string_view name = tokens[0];

  cvar_id id{};
  if (find_cvar(name, &id))
  {
    const cvar_info_t& info = cvar_info(id);

    // A bare read is always local, even for a @Server cvar: the client's copy
    // of the table is compile-time identical to the server's, so printing the
    // local value costs no round trip. Only a WRITE has to respect ownership.
    if (tokens.size() == 1)
    {
      std::string value;
      if (!cvar_to_text(state, id, value))
      {
        set_reply(out_reply,
                  std::format("[error] {}: value could not be formatted", name));
        log_error("cvar_to_text failed for '{}' -- the generated table and "
                  "cvar_state_t disagree about its type",
                  info.name);
        return console_result_t::bad_arguments;
      }
      set_reply(out_reply, std::format("{} is {} {}\n  {}", info.name, value,
                                       describe_cvar_flags(info.flags),
                                       info.description));
      return console_result_t::ok;
    }

    // Writing a server-owned value from a networked client is the server's
    // call, so the whole line goes upstream and comes back through this same
    // function on the other side.
    if ((info.flags & (CVAR_FLAG_SERVER | CVAR_FLAG_MIRRORED)) &&
        table.forward_to_server)
    {
      table.forward_to_server(line);
      return console_result_t::forwarded;
    }

    std::string_view value_text;
    if (info.type == CVAR_TYPE_STRING)
    {
      value_text = remainder_after_first_token(line);
    }
    else if (tokens.size() > 2)
    {
      set_reply(out_reply,
                std::format("[error] {}: takes one value, got {}", name,
                            tokens.size() - 1));
      return console_result_t::bad_arguments;
    }
    else
    {
      value_text = tokens[1];
    }

    if (!cvar_from_text(state, id, value_text))
    {
      // cvar_from_text leaves the value ALONE on a parse failure, so the
      // previous value is still live -- say so, rather than letting the user
      // assume the set landed.
      set_reply(out_reply, std::format("[error] {}: '{}' is not a valid value; "
                                       "unchanged",
                                       name, value_text));
      return console_result_t::bad_arguments;
    }

    set_reply(out_reply, std::format("{} set to {}", info.name, value_text));
    return console_result_t::ok;
  }

  command_id command{};
  if (find_command(name, &command))
  {
    const command_info_t& info = command_info(command);

    if ((info.flags & CVAR_FLAG_SERVER) && table.forward_to_server)
    {
      table.forward_to_server(line);
      return console_result_t::forwarded;
    }

    command_binder_t binder = table.binders[(uint32_t)command];
    if (!binder)
    {
      // A null slot means the module that owns this command is not loaded in
      // this process. Two legitimate shapes:
      //   - a @Server command on a client with no forwarder (not connected),
      //   - a @Client command reaching a dedicated server, which has no
      //     game_client and therefore never called bind_client_commands.
      // Neither is an internal error, so neither asserts -- and this path is
      // reachable from a REMOTE line, which an assert would turn into a way to
      // abort the server. "Does the handler exist at all" is answered at LINK
      // time by the generated binder, which is the check that matters.
      if (info.flags & CVAR_FLAG_SERVER)
      {
        set_reply(out_reply,
                  std::format("[error] {}: server-only, and this process is not "
                              "connected to a server",
                              name));
        return console_result_t::not_connected;
      }
      set_reply(out_reply,
                std::format("[error] {}: not available in this process", name));
      log_error("execute_console_line: command '{}' has no handler -- its "
                "owning module is not loaded here",
                info.name);
      return console_result_t::no_handler;
    }

    // The binder parses the tokens against the command's declared signature
    // and calls the typed handler; on a count or parse failure it fills
    // out_reply with the generated usage string and calls nothing. The views
    // all point into `line`, which is what lets a `string...` rest parameter
    // recover the untokenized tail (see command_binder_t).
    Span<std::string_view> args(tokens.data() + 1,
                                static_cast<uint32_t>(tokens.size() - 1));
    if (!binder(args, context, out_reply))
      return console_result_t::bad_arguments;
    return console_result_t::ok;
  }

  set_reply(out_reply, std::format("Unknown command: {}", name));
  return console_result_t::unknown_name;
}

} // namespace cvars
