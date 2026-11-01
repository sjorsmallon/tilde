#include "../message_handlers.hpp"

#include "../../shared/cvars/cvar_console.hpp"
#include "../../shared/log.hpp"
#include "../server_context.hpp"
#include "../server_messages.hpp"

#include <string>

namespace server::messages
{

// A line typed at the client, run here with that client as its caller; the
// reply, or an "OK", goes back to the same client.
void on_console_line(server_context_t &context, int32_t slot, const shared::Console_Line &value)
{
  const std::string line = value.line.c_str();
  log_terminal("Command from slot {}: {}", slot, line);

  cvars::command_context_t command_context{.caller_slot = slot};
  std::string reply;
  const cvars::console_result_t result =
      cvars::execute_console_line(*context.cvars, *context.commands, line, command_context, &reply);

  if (result == cvars::console_result_t::unknown_name)
    log_terminal("Unknown command from slot {}: {}", slot, line);

  send_server_text(context, slot, reply.empty() ? ("OK: " + line) : reply);
}

} // namespace server::messages
