#pragma once

// The wire form of a WHOLE SNAPSHOT: the set of replicated entities at one
// server tick, delta-encoded against the snapshot the receiver acked.
//
// `entity_serialization.{hpp,cpp}` handles one entity's FIELDS. This handles
// the set: which entities exist, which changed, and which are GONE.
//
// ---------------------------------------------------------------------------
// Why removal is a record in here and not a separate spawn/despawn channel
// ---------------------------------------------------------------------------
//
// Membership is a property of "the world at tick N", exactly like a field
// value, so it is delta-encoded against the acked baseline exactly like one --
// and it inherits that rule's reliability for free. If the datagram carrying
// "entity 47 is gone" is lost, the client's ack does not advance past it, so
// the next snapshot is computed against an older baseline that STILL CONTAINS
// entity 47, and the removal is emitted again. It self-heals within a round
// trip for the same reason a changed field does.
//
// A separate despawn channel would need its own retransmit layer AND its own
// ordering against the snapshot stream -- a despawn landing before a snapshot
// that still lists the entity, or after one that already dropped it, is a
// second source of truth about world membership disagreeing with the first.
//
// ---------------------------------------------------------------------------
// What explicit removal buys: absence now means UNCHANGED
// ---------------------------------------------------------------------------
//
// The old format wrote every entity every tick, because "absent means gone"
// was the only way to express a removal. So an entity that had not moved still
// paid its key and an all-zero change mask, every tick, forever.
//
// Now the receiver SEEDS the frame from the baseline and applies records on
// top, so an entity the server did not mention is simply carried across
// unchanged. Only spawns, changes and removals are on the wire. That is the
// step from "whole-world snapshots" to real per-entity deltas.
//
// ---------------------------------------------------------------------------
// Grammar
// ---------------------------------------------------------------------------
//
//   snapshot   := record_count:var_uint  record*
//   record     := type:var_uint  uid:var_uint  removed:bit  payload?
//   payload    := <entity_serialization.hpp: leaf change mask + changed values>
//
//   `type`      is entities::entity_type. Enum values shift when entities.def
//               changes, which is exactly what SCHEMA_HASH in the connect
//               handshake refuses a build over -- so it is safe to send raw.
//               It replaces the old 255/254 "special slot" sentinels, which
//               shared a number space with client slots and so would have
//               collided once bot slots reached 254 (BOT_SLOT_BASE is 32).
//   `uid`       is entity_id, the key. It has to be readable BEFORE the
//               payload, because it is what selects the baseline to delta
//               against.
//   `removed`   set => the entity was in the baseline and is gone; there is no
//               payload. Never emitted in a full update (nothing to remove
//               relative to nothing).
//   no payload bit is needed for "spawned": an entity with no baseline entry
//               is written with every mask bit set, which IS a full update,
//               and the receiver starts it from a default-constructed value.
//
// An unknown `type` is unrecoverable rather than skippable -- payload length is
// only knowable from the type's field table -- so decoding fails and the caller
// drops the packet whole.

#include "../entities/entity_reflection.hpp"
#include "../entity_uid.hpp"
#include "bitstream.hpp"
#include "entity_serialization.hpp"

#include <optional>
#include <unordered_map>

namespace network
{

// The replicated world at one tick, keyed by entity uid. Both ends hold these
// and they must agree byte for byte: the server deltas against what it believes
// the client reconstructed, so the two structures are the same type on purpose.
//
// One map per replicated entity type. Adding a networked type means adding a
// map here plus a case in each of the three switches in the .cpp -- the same
// exhaustive-switch pattern the rest of the entity code uses, so the compiler
// names every site.
struct snapshot_frame_t
{
  uint32_t tick = 0;

  std::unordered_map<shared::entity_uid_t, entities::Player_Entity>       players;
  // A player's inventory, one entity per carried weapon. Spawned in the SAME
  // tick as its owner, which is what makes a frame self-consistent: a frame
  // reassembles or is dropped whole, so a player and the weapons its
  // inventory names arrive together and `weapons[active_weapon]` can never
  // resolve to an entity this receiver has not seen.
  std::unordered_map<shared::entity_uid_t, entities::Weapon_Entity>       weapons;
  std::unordered_map<shared::entity_uid_t, entities::Rocket_Entity>       rockets;
  std::unordered_map<shared::entity_uid_t, entities::Physics_Body_Entity> physics_bodies;
  // Map-PLACED, unlike the four above, and replicated anyway: it is the one
  // placeable type whose state changes at runtime. Only `health` and the Render
  // component are @Networked, so after the spawn record an untouched crate
  // costs nothing -- the geometry the client draws it with came from the map.
  std::unordered_map<shared::entity_uid_t, entities::Damageable_Entity>    damageables;

  void clear()
  {
    tick = 0;
    players.clear();
    weapons.clear();
    rockets.clear();
    physics_bodies.clear();
    damageables.clear();
  }
};

// Writes `current` as a delta against `baseline`, or as a full update when
// `baseline` is null. Unchanged entities are omitted entirely.
void serialize_snapshot(Bit_Writer& writer, const snapshot_frame_t& current,
                        const snapshot_frame_t* baseline);

// Reconstructs a frame from the stream. `out_frame` is seeded from `baseline`
// (or emptied when it is null) and then has the records applied to it, so
// entities the sender omitted survive unchanged.
//
// Returns false on an undecodable stream (unknown entity type). The stream
// position is then meaningless, so the caller must drop the whole packet --
// including anything appended after the snapshot.
bool deserialize_snapshot(Bit_Reader& reader, const snapshot_frame_t* baseline,
                          snapshot_frame_t& out_frame);

// ---------------------------------------------------------------------------
// Which kind of package this is
// ---------------------------------------------------------------------------
//
// A package is a FULL UPDATE or a DELTA AGAINST ONE TICK, never a mixture, and
// the two kinds are told apart by the PRESENCE of delta_from_tick -- not by a
// tick number reserved to mean "no tick". These two functions are the only
// places that touch the field, so the sentinel cannot leak back in: the sender
// hands over the baseline frame it actually used, and the receiver gets back a
// value whose two states are the two kinds.

// Records which frame `writer` deltaed against. Null baseline => full update,
// so the field is left absent rather than written as zero.
inline void set_snapshot_baseline(game::S2C_EntityPackage& package,
                                  const snapshot_frame_t* baseline)
{
  if (baseline != nullptr)
    package.set_delta_from_tick(baseline->tick);
  else
    package.clear_delta_from_tick();
}

// The tick this package is a delta against, or nothing at all when it is a full
// update. Absence is one of the two answers, not a failure, which is why this
// is not a try_.
[[nodiscard]] inline std::optional<uint32_t> snapshot_baseline_tick(
    const game::S2C_EntityPackage& package)
{
  if (!package.has_delta_from_tick())
    return std::nullopt;
  return package.delta_from_tick();
}

} // namespace network
