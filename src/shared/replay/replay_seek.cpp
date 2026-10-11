#include "replay_seek.hpp"

#include "log.hpp"

#include <utility>

namespace shared
{

std::optional<replay_position_t> try_reconstruct_replay_at(const replay_t& replay, uint32_t tick)
{
  const std::optional<replay_keyframe_t> keyframe = try_find_keyframe_at_or_before(replay.index, tick);
  if (!keyframe)
    return std::nullopt;

  replay_position_t position;

  uint64_t offset = replay.first_tick_record_offset;
  while (std::optional<replay_record_t> record = try_read_replay_record(replay, offset))
  {
    if (record->tick > tick || record->kind == replay_record_kind_t::Index)
      break;
    if (record->kind == replay_record_kind_t::Cvar_Values)
      position.cvar_payloads.push_back(record->payload);
    offset = replay_record_end(*record);
  }

  network::snapshot_frame_t scratch;
  bool                      holds_frame = false;
  offset                                = keyframe->byte_offset;
  while (std::optional<replay_record_t> record = try_read_replay_record(replay, offset))
  {
    if (record->tick > tick || record->kind == replay_record_kind_t::Index)
      break;
    offset = replay_record_end(*record);
    if (record->kind != replay_record_kind_t::Snapshot)
      continue;

    const std::optional<replay_snapshot_payload_t> snapshot = try_split_replay_snapshot(*record);
    if (!snapshot)
    {
      log_error("replay seek: the snapshot record at tick {} is too short to hold its baseline", record->tick);
      return std::nullopt;
    }

    const bool is_keyframe = snapshot->baseline_tick == 0;
    if (!is_keyframe && (!holds_frame || snapshot->baseline_tick != position.frame.tick))
    {
      log_error("replay seek: tick {} is a delta from tick {}, but the chain holds tick {}", record->tick,
                snapshot->baseline_tick, holds_frame ? position.frame.tick : 0);
      return std::nullopt;
    }

    network::Bit_Reader reader(snapshot->snapshot_bytes.data, snapshot->snapshot_bytes.size());
    if (!network::deserialize_snapshot(reader, is_keyframe ? nullptr : &position.frame, scratch))
    {
      log_error("replay seek: the snapshot at tick {} does not decode", record->tick);
      return std::nullopt;
    }
    scratch.tick = record->tick;
    std::swap(position.frame, scratch);
    holds_frame = true;
  }

  if (!holds_frame)
    return std::nullopt;
  position.next_record_offset = offset;
  return position;
}

} // namespace shared
