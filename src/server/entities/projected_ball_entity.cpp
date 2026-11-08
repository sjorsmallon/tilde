#include "../../shared/entities/generated/entities/projected_ball_entity_generated.hpp"
#include "../entity_io_context.hpp"
#include "entities/generated/entities/projected_ball_entity_generated.hpp"

namespace entities
{

void set_color(Projected_Ball_Entity &ball, const Set_Color_Data &payload, server::input_context_t &context)
{
  ball.color = payload.color;
  emit_color_changed(ball, Color_Changed_Data{payload.color}, context);
}

} // namespace entities
