#pragma once

#include "../../shared/entity_uid.hpp"

namespace server
{

struct server_context_t;

// A remnant just placed becomes `owner_uid`'s ONE remnant: every other remnant of theirs is
// destroyed, moved rather than added to, the ping marker's rule for the ping marker's reason.
void claim_remnant(server_context_t& context, shared::entity_uid_t remnant_uid,
                   shared::entity_uid_t owner_uid);

} // namespace server
