#pragma once

#include "../shared/asset_id.hpp"
#include "../shared/span.hpp"
#include "entities/generated/entities_core_generated.hpp"

namespace client
{

// Sounds belong to a TYPE here and to a material later (impact_sound_plan.md §6), never to an instance.
// Names, because the table is constexpr; the play site resolves them with sound_id.
Span<const assets::asset_name_t> impact_sounds_for(entities::entity_type type);

assets::asset_name_t break_sound_for(entities::entity_type type);

} // namespace client
