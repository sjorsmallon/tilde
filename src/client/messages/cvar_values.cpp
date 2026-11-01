#include "../client_context.hpp"
#include "../message_handlers.hpp"
#include "../../shared/log.hpp"
#include "../../shared/network/cvar_mirror.hpp"

namespace client::messages
{

// The @Mirrored values the server pushed: the whole set at connect, the changed
// subset after. Applied to the launcher's one cvar_state_t.
void on_cvar_values(client_context_t &context, const shared::Cvar_Values &value)
{
  shared::apply_cvar_values(*context.cvars, value.values);

  for (const shared::Cvar_Value &cvar_value : value.values)
    log_terminal("[CLIENT] mirrored cvar '{}' = {}",
                 cvar_value.id < cvars::CVAR_COUNT ? cvars::cvar_info(static_cast<cvars::cvar_id>(cvar_value.id)).name
                                                   : "<unknown>",
                 cvar_value.text.c_str());
}

} // namespace client::messages
