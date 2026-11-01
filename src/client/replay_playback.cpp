#include "replay_playback.hpp"

#include "message_handlers.hpp"
#include "../shared/log.hpp"
#include "../shared/message_stream.hpp"
#include "../shared/network/bitstream.hpp"
#include "../shared/network/cvar_mirror.hpp"
#include "../shared/network/entity_snapshot.hpp"
#include "../shared/replay/replay_seek.hpp"

#include <optional>
#include <vector>

namespace client
{

namespace
{

void dispatch_record(const shared::replay_record_t& record, client_context_t& context)
{
  switch (record.kind)
  {
  // Each holds finished streams (the Snapshot one Entity_Package), received as
  // the payload a live Message datagram carries: the tick, then the streams.
  case shared::replay_record_kind_t::Snapshot:
  case shared::replay_record_kind_t::Messages:
  case shared::replay_record_kind_t::Cvar_Values:
  {
    std::vector<network::uint8> payload;
    shared::begin_server_message_payload(payload, record.tick);
    payload.insert(payload.end(), record.payload.data, record.payload.data + record.payload.size());
    receive_server_message_payload(context, Span<const network::uint8>{payload});
    return;
  }
  case shared::replay_record_kind_t::Player_View:
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
  playback.view_tracks           = shared::build_replay_view_tracks(playback.replay);
}

std::optional<int32_t> try_replay_first_person_slot(const replay_playback_t& playback, const cvars::cvar_state_t& cvars)
{
  if (!playback.active || !cvars.cl_replay_player_view ||
      !shared::replay_has_view_track(playback.view_tracks, cvars.cl_spectate_slot))
    return std::nullopt;
  return cvars.cl_spectate_slot;
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

void dispatch_due_replay_records(replay_playback_t& playback, float dt, client_context_t& context)
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

    std::vector<network::uint8> payload;
    for (Span<const uint8_t> streams : position->cvar_payloads)
    {
      shared::begin_server_message_payload(payload, target);
      payload.insert(payload.end(), streams.data, streams.data + streams.size());
      receive_server_message_payload(context, Span<const network::uint8>{payload});
    }

    network::Bit_Writer      writer;
    shared::message_stream_t stream;
    shared::write_entity_package(stream, network::entity_package_from_snapshot(writer, position->frame, nullptr,
                                                                               network::NO_INPUT_PROCESSED_YET));
    stream.finish();
    shared::begin_server_message_payload(payload, position->frame.tick);
    shared::append_message_stream(payload, stream);
    receive_server_message_payload(context, Span<const network::uint8>{payload});

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

    dispatch_record(*record, context);
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
