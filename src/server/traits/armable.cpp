// Armable, written ONCE against Inventory for every type that carries one.
//
// The receiver is here beside the component because the granted weapon records
// its owner's uid, which an Inventory& cannot name -- the same reason Mortal's
// handlers take one.
#include "../../shared/entities/generated/traits/armable_generated.hpp"
#include "../../shared/entity_uid.hpp"
#include "../../shared/log.hpp"
#include "../entity_io_context.hpp"
#include "../systems/inventory_system.hpp"

namespace entities
{

void grant_weapon(Entity& owner, Inventory& inventory, const Grant_Weapon_Data& payload,
                  server::input_context_t& context)
{
  if (server::try_grant_weapon(context.server, owner, inventory, payload.weapon,
                               payload.damage_type) == shared::null_entity_uid)
    log_error("grant_weapon: could not give {} a {} ({})", owner.entity_id,
              to_string(payload.weapon), to_string(payload.damage_type));
}

} // namespace entities
