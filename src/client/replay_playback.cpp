#include "replay_playback.hpp"

#include "../shared/log.hpp"
#include "../shared/network/cvar_mirror.hpp"
#include "../shared/replay_seek.hpp"

#include "game.pb.h"

#include <optional>

namespace client
{

namespace
{

void file_record(const shared::replay_record_t& record, network::Client_Inbox& inbox)
{
  switch (record.kind)
  {
  case shared::replay_record_kind_t::Snapshot:
  {
    const std::optional<shared::replay_snapshot_payload_t> snapshot = shared::try_split_replay_snapshot(record);
    if (!snapshot)
    {
      log_error("replay: the snapshot record at tick {} is too short to hold its baseline; skipped", record.tick);
      return;
    }
    game::S2C_EntityPackage& package = inbox.entity_updates.emplace_back();
    package.set_server_tick(static_cast<int32_t>(record.tick));
    if (snapshot->baseline_tick != 0)
      package.set_delta_from_tick(snapshot->baseline_tick);
    package.set_entity_data(snapshot->snapshot_bytes.data, snapshot->snapshot_bytes.size());
    return;
  }
  case shared::replay_record_kind_t::Effects:
  {
    game::S2C_EffectBatch& batch = inbox.effect_batches.emplace_back();
    if (!batch.ParseFromArray(record.payload.data, static_cast<int>(record.payload.size())))
    {
      log_error("replay: the effect batch at tick {} does not parse; skipped", record.tick);
      inbox.effect_batches.pop_back();
    }
    return;
  }
  case shared::replay_record_kind_t::Events:
  {
    game::S2C_GameEventBatch& batch = inbox.game_event_batches.emplace_back();
    if (!batch.ParseFromArray(record.payload.data, static_cast<int>(record.payload.size())))
    {
      log_error("replay: the event batch at tick {} does not parse; skipped", record.tick);
      inbox.game_event_batches.pop_back();
    }
    return;
  }
  case shared::replay_record_kind_t::Cvar_Values:
    inbox.cvar_value_messages.emplace_back(record.payload.data, record.payload.data + record.payload.size());
    return;
  case shared::replay_record_kind_t::Header:
  case shared::replay_record_kind_t::Map_Package:
  case shared::replay_record_kind_t::Index:
    return;
  }
}

} // namespace

void begin_replay_playback(replay_playback_t& playback, shared::replay_t&& replay,
                           const cvars::cvar_state_t& cvars)
{
  playback.replay                = std::move(replay);
  playback.active                = true;
  playback.next_record_offset    = playback.replay.first_tick_record_offset;
  playback.clock_tick            = playback.replay.index.first_tick;
  playback.reached_end           = false;
  playback.cvars_before_playback = cvars;
}

float replay_world_dt(const replay_playback_t& playback, float dt)
{
  return playback.paused ? 0.0f : dt * playback.speed;
}

bool replay_plays_at_normal_speed(const replay_playback_t& playback)
{
  return !playback.paused && playback.speed == 1.0f;
}

double replay_seconds_elapsed(const replay_playback_t& playback)
{
  return (playback.clock_tick - playback.replay.index.first_tick) / playback.replay.header.tickrate_hz;
}

double replay_seconds_total(const replay_playback_t& playback)
{
  return static_cast<double>(playback.replay.index.last_tick - playback.replay.index.first_tick) /
         playback.replay.header.tickrate_hz;
}

void request_replay_seek(replay_playback_t& playback, double seconds_from_start)
{
  const shared::replay_index_t& index = playback.replay.index;
  const double                  tick  = index.first_tick + seconds_from_start * playback.replay.header.tickrate_hz;
  if (tick <= index.first_tick)
    playback.pending_seek_tick = index.first_tick;
  else if (tick >= index.last_tick)
    playback.pending_seek_tick = index.last_tick;
  else
    playback.pending_seek_tick = static_cast<uint32_t>(tick);
}

void feed_replay_into_inbox(replay_playback_t& playback, float dt, network::Client_Inbox& inbox)
{
  if (!playback.active)
    return;

  if (playback.pending_seek_tick)
  {
    const uint32_t target = *playback.pending_seek_tick;
    playback.pending_seek_tick.reset();

    std::optional<shared::replay_position_t> position = shared::try_reconstruct_replay_at(playback.replay, target);
    if (!position)
    {
      log_error("replay: cannot rebuild the frame at tick {}; the replay stays where the seek left it empty", target);
      return;
    }

    for (Span<const uint8_t> payload : position->cvar_payloads)
      inbox.cvar_value_messages.emplace_back(payload.data, payload.data + payload.size());

    network::Bit_Writer writer;
    network::serialize_snapshot(writer, position->frame, nullptr);
    game::S2C_EntityPackage& package = inbox.entity_updates.emplace_back();
    package.set_server_tick(static_cast<int32_t>(position->frame.tick));
    package.set_entity_data(writer.buffer.data(), writer.buffer.size());

    playback.next_record_offset = position->next_record_offset;
    playback.clock_tick         = target;
    playback.reached_end        = false;
    return;
  }

  if (playback.reached_end)
    return;

  playback.clock_tick += static_cast<double>(replay_world_dt(playback, dt)) * playback.replay.header.tickrate_hz;

  while (true)
  {
    const std::optional<shared::replay_record_t> record =
        shared::try_read_replay_record(playback.replay, playback.next_record_offset);
    if (!record || record->kind == shared::replay_record_kind_t::Index)
    {
      playback.reached_end = true;
      log_terminal("replay: reached the end at tick {}", playback.replay.index.last_tick);
      return;
    }
    if (static_cast<double>(record->tick) > playback.clock_tick)
      return;

    file_record(*record, inbox);
    playback.next_record_offset = shared::replay_record_end(*record);
  }
}

void end_replay_playback(replay_playback_t& playback, cvars::cvar_state_t& cvars)
{
  if (!playback.active)
    return;
  shared::copy_cvars_from(cvars, playback.cvars_before_playback, cvars::mirrored_cvars());
  playback = {};
}

} // namespace client
