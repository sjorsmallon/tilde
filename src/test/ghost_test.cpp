// shared/ghost -- the .ghost file, the always-on capture it is cut from, and the sampler.

#include "shared/ghost.hpp"
#include "shared/network/ghost_transfer.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace shared;

namespace
{

entities::Player_Entity make_player(entity_uid_t uid, float x, float yaw, int32_t health = 100,
                                    entities::Team_Allegiance team = entities::Team_Allegiance::Red,
                                    const char* name = "runner")
{
  entities::Player_Entity player{};
  player.entity_id             = uid;
  player.team_allegiance       = team;
  player.display_name          = name;
  player.position              = {x, 0.f, 0.f};
  player.view_angle_yaw        = yaw;
  player.view_angle_pitch      = -10.f;
  player.body_yaw              = yaw;
  player.health.current_health = health;
  return player;
}

ghost_track_t make_track(uint32_t run_ticks, entities::Team_Allegiance team, const char* name, float x_offset)
{
  ghost_track_t track;
  track.team = team;
  track.name = name;
  for (uint32_t tick = 0; tick <= run_ticks; ++tick)
    track.poses.push_back({.position   = {x_offset + static_cast<float>(tick) * 10.f, 1.f, 2.f},
                           .view_yaw   = 170.f + static_cast<float>(tick) * 10.f,
                           .view_pitch = 5.f,
                           .body_yaw   = 0.f,
                           .alive      = true});
  return track;
}

ghost_t make_ghost(uint32_t run_ticks, uint32_t track_count = 1)
{
  ghost_t ghost;
  ghost.tickrate_hz      = 60;
  ghost.map_content_hash = 0xabcd1234;
  ghost.run_ticks        = run_ticks;
  ghost.tracks.push_back(make_track(run_ticks, entities::Team_Allegiance::Red, "sjors the second", 0.f));
  if (track_count >= 2)
    ghost.tracks.push_back(make_track(run_ticks, entities::Team_Allegiance::Blu, "a partner", 1000.f));
  return ghost;
}

Span<const uint8_t> span_of(const std::vector<uint8_t>& bytes)
{
  return Span<const uint8_t>(bytes.data(), static_cast<uint32_t>(bytes.size()));
}

void test_path_is_beside_the_map_and_names_the_party()
{
  assert(ghost_path_for("maps/bunnyhop.source", 1) == "maps/bunnyhop.1p.ghost");
  assert(ghost_path_for("maps/bunnyhop.source", 2) == "maps/bunnyhop.2p.ghost");
  assert(ghost_path_for("bunnyhop.source", 3) == "bunnyhop.3p.ghost");
  std::printf("  path beside the map, party size in the name: ok\n");
}

void test_two_track_round_trip()
{
  ghost_t ghost = make_ghost(4, 2);
  ghost.tracks[1].poses[2].alive = false;

  const std::vector<uint8_t>   bytes = serialize_ghost(ghost);
  const std::optional<ghost_t> read  = try_parse_ghost(span_of(bytes), "fixture");
  assert(read);
  assert(read->tickrate_hz == 60 && read->map_content_hash == 0xabcd1234 && read->run_ticks == 4);
  assert(read->tracks.size() == 2);
  assert(read->tracks[0].name == "sjors the second" && read->tracks[0].team == entities::Team_Allegiance::Red);
  assert(read->tracks[1].name == "a partner" && read->tracks[1].team == entities::Team_Allegiance::Blu);
  assert(ghost_party_name(*read) == "sjors the second + a partner");
  for (size_t track = 0; track < 2; ++track)
  {
    assert(read->tracks[track].poses.size() == 5);
    for (size_t index = 0; index < 5; ++index)
    {
      assert(read->tracks[track].poses[index].position.x == ghost.tracks[track].poses[index].position.x);
      assert(read->tracks[track].poses[index].view_yaw == ghost.tracks[track].poses[index].view_yaw);
      assert(read->tracks[track].poses[index].alive == ghost.tracks[track].poses[index].alive);
    }
  }
  std::printf("  two-track round trip: ok\n");
}

// The v1 layout: header, name, pose_count, poses.
std::vector<uint8_t> version_one_bytes()
{
  std::vector<uint8_t> bytes;
  const auto           append = [&](uint32_t value)
  {
    const uint8_t* first = reinterpret_cast<const uint8_t*>(&value);
    bytes.insert(bytes.end(), first, first + sizeof(value));
  };
  append(GHOST_MAGIC);
  append(1);  // version
  append(60); // tickrate_hz
  append(0);  // map_content_hash
  append(0);  // run_ticks
  append(0);  // name_length
  append(1);  // pose_count
  bytes.resize(bytes.size() + 25, 0);
  return bytes;
}

void test_refusals()
{
  const std::vector<uint8_t> bytes = serialize_ghost(make_ghost(3, 2));

  const std::vector<uint8_t> truncated(bytes.begin(), bytes.end() - 1);
  assert(!try_parse_ghost(span_of(truncated), "truncated"));

  std::vector<uint8_t> bad_magic = bytes;
  bad_magic[0] ^= 0xff;
  assert(!try_parse_ghost(span_of(bad_magic), "bad magic"));

  std::vector<uint8_t> bad_version = bytes;
  bad_version[4] += 1;
  assert(!try_parse_ghost(span_of(bad_version), "bad version"));

  assert(!try_parse_ghost(span_of(version_one_bytes()), "version 1"));

  // track_count is the u32 at byte 20: one more than the bytes hold, one fewer, none.
  std::vector<uint8_t> too_many_tracks = bytes;
  too_many_tracks[20]                  = 3;
  assert(!try_parse_ghost(span_of(too_many_tracks), "three tracks declared, two present"));

  std::vector<uint8_t> too_few_tracks = bytes;
  too_few_tracks[20]                  = 1;
  assert(!try_parse_ghost(span_of(too_few_tracks), "one track declared, two present"));

  std::vector<uint8_t> no_tracks = bytes;
  no_tracks[20]                  = 0;
  assert(!try_parse_ghost(span_of(no_tracks), "no tracks"));

  // The first track opens with its team, straight after the 24-byte header.
  std::vector<uint8_t> bad_team = bytes;
  bad_team[24]                  = 200;
  assert(!try_parse_ghost(span_of(bad_team), "a team outside the enum"));
  std::printf("  refusals: truncated, magic, version 1, track count against the bytes, team: ok\n");
}

void test_capture_pads_late_joiners_and_restarts_on_a_new_phase()
{
  ghost_capture_t capture;
  std::vector<entities::Player_Entity> players{make_player(7, 0.f, 0.f)};

  for (uint32_t tick = 100; tick <= 102; ++tick)
  {
    players[0].position.x = static_cast<float>(tick);
    if (tick == 102)
      players.push_back(make_player(9, 50.f, 0.f));
    capture_ghost_poses(capture, 100, tick, Span<const entities::Player_Entity>(players));
  }

  assert(capture.tracks.at(7).poses.size() == 3);
  assert(capture.tracks.at(7).poses[2].position.x == 102.f);
  const std::vector<ghost_pose_t>& late = capture.tracks.at(9).poses;
  assert(late.size() == 3 && !late[0].alive && !late[1].alive && late[2].alive);

  capture_ghost_poses(capture, 200, 200, Span<const entities::Player_Entity>(players));
  assert(capture.phase_start_tick == 200);
  assert(capture.tracks.at(7).poses.size() == 1);
  std::printf("  capture: late joiner padded, new phase restarts: ok\n");
}

void test_extraction_measures_the_party()
{
  ghost_capture_t capture;
  // Blu holds the LOWER uid: the order out is by team, not by uid and not by map iteration.
  std::vector<entities::Player_Entity> players{
      make_player(3, 0.f, 0.f, 100, entities::Team_Allegiance::Blu, "blu runner"),
      make_player(7, 0.f, 0.f, 100, entities::Team_Allegiance::Red, "red runner"),
      make_player(8, 0.f, 0.f, 0, entities::Team_Allegiance::Red, "never lived"),
  };

  for (uint32_t tick = 100; tick <= 104; ++tick)
  {
    // The Blu runner leaves after tick 102.
    if (tick == 103)
      players.erase(players.begin());
    for (entities::Player_Entity& player : players)
      player.position.x = static_cast<float>(tick);
    capture_ghost_poses(capture, 100, tick, Span<const entities::Player_Entity>(players));
  }

  const std::optional<ghost_t> ghost = try_extract_ghost(capture, 4);
  assert(ghost && ghost->run_ticks == 4);
  assert(ghost->tracks.size() == 2);
  assert(ghost->tracks[0].name == "red runner" && ghost->tracks[0].team == entities::Team_Allegiance::Red);
  assert(ghost->tracks[1].name == "blu runner" && ghost->tracks[1].team == entities::Team_Allegiance::Blu);
  assert(ghost_party_name(*ghost) == "red runner + blu runner");

  const ghost_track_t& left_early = ghost->tracks[1];
  assert(left_early.poses.size() == 5);
  assert(left_early.poses[2].alive && !left_early.poses[3].alive && !left_early.poses[4].alive);
  assert(left_early.poses[4].position.x == 102.f);
  assert(try_sample_ghost(left_early, 1.5));
  assert(!try_sample_ghost(left_early, 2.5));
  assert(!try_sample_ghost(left_early, 99.0));

  // Serializable as extracted: every track is run_ticks + 1 long.
  ghost_t whole     = *ghost;
  whole.tickrate_hz = 60;
  assert(try_parse_ghost(span_of(serialize_ghost(whole)), "extracted"));

  // A run shorter than the capture cuts every track to it, and a runner who lived only AFTER it is not counted.
  ghost_capture_t                      late_capture;
  std::vector<entities::Player_Entity> late_players{make_player(7, 0.f, 0.f)};
  for (uint32_t tick = 100; tick <= 104; ++tick)
  {
    if (tick == 104)
      late_players.push_back(make_player(9, 0.f, 0.f));
    capture_ghost_poses(late_capture, 100, tick, Span<const entities::Player_Entity>(late_players));
  }
  const std::optional<ghost_t> cut = try_extract_ghost(late_capture, 3);
  assert(cut && cut->tracks.size() == 1 && cut->tracks[0].poses.size() == 4);
  assert(try_extract_ghost(late_capture, 4)->tracks.size() == 2);

  const ghost_capture_t nobody;
  assert(!try_extract_ghost(nobody, 0));
  std::printf("  extraction: party measured, team order, early leaver padded dead, never-lived dropped: ok\n");
}

void test_sample()
{
  ghost_t        ghost = make_ghost(3);
  ghost_track_t& track = ghost.tracks[0];

  assert(!try_sample_ghost(track, -0.5));

  const std::optional<ghost_pose_t> middle = try_sample_ghost(track, 0.5);
  assert(middle && std::fabs(middle->position.x - 5.f) < 1e-4f);
  assert(std::fabs(middle->view_yaw - 175.f) < 1e-3f);

  const std::optional<ghost_pose_t> across_seam = try_sample_ghost(track, 1.5);
  assert(across_seam && std::fabs(across_seam->view_yaw - -175.f) < 1e-3f);

  const std::optional<ghost_pose_t> past_end = try_sample_ghost(track, 99.0);
  assert(past_end && past_end->position.x == 30.f);

  track.poses[1].alive = false;
  assert(!try_sample_ghost(track, 0.5));
  assert(try_sample_ghost(track, 2.5));
  std::printf("  sample: lerp, short-way yaw, hold at end, hidden when dead: ok\n");
}

void test_write_then_read_back()
{
  const std::string path = ghost_path_for("cmake_build/ghost_test_fixture.source", 2);
  std::filesystem::remove(path);
  assert(!try_read_ghost_file(path, 2));

  write_ghost_file(path, make_ghost(10, 2));
  const std::optional<ghost_t> read = try_read_ghost_file(path, 2);
  assert(read && read->run_ticks == 10 && read->tracks.size() == 2 && read->tracks[1].poses.size() == 11);
  assert(std::fabs(ghost_run_seconds(*read) - 10.f / 60.f) < 1e-6f);

  // A file whose name says another party size than its tracks is refused.
  assert(!try_read_ghost_file(path, 1));

  std::filesystem::remove(path);
  std::printf("  write then read back, a party size the file disagrees with refused: ok\n");
}

void test_transfer_messages_round_trip()
{
  network::Bit_Writer available_writer;
  serialize_ghost_available(available_writer, {.party_size = 2, .ghost_hash = 0xdeadbeef, .byte_count = 70000});
  network::Bit_Reader available_reader(available_writer.buffer.data(), available_writer.buffer.size());
  const ghost_available_message_t available = deserialize_ghost_available(available_reader);
  assert(available.party_size == 2 && available.ghost_hash == 0xdeadbeef && available.byte_count == 70000);

  network::Bit_Writer request_writer;
  serialize_request_ghost(request_writer, {.ghost_hash = 0xdeadbeef});
  network::Bit_Reader request_reader(request_writer.buffer.data(), request_writer.buffer.size());
  assert(deserialize_request_ghost(request_reader).ghost_hash == 0xdeadbeef);

  const std::vector<uint8_t> ghost_bytes = serialize_ghost(make_ghost(6, 2));
  network::Bit_Writer        data_writer;
  serialize_ghost_data(data_writer, {.party_size = 2, .ghost_hash = 7, .bytes = ghost_bytes});
  network::Bit_Reader data_reader(data_writer.buffer.data(), data_writer.buffer.size());
  const std::optional<ghost_data_message_t> data = try_deserialize_ghost_data(data_reader);
  assert(data && data->party_size == 2 && data->ghost_hash == 7 && data->bytes == ghost_bytes);

  // A payload cut short declares more bytes than it holds.
  network::Bit_Reader cut_reader(data_writer.buffer.data(), 8);
  assert(!try_deserialize_ghost_data(cut_reader));
  std::printf("  transfer messages: announce, request and data round trip, a short payload refused: ok\n");
}

void test_the_announcement_is_the_category_file()
{
  const std::string map_path = "cmake_build/ghost_test_announce.source";
  std::filesystem::remove(ghost_path_for(map_path, 1));
  std::filesystem::remove(ghost_path_for(map_path, 2));

  const ghost_announcement_t none = load_ghost_announcement(map_path, 2);
  assert(none.party_size == 2 && none.hash == 0 && none.bytes.empty());
  assert(load_ghost_announcement(map_path, 0).hash == 0);

  write_ghost_file(ghost_path_for(map_path, 2), make_ghost(5, 2));
  const ghost_announcement_t coop = load_ghost_announcement(map_path, 2);
  assert(coop.hash != 0 && coop.bytes == serialize_ghost(make_ghost(5, 2)));
  assert(coop.hash == compute_ghost_hash(span_of(coop.bytes)));
  assert(load_ghost_announcement(map_path, 1).hash == 0);

  // A faster run is another file, so another hash: that change IS the re-announce.
  write_ghost_file(ghost_path_for(map_path, 2), make_ghost(4, 2));
  assert(load_ghost_announcement(map_path, 2).hash != coop.hash);

  // A two-track file under the 1p name is not announced.
  write_ghost_file(ghost_path_for(map_path, 1), make_ghost(5, 2));
  assert(load_ghost_announcement(map_path, 1).hash == 0);

  assert(compute_ghost_hash(Span<const uint8_t>()) != 0);

  std::filesystem::remove(ghost_path_for(map_path, 1));
  std::filesystem::remove(ghost_path_for(map_path, 2));
  std::printf("  announcement: the category's file, hash 0 when absent or miscounted, moves with the file: ok\n");
}

} // namespace

int main()
{
  std::printf("[ghost]\n");
  test_path_is_beside_the_map_and_names_the_party();
  test_two_track_round_trip();
  test_refusals();
  test_capture_pads_late_joiners_and_restarts_on_a_new_phase();
  test_extraction_measures_the_party();
  test_sample();
  test_write_then_read_back();
  test_transfer_messages_round_trip();
  test_the_announcement_is_the_category_file();
  std::printf("ghost_test: all passed\n");
  return 0;
}
