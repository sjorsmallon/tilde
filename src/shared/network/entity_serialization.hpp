#pragma once

// The wire form of an entity: a changed-field bitmask followed by the changed
// values, driven by the generated field tables.
//
// TWO THINGS CHANGED AT THE CUTOVER and both are visible from here:
//
//   * @Networked is real. Only the networked LEAVES ride the wire, where the
//     macro system wrote every field of every entity regardless of flags. A
//     Rocket's damage numbers and a Light's whole configuration are no longer
//     on it.
//   * The mask is per LEAF, not per top-level field. A component used to be one
//     opaque blob in the mask, so touching render.material.color resent the
//     whole Render. Now each leaf has its own bit.
//
// Both sides build the leaf list from the same generated table via
// networked_leaf_fields(), so bit N means the same field on both ends by
// construction. SCHEMA_HASH at connect is what guarantees the tables match.

#include "../entities/entity_reflection.hpp"
#include "bitstream.hpp"
#include "packet.hpp"
#include "quantization.hpp"

namespace network
{

// Writes `entity` to the stream. With a baseline, only the networked leaves
// that differ from it; without one, every networked leaf (a full update).
//
// The baseline must be the same entity type -- it is the receiver's last known
// state of this same entity. A mismatch is a caller bug and is logged.
void serialize_entity(Bit_Writer& writer, const entities::Entity& entity,
                      const entities::Entity* baseline);

// Reads a stream written by serialize_entity into `entity`, whose type must be
// the one it was written from. Fields whose mask bit is clear keep their
// current value, which is what makes a delta a delta.
void deserialize_entity(Bit_Reader& reader, entities::Entity& entity);

inline void pack_entity_delta_for_update(game::S2C_EntityPackage& out_packet,
                                         const entities::Entity& entity,
                                         const entities::Entity* baseline = nullptr)
{
  out_packet.set_is_delta(true);

  Bit_Writer writer;
  serialize_entity(writer, entity, baseline);

  out_packet.set_entity_data(writer.buffer.data(), writer.buffer.size());
}

} // namespace network
