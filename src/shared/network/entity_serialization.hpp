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

#include <vector>

namespace network
{

// Which leaves a delta actually wrote, indexed exactly like
// entities::networked_leaf_fields(entity.type) -- bit N is leaf N.
//
// This is the change-notification seam: "mesh id changed -> reload the asset"
// needs to know WHICH fields a snapshot touched, and the deserializer is the
// one place that already knows. Handing the mask back costs nothing because it
// is the mask the reader had to buffer anyway.
struct changed_fields_t
{
  // Every entity in entities.def is far under this; the vector is what keeps a
  // future wide type correct instead of silently truncating its mask.
  static constexpr uint32_t INLINE_CAPACITY = 64;

  uint32_t          count        = 0;
  uint64_t          inline_bits  = 0;
  std::vector<bool> overflow_bits;

  void resize(uint32_t leaf_count)
  {
    count       = leaf_count;
    inline_bits = 0;
    overflow_bits.assign(leaf_count > INLINE_CAPACITY ? leaf_count : 0, false);
  }

  void set(uint32_t index, bool value)
  {
    if (count > INLINE_CAPACITY)
      overflow_bits[index] = value;
    else if (value)
      inline_bits |= (uint64_t)1 << index;
  }

  bool is_set(uint32_t index) const
  {
    if (index >= count)
      return false;
    if (count > INLINE_CAPACITY)
      return overflow_bits[index];
    return (inline_bits & ((uint64_t)1 << index)) != 0;
  }

  bool any() const
  {
    if (count > INLINE_CAPACITY)
    {
      for (bool bit : overflow_bits)
        if (bit)
          return true;
      return false;
    }
    return inline_bits != 0;
  }
};

// Writes `entity` to the stream. With a baseline, only the networked leaves
// that differ from it; without one, every networked leaf (a full update).
//
// The baseline must be the same entity type -- it is the receiver's last known
// state of this same entity. A mismatch is a caller bug and is logged.
void serialize_entity(Bit_Writer& writer, const entities::Entity& entity,
                      const entities::Entity* baseline);

// True when serialize_entity would write at least one field, i.e. when the
// change mask for this pair is not empty. Same comparison serialize_entity
// makes, exposed so a snapshot can OMIT an unchanged entity entirely rather
// than spend a key and an all-zero mask on it every tick.
//
// A type mismatch counts as changed: it forces a full update, which is what
// serialize_entity falls back to there anyway.
bool has_networked_changes(const entities::Entity& entity,
                           const entities::Entity& baseline);

// Reads a stream written by serialize_entity into `entity`, whose type must be
// the one it was written from. Fields whose mask bit is clear keep their
// current value, which is what makes a delta a delta.
//
// `out_changed` is optional; when given it receives the mask this call applied.
void deserialize_entity(Bit_Reader& reader, entities::Entity& entity,
                        changed_fields_t* out_changed = nullptr);

inline void pack_entity_delta_for_update(game::S2C_EntityPackage& out_packet,
                                         const entities::Entity& entity,
                                         const entities::Entity* baseline = nullptr)
{
  // No baseline means every networked leaf is on the wire, which is a full
  // update, not a delta. The flag says which one the receiver is holding.
  out_packet.set_is_delta(baseline != nullptr);

  Bit_Writer writer;
  serialize_entity(writer, entity, baseline);

  out_packet.set_entity_data(writer.buffer.data(), writer.buffer.size());
}

} // namespace network
