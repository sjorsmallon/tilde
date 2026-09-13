#include "../../shared/entities/generated/entities/spot_light_entity_generated.hpp"
#include "../entity_io_context.hpp"

namespace entities
{

void set_color(Spot_Light_Entity &light, const Set_Color_Data &payload, server::input_context_t &context)
{
  light.light.color = payload.color;
  emit_color_changed(light, Color_Changed_Data{payload.color}, context);
}

} // namespace entities
