#pragma once

#include "../shared/cosmetic_events.hpp"
#include "client_context.hpp"

#include <vector>

namespace client
{

using effect_handler_fn = void (*)(client_context_t &context,
                                   const shared::effect_data_t &data);

// Install the single handler for `type`. Calling twice for the same type
// overwrites — that is treated as a programmer error in debug builds
// (log_error + assert) since the closed enum means there is exactly one
// owner per effect.
void register_effect_handler(shared::effect_type_t type,
                             effect_handler_fn handler);

// Wire up every built-in effect handler. Call once at client startup. Idempotent.
void register_all_effect_handlers();

// Dispatch every effect in the batch through the registry. Missing handlers
// log_error + assert (closed enum, server should never emit something the
// client can't handle).
void dispatch_received_effects(client_context_t &context,
                               const std::vector<shared::dispatched_effect_t> &effects);

} // namespace client
