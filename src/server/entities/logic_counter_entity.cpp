#include "../../shared/entities/generated/entities/logic_counter_entity_generated.hpp"
#include "../../shared/entity_system.hpp"
#include "../../shared/game_session.hpp"
#include "../../shared/log.hpp"
#include "../entity_io_context.hpp"
#include "../server_context.hpp"

namespace entities
{

void add(struct entities::Entity& entity,
       struct entities::Counter& counter,
       struct entities::Add_Data const& add_data,
       struct server::input_context_t& context)
{
   counter.value += add_data.amount;
   if (counter.value >= counter.limit)
   {
      entities::emit_limit_reached(entity, Limit_Reached_Data{}, context);
      log_terminal("limit reached for counter {} (uid {})", entity.name.c_str(), entity.entity_id);
   }
}

void reset(Entity&, Counter& state, const Reset_Data&, server::input_context_t&)
{
  state.value = 0;
}


}//namespace entities
