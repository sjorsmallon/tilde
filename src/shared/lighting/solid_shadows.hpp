#pragma once

// A shadows_are_solid light's volumes made SOLID (projector_plan.md ss1): each kept volume the light throws is
// one piece of a mover whose two poses are equal, as a solid beam is, so a hull stands on a crate's shadow
// and a ball thrown into the dark rests. The volume is already a convex set of planes (its silhouette
// sides, a far cap where the light has a range, and the caster's lit faces), so the piece is the volume
// closed by a box SOLID_SHADOW_REACH past the caster where it is open: a directional light's prism and an
// unranged point light's pyramid end there. One mover per light, carrying nobody and crushing nobody.
// Like a solid beam it casts no shadow and stops none (compute_shadow_volumes skips a light's movers).

#include "collision/movers.hpp"
#include "shadow_volume.hpp"
#include "span.hpp"

#include <optional>
#include <vector>

namespace shared
{

struct Entity_System;

inline constexpr float SOLID_SHADOW_REACH = SHADOW_VOLUME_DRAWN_REACH;

// The volume as one collision piece, or empty when its planes bound nothing within reach.
[[nodiscard]] std::optional<collision_piece_t> try_build_solid_shadow_piece(const shadow_volume_t& volume);

// APPENDS one mover per shadows_are_solid light whose volumes are in `shadow_volumes`, after compute_shadow_volumes.
void compute_solid_shadows(const Entity_System& system, Span<const shadow_volume_t> shadow_volumes,
                           std::vector<mover_t>& out);

} // namespace shared
