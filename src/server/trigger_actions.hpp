
#include "../shared/entities/entity_reflection.hpp"

namespace server
{

struct server_context_t;

// Runs `trigger`'s configured action against `player`. Reports and does nothing
// if the trigger carries a Trigger_Action value outside the enum, which can only
// come from memory corruption -- a bad value in a map file is caught at load.
void fire_trigger_action(server_context_t &context,
                         entities::Trigger_Volume_Entity &trigger,
                         entities::Player_Entity &player);

} // namespace server
