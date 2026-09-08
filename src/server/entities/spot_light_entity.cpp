// Spot_Light_Entity's own handlers. Its Switchable half is written once
// against Enabled in src/server/traits/switchable.cpp; what is here is what
// only this type can answer.
#include "../../shared/entities/generated/entities/spot_light_entity_generated.hpp"
#include "../entity_io_context.hpp"

namespace entities
{

void set_color(Spot_Light_Entity &light, const Set_Color_Data &payload, server::input_context_t &)
{
  light.light.color = payload.color;
}

} // namespace entities
