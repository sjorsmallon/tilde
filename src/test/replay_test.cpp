// replay_def.md §8 steps 1, 2 and 5: the .replay container, the recorder that
// deltas against the last frame it wrote, and the seek that rebuilds a frame.

#include "shared/entities/generated/entities_generated.hpp"
#include "shared/network/cvar_mirror.hpp"
#include "shared/network/entity_snapshot.hpp"
#include "shared/network/snapshot_history.hpp"
#include "shared/replay_file.hpp"
#include "shared/replay_recorder.hpp"
#include "shared/replay_seek.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace shared;

namespace
{

const std::string FIXTURE_DIRECTORY = "cmake_build/replay_test_fixtures";

std::vector<uint8_t> read_bytes(const std::string& path)
{
  std::ifstream file(path, std::ios::binary);
  assert(file);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

replay_header_t make_header()
{
  replay_header_t header;
  header.schema_hash      = entities::SCHEMA_HASH;
  header.tickrate_hz      = 60;
  header.map_content_hash = 0xC0FFEE11;
  header.map_name         = "bunnyhop.source";
  header.date             = "2026-09-16T12:00";
  return header;
}

const std::vector<uint8_t> MAP_PACKAGE = {1, 2, 3, 4, 5, 6, 7};

std::string write_small_replay(const std::string& file_name)
{
  const std::string path = FIXTURE_DIRECTORY + "/" + file_name;
  std::optional<replay_writer_t> writer = try_open_replay_writer(path, make_header(), Span<const uint8_t>(MAP_PACKAGE));
  assert(writer);

  const std::vector<uint8_t> snapshot = {0xAA, 0xBB};
  const std::vector<uint8_t> effect   = {0xEE};
  for (uint32_t tick = 10; tick <= 40; ++tick)
  {
    write_replay_snapshot(*writer, tick, (tick - 10) % 10 == 0 ? 0 : tick - 1, Span<const uint8_t>(snapshot));
    if (tick % 3 == 0)
      write_replay_record(*writer, replay_record_kind_t::Effects, tick, Span<const uint8_t>(effect));
  }
  finish_replay(*writer);
  return path;
}

void test_header_and_records_round_trip()
{
  const std::string        path = write_small_replay("round_trip.replay");
  std::string              reason;
  std::optional<replay_t>  replay = try_read_replay_file(path, entities::SCHEMA_HASH, reason);
  assert(replay);

  assert(replay->header.tickrate_hz == 60);
  assert(replay->header.map_content_hash == 0xC0FFEE11);
  assert(replay->header.map_name == "bunnyhop.source");
  assert(replay->header.date == "2026-09-16T12:00");
  assert(replay->map_package.size() == MAP_PACKAGE.size());
  assert(memcmp(replay->map_package.data, MAP_PACKAGE.data(), MAP_PACKAGE.size()) == 0);
  assert(!replay->index_was_rebuilt);
  assert(replay->index.first_tick == 10 && replay->index.last_tick == 40);
  assert(replay->index.keyframes.size() == 4);

  uint32_t expected_tick   = 10;
  uint32_t snapshot_count  = 0;
  uint32_t effect_count    = 0;
  bool     saw_index       = false;
  uint64_t offset          = replay->first_tick_record_offset;
  while (std::optional<replay_record_t> record = try_read_replay_record(*replay, offset))
  {
    offset = replay_record_end(*record);
    if (record->kind == replay_record_kind_t::Index)
    {
      saw_index = true;
      continue;
    }
    assert(!saw_index);
    assert(record->tick >= expected_tick);
    expected_tick = record->tick;
    if (record->kind == replay_record_kind_t::Snapshot)
    {
      std::optional<replay_snapshot_payload_t> snapshot = try_split_replay_snapshot(*record);
      assert(snapshot);
      assert(snapshot->baseline_tick == ((record->tick - 10) % 10 == 0 ? 0 : record->tick - 1));
      assert(snapshot->snapshot_bytes.size() == 2 && snapshot->snapshot_bytes[0] == 0xAA);
      ++snapshot_count;
    }
    else
    {
      assert(record->kind == replay_record_kind_t::Effects);
      assert(record->tick % 3 == 0);
      ++effect_count;
    }
  }
  assert(saw_index);
  assert(offset == replay->bytes.size());
  assert(snapshot_count == 31);
  assert(effect_count == 10);
  std::printf("  header and records round trip: ok\n");
}

void check_keyframe_lookup(const replay_t& replay)
{
  assert(!try_find_keyframe_at_or_before(replay.index, 9));
  assert(try_find_keyframe_at_or_before(replay.index, 10)->tick == 10);
  assert(try_find_keyframe_at_or_before(replay.index, 19)->tick == 10);
  assert(try_find_keyframe_at_or_before(replay.index, 20)->tick == 20);
  assert(try_find_keyframe_at_or_before(replay.index, 1000)->tick == 40);

  for (const replay_keyframe_t& keyframe : replay.index.keyframes)
  {
    std::optional<replay_record_t> record = try_read_replay_record(replay, keyframe.byte_offset);
    assert(record && record->kind == replay_record_kind_t::Snapshot && record->tick == keyframe.tick);
    assert(try_split_replay_snapshot(*record)->baseline_tick == 0);
  }
}

void test_keyframes_are_found_with_and_without_the_index()
{
  const std::string path  = write_small_replay("index.replay");
  std::string       reason;
  std::optional<replay_t> with_index = try_read_replay_file(path, entities::SCHEMA_HASH, reason);
  assert(with_index && !with_index->index_was_rebuilt);
  check_keyframe_lookup(*with_index);

  uint64_t index_offset = 0;
  uint64_t offset       = with_index->first_tick_record_offset;
  while (std::optional<replay_record_t> record = try_read_replay_record(*with_index, offset))
  {
    if (record->kind == replay_record_kind_t::Index)
      index_offset = offset;
    offset = replay_record_end(*record);
  }
  assert(index_offset != 0);

  std::vector<uint8_t> without = with_index->bytes;
  without.resize((size_t)index_offset);
  std::optional<replay_t> rebuilt = try_open_replay(without, entities::SCHEMA_HASH, reason);
  assert(rebuilt && rebuilt->index_was_rebuilt);
  assert(rebuilt->index.first_tick == 10 && rebuilt->index.last_tick == 40);
  assert(rebuilt->index.keyframes.size() == with_index->index.keyframes.size());
  check_keyframe_lookup(*rebuilt);

  std::vector<uint8_t> crashed = without;
  crashed.resize(crashed.size() - 3);
  std::optional<replay_t> cut = try_open_replay(crashed, entities::SCHEMA_HASH, reason);
  assert(cut && cut->index_was_rebuilt);
  assert(cut->index.last_tick == 39);
  assert(cut->index.keyframes.size() == 3);
  assert(try_find_keyframe_at_or_before(cut->index, 1000)->tick == 30);
  std::printf("  keyframes with and without the index, and a cut tail: ok\n");
}

void test_refusals_are_named()
{
  const std::vector<uint8_t> good = read_bytes(write_small_replay("refusals.replay"));
  std::string                reason;

  std::vector<uint8_t> bad_magic = good;
  bad_magic[REPLAY_RECORD_HEADER_SIZE] = 'X';
  assert(!try_open_replay(bad_magic, entities::SCHEMA_HASH, reason));
  assert(reason.find("magic") != std::string::npos);

  std::vector<uint8_t> bad_version = good;
  bad_version[REPLAY_RECORD_HEADER_SIZE + sizeof(REPLAY_MAGIC)] = 99;
  assert(!try_open_replay(bad_version, entities::SCHEMA_HASH, reason));
  assert(reason.find("version 99") != std::string::npos);

  assert(!try_open_replay(good, entities::SCHEMA_HASH + 1, reason));
  assert(reason.find("schema hash") != std::string::npos);

  assert(!try_open_replay({}, entities::SCHEMA_HASH, reason));
  std::printf("  wrong magic, version and schema hash refused by name: ok\n");
}

entities::Player_Entity make_player(entity_uid_t uid, uint32_t tick)
{
  entities::Player_Entity player;
  player.entity_id               = uid;
  player.position                = {(float)tick * 1.25f + (float)uid, 64.0f, (float)(tick / 7)};
  player.health.current_health   = (int32_t)(100 - tick / 10);
  return player;
}

void build_world(Entity_System& world, uint32_t tick)
{
  world.reset();
  entities::Player_Entity first = make_player(1, tick);
  world.add_entity(first.entity_id, &first);
  if (tick % 5 != 0)
  {
    entities::Player_Entity second = make_player(2, 0);
    world.add_entity(second.entity_id, &second);
  }
  if (tick >= 50 && tick < 180)
  {
    entities::Rocket_Entity rocket;
    rocket.entity_id = 77;
    rocket.position  = {(float)tick * 30.0f, 10.0f, 0.0f};
    world.add_entity(rocket.entity_id, &rocket);
  }
}

std::vector<uint8_t> full_update_bytes(const network::snapshot_frame_t& frame)
{
  network::Bit_Writer writer;
  network::serialize_snapshot(writer, frame, nullptr);
  return writer.buffer;
}

// Every seek lands on the last recorded tick at or before the target, rebuilds
// exactly the frame recorded there, carries the cvar records up to it, and
// resumes at the first record past it. Twice, the same.
void test_seek_rebuilds_the_recorded_frame(const replay_t& replay, const std::vector<std::vector<uint8_t>>& expected)
{
  struct seek_case_t
  {
    uint32_t target;
    uint32_t landed;
    size_t   cvar_payloads;
  };
  const seek_case_t cases[] = {
      {1, 1, 1},    {29, 29, 1},   {30, 30, 1},   {31, 31, 1},   {99, 99, 1},    {120, 99, 1},
      {141, 141, 1}, {149, 149, 1}, {150, 150, 2}, {300, 300, 2}, {5000, 300, 2},
  };

  for (const seek_case_t& seek : cases)
  {
    std::optional<replay_position_t> first  = try_reconstruct_replay_at(replay, seek.target);
    std::optional<replay_position_t> second = try_reconstruct_replay_at(replay, seek.target);
    assert(first && second);
    assert(first->frame.tick == seek.landed);
    assert(full_update_bytes(first->frame) == expected[seek.landed]);
    assert(full_update_bytes(second->frame) == full_update_bytes(first->frame));
    assert(first->cvar_payloads.size() == seek.cvar_payloads);
    assert(first->next_record_offset == second->next_record_offset);

    const std::optional<replay_record_t> next = try_read_replay_record(replay, first->next_record_offset);
    assert(!next || next->tick > seek.target || next->kind == replay_record_kind_t::Index);
  }

  assert(!try_reconstruct_replay_at(replay, 0));
  std::printf("  seek: every target rebuilds the recorded frame, twice alike, gap included: ok\n");
}

void test_recorder_frames_decode_bit_exact_across_a_gap()
{
  const std::string path = FIXTURE_DIRECTORY + "/recorder.replay";
  cvars::cvar_state_t cvars;

  replay_recorder_t recorder;
  assert(try_start_replay_recording(recorder, path, make_header(), Span<const uint8_t>(MAP_PACKAGE), 30));

  std::vector<std::vector<uint8_t>> expected(301);
  Entity_System                     world;
  for (uint32_t tick = 1; tick <= 300; ++tick)
  {
    if (tick >= 100 && tick <= 140)
      continue;
    if (tick == 150)
      cvars.pm_maxspeed = 400.0f;

    build_world(world, tick);
    network::snapshot_frame_t frame;
    frame.tick = tick;
    frame.copy_replicated_entities_from(world);
    expected[tick] = full_update_bytes(frame);

    const std::vector<uint8_t> effect = {(uint8_t)tick};
    record_replay_tick(recorder, frame, tick % 4 == 0 ? Span<const uint8_t>(effect) : Span<const uint8_t>(), {}, cvars);
  }
  assert(recorder.keyframe_count == 10);
  assert(recorder.snapshot_count == 259);
  finish_replay_recording(recorder);
  assert(!recorder.active);

  std::string             reason;
  std::optional<replay_t> replay = try_read_replay_file(path, entities::SCHEMA_HASH, reason);
  assert(replay);
  assert(replay->index.keyframes.size() == 10);
  assert(replay->index.first_tick == 1 && replay->index.last_tick == 300);

  using history_t = network::Snapshot_History<network::snapshot_frame_t>;
  history_t history;

  uint32_t decoded_count   = 0;
  uint32_t cvar_records    = 0;
  uint32_t keyframes_seen  = 0;
  uint64_t offset          = replay->first_tick_record_offset;
  while (std::optional<replay_record_t> record = try_read_replay_record(*replay, offset))
  {
    offset = replay_record_end(*record);
    if (record->kind == replay_record_kind_t::Cvar_Values)
    {
      network::Bit_Reader         reader(record->payload.data, record->payload.size());
      const cvar_values_message_t message = deserialize_cvar_values(reader);
      assert(!message.values.empty());
      assert(cvar_records == 0 ? record->tick == 1 : record->tick == 150);
      if (cvar_records == 1)
        assert(message.values.size() == 1 && message.values[0].id == cvars::cvar_id::pm_maxspeed);
      ++cvar_records;
      continue;
    }
    if (record->kind != replay_record_kind_t::Snapshot)
      continue;

    std::optional<replay_snapshot_payload_t> snapshot = try_split_replay_snapshot(*record);
    assert(snapshot);
    const network::snapshot_frame_t* baseline = nullptr;
    if (snapshot->baseline_tick != 0)
    {
      baseline = history.find(snapshot->baseline_tick);
      assert(baseline != nullptr && "a delta names a frame playback no longer holds");
    }
    else
    {
      ++keyframes_seen;
    }

    network::snapshot_frame_t decoded;
    network::Bit_Reader        reader(snapshot->snapshot_bytes.data, snapshot->snapshot_bytes.size());
    assert(network::deserialize_snapshot(reader, baseline, decoded));
    decoded.tick = record->tick;
    assert(full_update_bytes(decoded) == expected[record->tick]);

    history.slot_for(record->tick) = std::move(decoded);
    ++decoded_count;
  }
  assert(decoded_count == 259);
  assert(keyframes_seen == 10);
  assert(cvar_records == 2);
  std::printf("  recorder: 259 frames decode bit-exact across a 41-tick gap, 10 keyframes: ok\n");

  test_seek_rebuilds_the_recorded_frame(*replay, expected);
}

void test_keyframe_interval_and_path()
{
  assert(replay_keyframe_interval_ticks(2.0f, 60) == 120);
  assert(replay_keyframe_interval_ticks(0.0f, 60) == 1);
  assert(replay_path_for("maps/bunnyhop.source", "mine") == "replays/mine.replay");
  const std::string dated = replay_path_for("maps/bunnyhop.source", "");
  assert(dated.rfind("replays/bunnyhop_", 0) == 0 && dated.size() == std::string("replays/bunnyhop_2026-09-16_120000.replay").size());
  std::printf("  keyframe interval and path: ok\n");
}

} // namespace

int main()
{
  std::printf("[TEST] replay_test\n");
  std::filesystem::create_directories(FIXTURE_DIRECTORY);

  test_header_and_records_round_trip();
  test_keyframes_are_found_with_and_without_the_index();
  test_refusals_are_named();
  test_recorder_frames_decode_bit_exact_across_a_gap();
  test_keyframe_interval_and_path();

  std::printf("[TEST] replay_test passed\n");
  return 0;
}
