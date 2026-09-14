#pragma once

#include "../shared/network/entity_snapshot.hpp"

namespace client
{

struct client_context_t;

// The one-shot sounds derived from replicated STATE crossing an edge between
// the previous held frame and the one just applied: a gunshot when a player's
// last_fire_tick advances, our own hitmarker when last_hit_tick does, a break
// when a damageable's health crosses zero, a clip when an emitter's play_count
// bumps. None of these is an effect on the wire, on purpose: an effect can be
// lost and a crate that broke silently is worse than one that broke a tick
// late, while a field rides the delta against the acked baseline and cannot be.
//
// Both frames are read, never the session copy before and after the write, so
// this is a consumer of the replicated state and not a step of applying it --
// and so the first snapshot after a connect (`previous` null) fires nothing,
// with no seeding flag and no retained stamp to clear. Call once per applied
// snapshot, after the frame is in the session (the session copy is what has
// the map's position, sound and reach).
void play_snapshot_edge_audio(client_context_t& context,
                              const ::network::snapshot_frame_t* previous,
                              const ::network::snapshot_frame_t& current);

} // namespace client
