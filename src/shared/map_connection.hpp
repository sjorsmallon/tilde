#pragma once

#include "entities/generated/entity_io_generated.hpp"
#include "entity_uid.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// ============================================================================
// The per-INSTANCE half of entity I/O: THIS button opens THAT door.
//
// A connection is a row of MAP data, keyed by uid, stored beside the entities
// and never inside one -- exactly where `materials` and `attached_cvars` sit.
// entity_io_def.md ss6 is the design; the TYPE half (what a door can be told at
// all) is entities.def's `trait` declarations and appears nowhere in here.
//
// Targets are UIDS, never names. Uids are stable in the file (`next_uid`), so
// the format does not need names for identity, and `Entity::name` stays a
// display LABEL nothing resolves -- renaming breaks no wiring.
// ============================================================================

namespace shared
{

struct map_t;

// The two special targets are an enum beside the uid rather than a magic
// string, so a target that is not a uid is not spellable as one.
enum class connection_target_t : uint8_t
{
  Uid = 0,   // `target` names the entity
  Activator, // whoever caused the signal -- the player who walked into the trigger
  Self,      // the sender itself
};

const char* to_string(connection_target_t kind);
[[nodiscard]] std::optional<connection_target_t> try_connection_target_from_text(std::string_view text);

struct connection_t
{
  entity_uid_t            sender = null_entity_uid;
  entities::entity_signal signal = {};

  connection_target_t target_kind = connection_target_t::Uid;
  entity_uid_t        target      = null_entity_uid;

  // WHICH action, and the parameter it is called with, in one member: the
  // tag IS the action. A separate `action` field beside this would be a second
  // copy of the same fact, free to disagree with the union it selects.
  //
  // With `has_override`, the bytes are whatever the author typed, parsed
  // through the action's own field table; without it, the signal's payload is
  // copied straight over them at emit time -- legal exactly when the two field
  // tables are identical, which is what validate_map_connections checks.
  entities::action_data_t data         = {};
  bool                    has_override = false;

  float delay_seconds = 0.0f;
  bool  fire_once     = false;
};

// One reason one row cannot be run. The index is into map_t::connections, so
// a caller can act on the ROW rather than parse the sentence back apart -- the
// editor draws that row red, build_session drops it. A row with two things
// wrong with it produces two of these.
struct connection_refusal_t
{
  size_t      index = 0;
  std::string reason;
};

// Every reason this map's connections cannot be run, each naming the row by
// its entities' labels. Empty means every row is well-typed.
//
// ONE check, two policies, and that split is deliberate: build_session logs
// these and DROPS the rows they name, so the drain's fatal_error on a null
// dispatch cell stays unreachable, while the SERVER's map load treats a
// non-empty result as a refusal and keeps the map it is running. An editor
// that could not open a map with one bad row could not repair it either.
[[nodiscard]] std::vector<connection_refusal_t> validate_map_connections(const map_t& map);

// Whether a signal's payload can be handed to an action untouched. True when
// the two field tables agree name for name, type for type, offset for offset,
// and the two payloads are the same size -- which is what makes the emit a
// memcpy with no conversion anywhere.
[[nodiscard]] bool signal_payload_passes_through(entities::entity_signal signal,
                                                 entities::entity_action action);

} // namespace shared
