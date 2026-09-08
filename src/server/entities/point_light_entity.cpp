// Point_Light_Entity's own handlers.
//
// One handler PER TYPE rather than one against Light, and that is the
// distinction `requires` draws: a light's colour is its own field, but a future
// Colorable that tints a decal or a fog volume writes something else entirely.
// Switchable is the opposite case and has the `requires` form, so this type's
// enable/disable are not here -- they are in src/server/traits/switchable.cpp,
// written once for everything that carries an Enabled.
//
// Colorable's Color_Changed signal is not emitted here. A handler only
// REQUESTS; a signal is emitted by the SYSTEM at the tick the state change
// becomes true (entity_io_def.md ss7). For a light that happens to be the same
// instant, but making the exception here is how the rule stops being one.
#include "../../shared/entities/generated/entities/point_light_entity_generated.hpp"
#include "../entity_io_context.hpp"

namespace entities
{

void set_color(Point_Light_Entity &light, const Set_Color_Data &payload,
               server::input_context_t &)
{
  light.light.color = payload.color;
}

} // namespace entities
