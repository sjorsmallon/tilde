#pragma once

#include "../shared/entities/generated/entities_generated.hpp"
#include "../shared/entity_uid.hpp"
#include "server_context.hpp"

namespace server
{

bool destroy_entity(server_context_t &context, shared::entity_uid_t uid);

} // namespace server
