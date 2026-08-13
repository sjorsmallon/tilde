#include "client_context.hpp"

#include "../shared/network/cvar_mirror.hpp"

// The one place that answers "what resets when, and why". Each group's presence
// or absence below carries its reason on the line that does it; if a group ever
// needs to be half-cleared, that is the signal its boundary is drawn wrong, not
// a reason to open-code a field list at the call site again.

namespace client
{

void reset_for_new_connection(client_context_t& context)
{
  context.connection  = {};
  context.prediction  = {};
  context.replication = {};
  context.visuals     = {};

  // The one piece of connection-scoped state we do not own. Gated because the
  // integrated launcher hands ONE cvar_state_t to both client::Init and
  // server::Init, so an in-process server is still the authority on these
  // values and reverting them here would clobber the running game.
  // server_session is non-null exactly when such a server exists.
  if (context.server_session == nullptr && context.cvars != nullptr)
    shared::revert_mirrored_cvars_to_defaults(*context.cvars);
}

void reset_for_new_map(client_context_t& context)
{
  context.replication = {};
  context.visuals     = {};
}

} // namespace client
