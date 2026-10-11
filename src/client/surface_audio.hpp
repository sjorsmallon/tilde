#pragma once

// What a surface kind SOUNDS like: one row per Surface_Kind, literals resolved
// by sound_id (material_sound_plan.md step 2). The kind is derived from the
// map's material index every time it is asked, never stored.

#include "../shared/assets/asset_id.hpp"
#include "../shared/map/surface_kind.hpp"
#include "../shared/map/surface_lookup.hpp"

#include <cstdint>

namespace client
{

struct client_context_t;

// Index 0, out of range, or a material folder with no surface.txt is Default.
[[nodiscard]] shared::Surface_Kind surface_kind_for_material(const client_context_t& context,
                                                             uint16_t material_index);

// `step_index` is a running count per foot owner; the variants cycle through it.
[[nodiscard]] assets::sound_asset step_sound_for(shared::Surface_Kind kind, uint32_t step_index);
[[nodiscard]] assets::sound_asset land_sound_for(shared::Surface_Kind kind);

// Linear volumes: a step by how fast it was taken, a landing by how hard.
[[nodiscard]] float step_volume_for(float step_speed, float run_speed);
[[nodiscard]] float land_volume_for(float land_impact_speed);

} // namespace client
