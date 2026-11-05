#pragma once

// A shadows_are_solid light's volumes made SOLID (projector_plan.md ss1): each kept volume the light throws is
// one piece of a mover whose two poses are equal, as a solid beam is, so a hull stands on a crate's shadow
// and a ball thrown into the dark rests. The volume is already a convex set of planes (its silhouette
// sides, a far cap where the light has a range, and the caster's lit faces), so the piece is the volume
// closed by a box SOLID_SHADOW_REACH past the caster where it is open: a directional light's prism and an
// unranged point light's pyramid end there. One mover per light, carrying nobody and crushing nobody; a
// volume a PLAYER casts (player_shadow_casters.hpp) goes into a mover named by the player's uid instead, one per
// player, so the player's own view can leave it out (predicted_world_t::mover_uids_to_ignore).
// Like a solid beam it casts no shadow and stops none (compute_shadow_volumes skips a light's movers).
//
// Building a piece from its planes clips every plane against every other, so it costs the plane count
// squared, and a crate under a fixed lamp throws the same planes every tick. The pieces are REMEMBERED in a
// solid_shadow_cache_t keyed by the volume (its light and its caster piece) and checked against the planes it
// was built from: a volume whose caster or light moved misses and is rebuilt, everything else is a copy.

#include "collision/movers.hpp"
#include "shadow_volume.hpp"
#include "span.hpp"

#include <optional>
#include <vector>

namespace shared
{

struct Entity_System;

inline constexpr float SOLID_SHADOW_REACH = SHADOW_VOLUME_DRAWN_REACH;

// An entry not asked for in this many cuts is dropped: its caster or light is gone, or has moved for good.
inline constexpr uint64_t SOLID_SHADOW_CACHE_KEEP_CUTS = 64;

// The volume as one collision piece, or empty when its planes bound nothing within reach.
[[nodiscard]] std::optional<collision_piece_t> try_build_solid_shadow_piece(const shadow_volume_t& volume);

// One remembered piece: which volume it is (the light, and the caster piece's key in the collect's order),
// the planes it was built from, and the piece, empty when those planes bound nothing.
struct solid_shadow_cache_entry_t
{
  entity_uid_t                     light        = null_entity_uid;
  uint32_t                         caster_piece = 0;
  uint64_t                         last_used    = 0;
  shadow_volume_t                  built_from;
  std::optional<collision_piece_t> piece;
};

struct solid_shadow_cache_t
{
  std::vector<solid_shadow_cache_entry_t> entries;
  // Which cut this is; an entry is stamped with it when used.
  uint64_t cut = 0;
  // How many pieces were built rather than copied, over the cache's life: the hit rate for a report, and the pin.
  uint64_t builds = 0;
};

// The volume's piece, copied from the cache when the same volume was built from the same planes before,
// built and remembered otherwise. Empty when the planes bound nothing within reach.
[[nodiscard]] std::optional<collision_piece_t> try_get_solid_shadow_piece_for_volume(solid_shadow_cache_t& cache,
                                                                                    const shadow_volume_t& volume);

// APPENDS one mover per shadows_are_solid light whose volumes are in `shadow_volumes`, after compute_shadow_volumes.
void compute_solid_shadows(const Entity_System& system, Span<const shadow_volume_t> shadow_volumes,
                           solid_shadow_cache_t& cache, std::vector<mover_t>& out);

} // namespace shared
