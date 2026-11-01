#include "../message_handlers.hpp"

#include "../server_context.hpp"

namespace server::messages
{

// Filed, not run: the tick sorts its inputs by (slot, input_number), reads the
// riders off them and then runs them in the input step. Most of a datagram's
// rows are resends of inputs already run; the step's high-water check drops
// those, so nothing is deduplicated here.
void on_client_input(server_context_t &context, int32_t slot, const shared::Client_Input &value)
{
  context.incoming.client_inputs.push_back({.slot = slot, .input = value});
}

} // namespace server::messages
