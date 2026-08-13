#pragma once

#include "cvars/generated/cvars_generated.hpp"
#include "bitstream.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace shared
{

// --- @Mirrored cvar values on the wire (CVAR TRACK step 4) ---
//
// Bitstream-native, NOT protobuf. It replaces S2C_CVarSync, which shipped every
// NAME, description and flag so the client could build stub registry entries.
// Both sides now compile the same generated tables out of cvars.def and the
// connect handshake refuses a client whose SCHEMA_HASH differs, so "what names
// exist" needs no message at all. What is left is VALUES, and only for the
// @Mirrored subset -- server-owned values the client must agree with because it
// feeds them into the same player_move() the server runs.
//
// GRAMMAR
//
//   cvar_values_message := count:var_uint  pair{count}
//   pair                := cvar_id:var_uint  text:string
//
// Ids are per-build indices into the generated table, safe on the wire for the
// same reason entity field indices are: the handshake already refused any client
// whose tables differ. Values ride as TEXT rather than raw bytes so the one
// cvar_to_text / cvar_from_text pair stays the only place cvar bytes become
// characters -- the same argument entity_reflection makes for field text.

struct cvar_value_t
{
  cvars::cvar_id id;
  std::string    text;
};

struct cvar_values_message_t
{
  std::vector<cvar_value_t> values;
};

void                  serialize_cvar_values(network::Bit_Writer&         writer,
                                            const cvar_values_message_t& message);
cvar_values_message_t deserialize_cvar_values(network::Bit_Reader& reader);

// The whole @Mirrored set. Sent once per client at connect time, so a client
// that joins mid-match starts from the server's live values rather than from
// cvars.def defaults.
cvar_values_message_t collect_mirrored_cvars(const cvars::cvar_state_t& state);

// The @Mirrored members whose BYTES differ from the retained last-broadcast
// copy. Compare-based rather than write-hooked on purpose: a direct field write
// anywhere in server code replicates correctly, so there is no "must call Set()"
// trap -- which is why there is no Set(). Repair of a lost update falls out of
// the same property: an unacked change stays different and is collected again
// only once the retain step runs, so the caller must retain ONLY after the
// broadcast actually went out.
cvar_values_message_t
collect_changed_mirrored_cvars(const cvars::cvar_state_t& current,
                               const cvars::cvar_state_t& last_broadcast);

// Client side. Returns false if ANY pair was rejected (unknown id, an id whose
// cvar is not @Mirrored, or text that does not parse); every rejection is
// logged individually and leaves that value alone, and the remaining pairs are
// still applied -- one bad pair must not drop a whole tick's movement constants.
bool apply_cvar_values(cvars::cvar_state_t&         state,
                       const cvar_values_message_t& message);

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

} // namespace shared
