#pragma once

#include "../shared/network/entity_snapshot.hpp"

#include <cstdint>
#include <optional>

namespace game
{
class S2C_EntityPackage;
}

namespace client
{

struct client_context_t;

// What one accepted S2C_EntityPackage said. The frame is the world;
// latest_processed_input_number is the server's ack of our input stream, riding
// the same message because snapshots are the only regular S2C traffic -- the
// mirror of held_snapshot_tick hitching a ride on C2S_ClientInput.
struct decoded_snapshot_t
{
  ::network::snapshot_frame_t frame;
  std::optional<uint32_t>     latest_processed_input_number;
};

// Decode one package against the history we hold. Reads the context and writes
// NOTHING to it: a packet we drop must leave no trace, and that is worth a
// return value rather than a promise about the order of statements in a
// hundred-line loop. Empty means dropped -- a stale tick, a baseline we no
// longer hold, or an undecodable payload -- and each of those logs its own
// reason before returning.
[[nodiscard]] std::optional<decoded_snapshot_t>
try_decode_snapshot(const client_context_t& context, const game::S2C_EntityPackage& package);

// Tick N becomes the NEWEST of the snapshots the client holds -- the state named
// by C2S_ClientInput::held_snapshot_tick. Newest of, not the only one: the ring
// keeps 32, and try_decode_snapshot resolves a baseline with find(tick) against
// any of them, because a delta the server built before our ack reached it names
// an older one and still has to decode.
//
// That sentence is the postcondition; the numbered steps in the .cpp are what it
// means for each consumer:
//
//   the rings   -- N is the new interpolation HORIZON, not the new render time.
//                  The cursor still trails it by cl_interpolation_delay_ticks.
//   this frame  -- received_server_update is set, which ARMS the reconciliation
//                  block later in Play_State::update. Not just storage: it
//                  schedules work for a later phase of the same frame.
//   from now on -- a delta naming N decodes, anything <= N is refused as stale,
//                  and the next C2S reports N so the server deltas against it.
//
// MONOTONIC, and that is what lets those three key off "what we hold" with no
// staleness check of their own: try_decode_snapshot refuses anything <=
// latest_processed_tick, and acknowledge() only moves on tick > acked_tick.
//
// It never sees a delta and cannot tell whether the packet was one --
// try_decode_snapshot consumes that distinction whole and hands over a COMPLETE
// frame either way, which is why everything is replaced wholesale below.
//
// Total: the decision to drop was already made, so no path out of it leaves the
// context half-updated.
//
// IT TAKES NO dt, AND THAT IS THE POINT OF THE SPLIT. This runs once per
// received packet -- zero-to-many times per frame, paced by the server's clock
// rather than the renderer's. Anything advanced by frame time (a tween, a decay,
// the interpolation cursor, the camera) belongs to Play_State::update and would
// be stepped a random number of times if it were reached from here. Living in
// its own translation unit with this parameter list is the enforcement: there is
// no dt in scope to reach for, and no camera either.
void advance_newest_held_snapshot(client_context_t& context, decoded_snapshot_t&& decoded);

} // namespace client
