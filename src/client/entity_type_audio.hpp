#pragma once

#include "../shared/assets/generated/assets_generated.hpp"
#include "../shared/entities/generated/entities_generated.hpp"
#include "../shared/span.hpp"

namespace client
{

// Sounds belong to a TYPE here and to a material later (impact_sound_plan.md §6), never to an instance.
Span<const assets::sound_asset> impact_sounds_for(entities::entity_type type);

assets::sound_asset break_sound_for(entities::entity_type type);

} // namespace client
