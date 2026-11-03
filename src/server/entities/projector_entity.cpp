#include "../../shared/entities/generated/entities/projector_entity_generated.hpp"
#include "../entity_io_context.hpp"
#include "entities/generated/entities/projector_entity_generated.hpp"

namespace entities
{

void set_color(Projector_Entity &projector, const Set_Color_Data &payload, server::input_context_t &context)
{
  projector.light.color = payload.color;
  emit_color_changed(projector, Color_Changed_Data{payload.color}, context);
}

} // namespace entities
