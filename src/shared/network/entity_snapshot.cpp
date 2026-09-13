#include "entity_snapshot.hpp"

#include "../log.hpp"

namespace network
{

namespace
{

// The records one TYPE contributes against a baseline: the spawned, the
// changed, the removed. A uid is resolved in the other frame through its index,
// so a type's pool is walked once and never searched.
//
// Counted in its own pass because the count is a var_uint at the head of the
// stream, so it has to be known before the first record is written.
uint32_t count_records(const snapshot_frame_t& current, const snapshot_frame_t* baseline,
                       entities::entity_type type)
{
  uint32_t count = 0;

  const shared::Entity_Pool& pool = current.entities.pools[(uint32_t)type];
  for (uint32_t slot = 0; slot < pool.count; ++slot)
  {
    const entities::Entity* entity = pool.at(slot);
    if (baseline == nullptr)
    {
      ++count; // no baseline at all: every entity is a full update
      continue;
    }

    const entities::Entity* base = baseline->entities.try_find(entity->entity_id);
    if (base == nullptr || has_networked_changes(*entity, *base))
      ++count;
  }

  if (baseline != nullptr)
  {
    const shared::Entity_Pool& baseline_pool = baseline->entities.pools[(uint32_t)type];
    for (uint32_t slot = 0; slot < baseline_pool.count; ++slot)
      if (current.entities.try_find(baseline_pool.at(slot)->entity_id) == nullptr)
        ++count;
  }

  return count;
}

void write_records(Bit_Writer& writer, const snapshot_frame_t& current,
                   const snapshot_frame_t* baseline, entities::entity_type type)
{
  const shared::Entity_Pool& pool = current.entities.pools[(uint32_t)type];
  for (uint32_t slot = 0; slot < pool.count; ++slot)
  {
    const entities::Entity* entity = pool.at(slot);
    const entities::Entity* base   = nullptr;

    if (baseline != nullptr)
    {
      base = baseline->entities.try_find(entity->entity_id);

      // Unchanged. Say nothing: the receiver carries it over from the same
      // baseline, which is the whole point of explicit removal existing.
      if (base != nullptr && !has_networked_changes(*entity, *base))
        continue;
    }

    write_var_uint(writer, (uint32_t)type);
    write_var_uint(writer, entity->entity_id);
    writer.write_bit(false);
    serialize_entity(writer, *entity, base);
  }

  // Removals are relative to the baseline, so a full update has none by
  // construction -- there is nothing for the receiver to still be holding.
  if (baseline == nullptr)
    return;

  const shared::Entity_Pool& baseline_pool = baseline->entities.pools[(uint32_t)type];
  for (uint32_t slot = 0; slot < baseline_pool.count; ++slot)
  {
    const shared::entity_uid_t uid = baseline_pool.at(slot)->entity_id;
    if (current.entities.try_find(uid) != nullptr)
      continue;

    write_var_uint(writer, (uint32_t)type);
    write_var_uint(writer, uid);
    writer.write_bit(true);
  }
}

} // namespace

void snapshot_frame_t::copy_replicated_entities_from(const shared::Entity_System& world)
{
  for (entities::entity_type type : entities::replicated_entity_types())
  {
    const shared::Entity_Pool& pool = world.pools[(uint32_t)type];
    for (uint32_t slot = 0; slot < pool.count; ++slot)
    {
      const entities::Entity* entity = pool.at(slot);
      entities.add_entity(entity->entity_id, entity);
    }
  }
}

void serialize_snapshot(Bit_Writer& writer, const snapshot_frame_t& current,
                        const snapshot_frame_t* baseline)
{
  uint32_t record_count = 0;
  for (entities::entity_type type : entities::replicated_entity_types())
    record_count += count_records(current, baseline, type);

  write_var_uint(writer, record_count);

  for (entities::entity_type type : entities::replicated_entity_types())
    write_records(writer, current, baseline, type);
}

bool deserialize_snapshot(Bit_Reader& reader, const snapshot_frame_t* baseline,
                          snapshot_frame_t& out_frame)
{
  // Seed from the baseline: every entity the sender did not mention is
  // unchanged, not gone.
  if (baseline != nullptr)
    out_frame.entities = baseline->entities;
  else
    out_frame.entities.reset();

  const uint32_t record_count = read_var_uint(reader);

  for (uint32_t index = 0; index < record_count; ++index)
  {
    const uint32_t             raw_type = read_var_uint(reader);
    const shared::entity_uid_t uid      = read_var_uint(reader);
    const bool                 removed  = reader.read_bit();

    // A type we cannot decode is not skippable: how many bits its payload
    // occupies is only knowable from that type's field table, so the rest of
    // the stream is unreadable. Fail loudly and let the caller drop the packet
    // whole -- the ack will not advance, so the sender re-baselines.
    if (raw_type == 0 || raw_type >= entities::ENTITY_TYPE_COUNT ||
        !entities::entity_type_is_replicated((entities::entity_type)raw_type))
    {
      log_error("snapshot: record {} names entity type {} (uid {}), which is not replicated. "
                "The rest of the packet is undecodable; dropping it.",
                index, raw_type, uid);
      return false;
    }
    const entities::entity_type type = (entities::entity_type)raw_type;

    if (removed)
    {
      // The sender computed this removal against the baseline it named, and
      // the frame was seeded from that same baseline, so the uid has to be
      // here. A miss means the two rings disagree about a tick -- worth a line,
      // not a drop: a removal has no payload, so the stream is still readable.
      if (!out_frame.entities.destroy(uid))
        log_error("snapshot: record {} removes uid {}, which the baseline does not hold", index,
                  uid);
      continue;
    }

    // `entity` already holds the baseline's copy for this uid (the frame was
    // seeded from it), so an absent entry means the sender is spawning this
    // entity and a default-constructed value is the right thing to decode a
    // full update into. Decoded IN PLACE: nothing is pushed between taking the
    // pointer and reading the record, so it stays valid.
    entities::Entity* entity = out_frame.entities.try_find(uid);
    if (entity != nullptr && entity->type != type)
    {
      // A uid is never reused within a session and both rings reset on a map
      // change, so this is a corrupt stream, not a retype.
      log_error("snapshot: record {} says uid {} is a {}, but the baseline holds a {} under it. "
                "Dropping the packet.",
                index, uid, entities::entity_info(type).classname,
                entities::entity_info(entity->type).classname);
      return false;
    }
    if (entity == nullptr)
    {
      entity = out_frame.entities.add_default(type, uid);
      if (entity == nullptr)
        return false; // add_default has already said why
    }

    // A record that fails to decode has already logged why, and the read
    // position is mid-record -- the same unreadable-stream situation as an
    // unknown entity type above, so it takes the same exit.
    if (!deserialize_entity(reader, *entity))
      return false;

    // The record's uid is what indexes the frame, and entity_id is also a
    // networked field, so pin them together rather than leaving two answers to
    // "which entity is this".
    entity->entity_id = uid;
  }

  return true;
}

} // namespace network
