// Mortal, on Damageable_Entity.
//
// `Damage` goes through inflict_damage rather than subtracting health here,
// and that is the whole point of routing it: the damage choke point owns the
// corpse gate, the colour scale, the destroyed-visual and (at ss11 step 5) the
// Died signal. A handler that touched `health` directly would be a second
// damage path that agrees today and drifts the first time either is tuned --
// the failure inflict_damage_batch already exists to prevent.
//
// `Set_Health` is the exception and is deliberately raw: it is an author's
// override, not damage. It takes no attacker, deals no knockback, and setting
// it to zero is not a kill -- which is why Kill is its own verb.
#include "../../shared/entities/generated/entity_io_generated.hpp"
#include "../damage.hpp"
#include "../entity_io_context.hpp"

namespace entities
{

void damage(Damageable_Entity &target, const Damage_Data &payload,
            server::input_context_t &context)
{
  server::damage_info_t info;
  info.victim_uid   = target.entity_id;
  info.attacker_uid = context.activator;
  info.amount       = (float)payload.amount;
  server::inflict_damage(context.server, info);
}

void kill(Damageable_Entity &target, const Kill_Data &, server::input_context_t &context)
{
  if (target.health <= 0)
    return;

  server::damage_info_t info;
  info.victim_uid   = target.entity_id;
  info.attacker_uid = context.activator;
  info.amount       = (float)target.health;
  server::inflict_damage(context.server, info);
}

void set_health(Damageable_Entity &target, const Set_Health_Data &payload,
                server::input_context_t &)
{
  target.health = payload.amount;
}

} // namespace entities
