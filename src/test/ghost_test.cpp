// shared/ghost -- the .ghost file, the always-on capture it is cut from, and the sampler.

#include "shared/ghost.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace shared;

namespace
{

entities::Player_Entity make_player(entity_uid_t uid, float x, float yaw, int32_t health = 100)
{
  entities::Player_Entity player{};
  player.entity_id             = uid;
  player.position              = {x, 0.f, 0.f};
  player.view_angle_yaw        = yaw;
  player.view_angle_pitch      = -10.f;
  player.body_yaw              = yaw;
  player.health.current_health = health;
  return player;
}

ghost_t make_ghost(uint32_t run_ticks)
{
  ghost_t ghost;
  ghost.tickrate_hz      = 60;
  ghost.map_content_hash = 0xabcd1234;
  ghost.run_ticks        = run_ticks;
  ghost.name             = "sjors the second";
  for (uint32_t tick = 0; tick <= run_ticks; ++tick)
    ghost.poses.push_back({.position   = {static_cast<float>(tick) * 10.f, 1.f, 2.f},
                           .view_yaw   = 170.f + static_cast<float>(tick) * 10.f,
                           .view_pitch = 5.f,
                           .body_yaw   = 0.f,
                           .alive      = true});
  return ghost;
}

void test_path_is_beside_the_map()
{
  assert(ghost_path_for("maps/bunnyhop.source") == "maps/bunnyhop.ghost");
  assert(ghost_path_for("bunnyhop.source") == "bunnyhop.ghost");
  std::printf("  path beside the map: ok\n");
}

void test_round_trip()
{
  ghost_t ghost = make_ghost(4);
  ghost.poses[2].alive = false;

  const std::vector<uint8_t> bytes  = serialize_ghost(ghost);
  const std::optional<ghost_t> read = try_parse_ghost(Span<const uint8_t>(bytes.data(), static_cast<uint32_t>(bytes.size())), "fixture");
  assert(read);
  assert(read->tickrate_hz == 60 && read->map_content_hash == 0xabcd1234 && read->run_ticks == 4);
  assert(read->name == "sjors the second");
  assert(read->poses.size() == 5);
  for (size_t index = 0; index < 5; ++index)
  {
    assert(read->poses[index].position.x == ghost.poses[index].position.x);
    assert(read->poses[index].view_yaw == ghost.poses[index].view_yaw);
    assert(read->poses[index].alive == ghost.poses[index].alive);
  }
  std::printf("  round trip: ok\n");
}

void test_refusals()
{
  std::vector<uint8_t> bytes = serialize_ghost(make_ghost(3));

  std::vector<uint8_t> truncated(bytes.begin(), bytes.end() - 1);
  assert(!try_parse_ghost(Span<const uint8_t>(truncated.data(), static_cast<uint32_t>(truncated.size())), "truncated"));

  std::vector<uint8_t> bad_magic = bytes;
  bad_magic[0] ^= 0xff;
  assert(!try_parse_ghost(Span<const uint8_t>(bad_magic.data(), static_cast<uint32_t>(bad_magic.size())), "bad magic"));

  std::vector<uint8_t> bad_version = bytes;
  bad_version[4] += 1;
  assert(!try_parse_ghost(Span<const uint8_t>(bad_version.data(), static_cast<uint32_t>(bad_version.size())), "bad version"));
  std::printf("  refusals: ok\n");
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

  assert(capture.tracks.at(7).size() == 3);
  assert(capture.tracks.at(7)[2].position.x == 102.f);
  const std::vector<ghost_pose_t>& late = capture.tracks.at(9);
  assert(late.size() == 3 && !late[0].alive && !late[1].alive && late[2].alive);

  const std::optional<ghost_t> ghost = try_extract_ghost(capture, 7, 2);
  assert(ghost && ghost->run_ticks == 2 && ghost->poses.size() == 3);
  assert(!try_extract_ghost(capture, 7, 3));
  assert(!try_extract_ghost(capture, 8, 0));

  capture_ghost_poses(capture, 200, 200, Span<const entities::Player_Entity>(players));
  assert(capture.phase_start_tick == 200);
  assert(capture.tracks.at(7).size() == 1);
  std::printf("  capture: late joiner padded, new phase restarts: ok\n");
}

void test_sample()
{
  ghost_t ghost = make_ghost(3);

  assert(!try_sample_ghost(ghost, -0.5));

  const std::optional<ghost_pose_t> middle = try_sample_ghost(ghost, 0.5);
  assert(middle && std::fabs(middle->position.x - 5.f) < 1e-4f);
  assert(std::fabs(middle->view_yaw - 175.f) < 1e-3f);

  const std::optional<ghost_pose_t> across_seam = try_sample_ghost(ghost, 1.5);
  assert(across_seam && std::fabs(across_seam->view_yaw - -175.f) < 1e-3f);

  const std::optional<ghost_pose_t> past_end = try_sample_ghost(ghost, 99.0);
  assert(past_end && past_end->position.x == 30.f);

  ghost.poses[1].alive = false;
  assert(!try_sample_ghost(ghost, 0.5));
  assert(try_sample_ghost(ghost, 2.5));
  std::printf("  sample: lerp, short-way yaw, hold at end, hidden when dead: ok\n");
}

void test_write_then_read_back()
{
  const std::string path = "cmake_build/ghost_test_fixture.ghost";
  std::filesystem::remove(path);
  assert(!try_read_ghost_file(path));

  write_ghost_file(path, make_ghost(10));
  const std::optional<ghost_t> read = try_read_ghost_file(path);
  assert(read && read->run_ticks == 10 && read->poses.size() == 11);
  assert(std::fabs(ghost_run_seconds(*read) - 10.f / 60.f) < 1e-6f);

  std::filesystem::remove(path);
  std::printf("  write then read back: ok\n");
}

} // namespace

int main()
{
  std::printf("[ghost]\n");
  test_path_is_beside_the_map();
  test_round_trip();
  test_refusals();
  test_capture_pads_late_joiners_and_restarts_on_a_new_phase();
  test_sample();
  test_write_then_read_back();
  std::printf("ghost_test: all passed\n");
  return 0;
}
