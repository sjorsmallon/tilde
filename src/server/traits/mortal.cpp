// Mortal, written ONCE against Health for every type that carries one.
//
// The receiver is here because a Health& cannot name its owner and every death
// consequence -- the >0 crossing, knockback, PLAYER_DIED, the respawn schedule
// -- hangs off the victim's uid inside inflict_damage. set_health is the one
// verb that could have done without it.
#include "../../shared/entities/generated/traits/mortal_generated.hpp"
#include "../damage.hpp"
#include "../entity_io_context.hpp"

namespace entities
{

void damage(Entity &target, Health &, const Damage_Data &payload,
            server::input_context_t &context)
{
  server::damage_info_t info;
  info.victim_uid   = target.entity_id;
  info.attacker_uid = context.activator;
  info.amount       = (float)payload.amount;
  server::inflict_damage(context.server, info);
}

void kill(Entity &target, Health &health, const Kill_Data &, server::input_context_t &context)
{
  if (health.current_health <= 0)
    return;

  server::damage_info_t info;
  info.victim_uid   = target.entity_id;
  info.attacker_uid = context.activator;
  info.amount       = (float)health.current_health;
  server::inflict_damage(context.server, info);
}

void set_health(Entity &, Health &health, const Set_Health_Data &payload,
                server::input_context_t &)
{
  health.current_health = payload.amount;
}

} // namespace entities
