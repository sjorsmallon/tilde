#pragma once

#include "cvars/generated/cvars_generated.hpp"
#include "generated/messages_generated.hpp"
#include "message_stream.hpp"
#include "span.hpp"

#include <cstdint>
#include <vector>

namespace shared
{

// --- @Mirrored cvar values on the wire ---
//
// Both sides compile the same generated tables out of cvars.def and the
// connect handshake refuses a client whose SCHEMA_HASH differs, so "what names
// exist" needs no message at all. What is left is VALUES, and only for the
// @Mirrored subset -- server-owned values the client must agree with because it
// feeds them into the same player_move() the server runs. A value is a
// Cvar_Value row of messages.def: the per-build id, and the value as TEXT so
// the one cvar_to_text / cvar_from_text pair stays the only place cvar bytes
// become characters. Cvar_Values carries up to its bound of them; a larger set
// goes as several, which the reliable stream delivers in order.

// Up to the bound per Cvar_Values, as many Cvar_Values as it takes.
void write_cvar_value_messages(reliable_stream_t& stream, Span<const Cvar_Value> values);

// The whole @Mirrored set. Sent once per client at connect time, so a client
// that joins mid-match starts from the server's live values rather than from
// cvars.def defaults.
std::vector<Cvar_Value> collect_mirrored_cvars(const cvars::cvar_state_t& state);

// The @Mirrored members whose BYTES differ from the retained last-broadcast
// copy. Compare-based rather than write-hooked on purpose: a direct field write
// anywhere in server code replicates correctly, so there is no "must call Set()"
// trap -- which is why there is no Set(). Repair of a lost update falls out of
// the same property: an unacked change stays different and is collected again
// only once the retain step runs, so the caller must retain ONLY after the
// broadcast actually went out.
std::vector<Cvar_Value> collect_changed_mirrored_cvars(const cvars::cvar_state_t& current,
                                                       const cvars::cvar_state_t& last_broadcast);

// Client side. Returns false if ANY value was rejected (unknown id, an id whose
// cvar is not @Mirrored, or text that does not parse); every rejection is
// logged individually and leaves that value alone, and the remaining values are
// still applied -- one bad value must not drop a whole tick's movement constants.
bool apply_cvar_values(cvars::cvar_state_t& state, Span<const Cvar_Value> values);

// Client side, on disconnect. The @Mirrored values are exactly as stale as the
// connection that pushed them: nothing else in cvar_state_t is server-owned, so
// nothing else is reverted. Without this a dead server's movement constants
// keep steering the offline session -- play_state still runs player_move when
// not connected -- and survive into the first ticks of the next connection.
//
// CALLER BEWARE: the integrated launcher hands ONE cvar_state_t to both
// client::Init and server::Init, so an in-process server still owns these
// values and this must not run there. See reset_for_new_connection.
void revert_mirrored_cvars_to_defaults(cvars::cvar_state_t& state);

// The same revert over an arbitrary set, which is what the server unloading a
// map needs: a map's attached_cvars list is a set of cvars that were the MAP's
// while it was loaded, so unloading it puts exactly those back. One
// implementation for both, because "what a default is" (a default-constructed
// cvar_state_t) and "how a value is written" (the generated offset/size) must
// have one answer -- the mirrored revert is just this one over
// cvars::mirrored_cvars().
void revert_cvars_to_defaults(cvars::cvar_state_t&      state,
                              Span<const cvars::cvar_id> ids);

// The general form: those ids' values copied from `source`. A replay restores
// the values it overwrote through this.
void copy_cvars_from(cvars::cvar_state_t&       state,
                     const cvars::cvar_state_t& source,
                     Span<const cvars::cvar_id> ids);

} // namespace shared
