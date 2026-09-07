// Switchable's three actions, written ONCE against the Enabled component.
//
// This is what `requires Enabled` buys: three functions instead of three per
// opting-in type. It fits here because the verb IS the component -- enabling a
// trigger volume and enabling a lamp are the same operation on the same bool.
// Openable deliberately has no `requires`, because a door and a chest open
// differently. entity_io_def.md ss5.
//
// Nothing here reads the context: a switch is state, and what CONSUMES the
// state is each type's own system, which already runs every tick.
#include "../../shared/entities/generated/entity_io_generated.hpp"
#include "../entity_io_context.hpp"

namespace entities
{

void enable(Enabled &state, const Enable_Data &, server::input_context_t &)
{
  state.value = true;
}

void disable(Enabled &state, const Disable_Data &, server::input_context_t &)
{
  state.value = false;
}

void toggle_enabled(Enabled &state, const Toggle_Enabled_Data &, server::input_context_t &)
{
  state.value = !state.value;
}

} // namespace entities
