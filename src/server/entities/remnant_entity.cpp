#include "remnant_entity.hpp"

#include "../../shared/log.hpp"
#include "../entity_lifecycle.hpp"
#include "../server_context.hpp"

#include <vector>

namespace server
{

void claim_remnant(server_context_t& context, shared::entity_uid_t remnant_uid,
                   shared::entity_uid_t owner_uid)
{
  shared::Entity_System& entity_system = context.world.session.entity_system;

  std::vector<shared::entity_uid_t> superseded;
  for (const entities::Remnant_Entity& remnant : entity_system.entities_of<entities::Remnant_Entity>())
  {
    if (remnant.owner_uid == owner_uid && remnant.entity_id != remnant_uid)
      superseded.push_back(remnant.entity_id);
  }
  for (const shared::entity_uid_t uid : superseded)
    destroy_entity(context, uid);

  entities::Remnant_Entity* remnant = entity_system.get<entities::Remnant_Entity>(remnant_uid);
  if (remnant == nullptr)
  {
    log_error("claim_remnant: uid {} is not a remnant, so uid {} claims nothing", remnant_uid,
              owner_uid);
    return;
  }
  remnant->owner_uid = owner_uid;
}

} // namespace server
