#include "../client_context.hpp"
#include "../console.hpp"
#include "../message_handlers.hpp"
#include "../../shared/log.hpp"

namespace client::messages
{

// Console text from the server, forwarded to the console (not an announcement).
void on_server_text(client_context_t &context, const shared::Server_Text &value)
{
  (void)context;
  log_terminal("[SERVER] {}", value.text.c_str());
  console::get().print_line(value.text.c_str());
}

} // namespace client::messages
