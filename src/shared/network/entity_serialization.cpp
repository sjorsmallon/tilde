#include "entity_serialization.hpp"

#include "../log.hpp"
#include "field_codec.hpp"

#include <cstring>

namespace network
{

using entities::leaf_field_t;

void serialize_entity(Bit_Writer& writer, const entities::Entity& entity,
                      const entities::Entity* baseline)
{
  if (baseline != nullptr && baseline->type != entity.type)
  {
    log_error("entity wire: baseline is a {} but the entity is a {} — sending a full update "
              "instead of a delta",
              entities::classname_of(baseline), entities::classname_of(&entity));
    baseline = nullptr;
  }

  const Span<const leaf_field_t> leaves = entities::networked_leaf_fields(entity.type);

  const uint8_t* entity_base   = reinterpret_cast<const uint8_t*>(&entity);
  const uint8_t* baseline_base = reinterpret_cast<const uint8_t*>(baseline);

  // Pass 1: the mask. Without a baseline every leaf is "changed", which is what
  // a full update is.
  for (const leaf_field_t& leaf : leaves)
  {
    const bool changed =
        baseline_base == nullptr ||
        std::memcmp(entity_base + leaf.offset, baseline_base + leaf.offset,
                    leaf.info->size_in_bytes) != 0;
    writer.write_bit(changed);
  }

  // Pass 2: the values, in the same order, so the reader can walk one list.
  for (const leaf_field_t& leaf : leaves)
  {
    if (baseline_base != nullptr &&
        std::memcmp(entity_base + leaf.offset, baseline_base + leaf.offset,
                    leaf.info->size_in_bytes) == 0)
      continue;
    write_field(writer, entity_base, *leaf.info, leaf.offset);
  }
}

bool has_networked_changes(const entities::Entity& entity,
                           const entities::Entity& baseline)
{
  if (baseline.type != entity.type)
    return true;

  const Span<const leaf_field_t> leaves = entities::networked_leaf_fields(entity.type);

  const uint8_t* entity_base   = reinterpret_cast<const uint8_t*>(&entity);
  const uint8_t* baseline_base = reinterpret_cast<const uint8_t*>(&baseline);

  for (const leaf_field_t& leaf : leaves)
    if (std::memcmp(entity_base + leaf.offset, baseline_base + leaf.offset,
                    leaf.info->size_in_bytes) != 0)
      return true;

  return false;
}

bool deserialize_entity(Bit_Reader& reader, entities::Entity& entity,
                        changed_fields_t* out_changed)
{
  const Span<const leaf_field_t> leaves = entities::networked_leaf_fields(entity.type);

  // The mask has to be read whole before any value, so it is buffered rather
  // than interleaved. That buffer IS the changed-field mask a caller can ask
  // for, so there is one of them rather than two.
  changed_fields_t  local_mask;
  changed_fields_t& mask = out_changed != nullptr ? *out_changed : local_mask;
  mask.resize((uint32_t)leaves.size());

  for (uint32_t index = 0; index < leaves.size(); ++index)
    mask.set(index, reader.read_bit());

  uint8_t* entity_base = reinterpret_cast<uint8_t*>(&entity);

  for (uint32_t index = 0; index < leaves.size(); ++index)
  {
    if (!mask.is_set(index))
      continue;
    // A rejected field leaves the read position mid-record, so there is
    // nothing to salvage: the entity is half-applied and every later field in
    // the stream is misaligned. Stop here and let the caller drop the packet.
    const leaf_field_t& leaf = leaves[index];
    if (!read_field(reader, entity_base, *leaf.info, leaf.offset))
      return false;
  }

  return true;
}

} // namespace network
