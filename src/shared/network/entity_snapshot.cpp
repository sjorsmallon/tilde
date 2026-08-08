#include "entity_snapshot.hpp"

#include "../log.hpp"

namespace network
{

namespace
{

template <typename Entity_T>
using entity_map_t = std::unordered_map<shared::entity_uid_t, Entity_T>;

// How many records one map contributes: the spawned, the changed, the removed.
// Counted in its own pass because the count is a var_uint at the head of the
// stream, so it has to be known before the first record is written.
template <typename Entity_T>
uint32_t count_records(const entity_map_t<Entity_T>& current,
                       const entity_map_t<Entity_T>* baseline)
{
  uint32_t count = 0;

  for (const auto& [uid, entity] : current)
  {
    if (baseline == nullptr)
    {
      ++count; // no baseline at all: every entity is a full update
      continue;
    }

    const auto found = baseline->find(uid);
    if (found == baseline->end() || has_networked_changes(entity, found->second))
      ++count;
  }

  if (baseline != nullptr)
    for (const auto& entry : *baseline)
      if (current.find(entry.first) == current.end())
        ++count;

  return count;
}

template <typename Entity_T>
void write_records(Bit_Writer& writer, const entity_map_t<Entity_T>& current,
                   const entity_map_t<Entity_T>* baseline)
{
  for (const auto& [uid, entity] : current)
  {
    const Entity_T* base = nullptr;

    if (baseline != nullptr)
    {
      const auto found = baseline->find(uid);
      if (found != baseline->end())
      {
        // Unchanged. Say nothing: the receiver carries it over from the same
        // baseline, which is the whole point of explicit removal existing.
        if (!has_networked_changes(entity, found->second))
          continue;
        base = &found->second;
      }
    }

    write_var_uint(writer, (uint32_t)Entity_T::static_type);
    write_var_uint(writer, uid);
    writer.write_bit(false);
    serialize_entity(writer, entity, base);
  }

  // Removals are relative to the baseline, so a full update has none by
  // construction -- there is nothing for the receiver to still be holding.
  if (baseline == nullptr)
    return;

  for (const auto& entry : *baseline)
  {
    if (current.find(entry.first) != current.end())
      continue;

    write_var_uint(writer, (uint32_t)Entity_T::static_type);
    write_var_uint(writer, entry.first);
    writer.write_bit(true);
  }
}

// Applies one record to the frame being reconstructed. `target` already holds
// the baseline's entry for this uid (the frame was seeded from it), so an
// absent entry means the sender is spawning this entity and a default-
// constructed value is the right thing to decode a full update into.
// Returns false if the record could not be decoded, which leaves the stream
// unreadable from that point — see deserialize_entity.
template <typename Entity_T>
bool apply_record(Bit_Reader& reader, entity_map_t<Entity_T>& target,
                  shared::entity_uid_t uid, bool removed)
{
  if (removed)
  {
    target.erase(uid);
    return true;
  }

  Entity_T   entity;
  const auto found = target.find(uid);
  if (found != target.end())
    entity = found->second;

  if (!deserialize_entity(reader, entity))
    return false;

  // The record's uid is what keys the map, and entity_id is also a networked
  // field, so pin them together rather than leaving two answers to "which
  // entity is this".
  entity.entity_id = uid;
  target[uid]      = entity;
  return true;
}

} // namespace

void serialize_snapshot(Bit_Writer& writer, const snapshot_frame_t& current,
                        const snapshot_frame_t* baseline)
{
  const uint32_t record_count =
      count_records(current.players, baseline ? &baseline->players : nullptr) +
      count_records(current.rockets, baseline ? &baseline->rockets : nullptr) +
      count_records(current.physics_bodies,
                    baseline ? &baseline->physics_bodies : nullptr);

  write_var_uint(writer, record_count);

  write_records(writer, current.players, baseline ? &baseline->players : nullptr);
  write_records(writer, current.rockets, baseline ? &baseline->rockets : nullptr);
  write_records(writer, current.physics_bodies,
                baseline ? &baseline->physics_bodies : nullptr);
}

bool deserialize_snapshot(Bit_Reader& reader, const snapshot_frame_t* baseline,
                          snapshot_frame_t& out_frame)
{
  // Seed from the baseline: every entity the sender did not mention is
  // unchanged, not gone.
  if (baseline != nullptr)
  {
    out_frame.players        = baseline->players;
    out_frame.rockets        = baseline->rockets;
    out_frame.physics_bodies = baseline->physics_bodies;
  }
  else
  {
    out_frame.players.clear();
    out_frame.rockets.clear();
    out_frame.physics_bodies.clear();
  }

  const uint32_t record_count = read_var_uint(reader);

  for (uint32_t index = 0; index < record_count; ++index)
  {
    const entities::entity_type  type    = (entities::entity_type)read_var_uint(reader);
    const shared::entity_uid_t   uid     = read_var_uint(reader);
    const bool                   removed = reader.read_bit();

    switch (type)
    {
      // A record that fails to decode has already logged why, and the read
      // position is mid-record — the same unreadable-stream situation as an
      // unknown entity type below, so it takes the same exit.
      case entities::entity_type::Player_Entity:
        if (!apply_record(reader, out_frame.players, uid, removed))
          return false;
        continue;

      case entities::entity_type::Rocket_Entity:
        if (!apply_record(reader, out_frame.rockets, uid, removed))
          return false;
        continue;

      case entities::entity_type::Physics_Body_Entity:
        if (!apply_record(reader, out_frame.physics_bodies, uid, removed))
          return false;
        continue;

      // Everything below is a real entity type that is simply never
      // replicated: map-placed entities the client already has from its own
      // map load. Listed rather than folded into `default` so that adding a
      // type to entities.def makes this switch a compile error -- if the new
      // type IS replicated, it needs a map on snapshot_frame_t and a case
      // here, and the compiler is what says so.
      case entities::entity_type::Invalid:
      case entities::entity_type::Player_Spawn_Entity:
      case entities::entity_type::Weapon_Entity:
      case entities::entity_type::Particle_Emitter_Entity:
      case entities::entity_type::Trigger_Volume_Entity:
      case entities::entity_type::Light_Entity:
        break;
    }

    // A type we cannot decode is not skippable: how many bits its payload
    // occupies is only knowable from that type's field table, so the rest of
    // the stream is unreadable. Fail loudly and let the caller drop the packet
    // whole -- the ack will not advance, so the sender re-baselines.
    log_error("snapshot: record {} names entity type {} (uid {}), which is not replicated. "
              "The rest of the packet is undecodable; dropping it.",
              index, (uint32_t)type, uid);
    return false;
  }

  return true;
}

} // namespace network
