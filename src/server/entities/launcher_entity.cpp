#include "../../shared/entities/generated/entities/launcher_entity_generated.hpp"
#include "../../shared/entities/generated/entities_generated.hpp"
#include "../entity_io_context.hpp"
#include "../server_context.hpp"
#include "../systems/launcher_system.hpp"

namespace entities
{

// The switch is a mute: a disabled launcher swallows its Fire.
void fire(Launcher_Entity& launcher, const Fire_Data&, server::input_context_t& context)
{
  if (!launcher.switch_state.value)
    return;

  server::fire_launcher_shot(context.server, launcher);
}

} // namespace entities
