#pragma once

#include "../../shared/entities/generated/entities_core_generated.hpp"

#include <string>
#include <string_view>

namespace client::hud
{

// The banner for the match moving from `before` to `after`, empty when there is
// nothing to say. A restart is an edge too: same phase, the next round number.
// `frag_leader_name` names who won a Frag_Limit match.
[[nodiscard]] std::string match_announcement_for(const entities::Match& before,
                                                 const entities::Match& after,
                                                 std::string_view frag_leader_name);

} // namespace client::hud
