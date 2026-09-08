#include "../../shared/entities/generated/traits/switchable_generated.hpp"
#include "../entity_io_context.hpp"

namespace entities
{

void enable(Entity &, Enabled &state, const Enable_Data &, server::input_context_t &)
{
  state.value = true;
}

void disable(Entity &, Enabled &state, const Disable_Data &, server::input_context_t &)
{
  state.value = false;
}

void toggle_enabled(Entity &, Enabled &state, const Toggle_Enabled_Data &,
                    server::input_context_t &)
{
  state.value = !state.value;
}

} // namespace entities
