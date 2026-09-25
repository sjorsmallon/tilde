#pragma once

namespace entities
{
struct Launcher_Entity;
}

namespace server
{

struct server_context_t;

// One shot of the launcher's weapon row along its orientation, spread and varied by (uid, shots_fired).
// The one place a launcher fires: its Fire handler and its own clock both come here.
void fire_launcher_shot(server_context_t& context, entities::Launcher_Entity& launcher);

// Every enabled launcher with a fire interval fires once its next_fire_tick has come. A launcher
// that was off does not catch up: it fires once and counts the interval from there.
void update_launchers(server_context_t& context);

} // namespace server
