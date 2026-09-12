// shared/run_times -- the .times file beside a map: one appended line per
// completed run, sorted by whoever reads it.

#include "shared/run_times.hpp"

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <string>

using namespace shared;

namespace
{

void test_path_is_beside_the_map()
{
  assert(run_times_path_for("maps/bunnyhop.source") == "maps/bunnyhop.times");
  assert(run_times_path_for("maps/nested/dir/x.source") == "maps/nested/dir/x.times");
  std::printf("  path beside the map: ok\n");
}

void test_line_round_trip_keeps_spaces_in_the_name()
{
  run_time_record_t record;
  record.ticks       = 748;
  record.tickrate_hz = 60;
  record.date        = "2026-09-12T14:03";
  record.name        = "sjors the second";

  const std::string line = run_time_line(record);
  assert(line == "748 60 2026-09-12T14:03 sjors the second");

  const std::optional<run_time_record_t> parsed = try_parse_run_time_line(line);
  assert(parsed);
  assert(parsed->ticks == 748);
  assert(parsed->tickrate_hz == 60);
  assert(parsed->date == "2026-09-12T14:03");
  assert(parsed->name == "sjors the second");

  const std::optional<run_time_record_t> crlf = try_parse_run_time_line(line + "\r");
  assert(crlf && crlf->name == "sjors the second");
  std::printf("  line round trip: ok\n");
}

void test_bad_lines_are_skipped_and_the_rest_count()
{
  const std::vector<run_time_record_t> records = parse_run_times(
      "748 60 2026-09-12T14:03 sjors\n"
      "not a number 60 2026-09-12T14:03 sjors\n"
      "\n"
      "791 0 2026-09-12T13:58 zero tickrate\n"
      "1204 60 2026-09-11T22:10 sjors\n");
  assert(records.size() == 2);
  assert(records[0].ticks == 748);
  assert(records[1].ticks == 1204);
  std::printf("  bad lines skipped: ok\n");
}

void test_best_is_fastest_first_across_tickrates()
{
  std::vector<run_time_record_t> records;
  records.push_back({.ticks = 1204, .tickrate_hz = 60, .date = "d", .name = "slow"});
  records.push_back({.ticks = 748, .tickrate_hz = 60, .date = "d", .name = "fast"});
  records.push_back({.ticks = 1500, .tickrate_hz = 120, .date = "d", .name = "fast at 120"});
  records.push_back({.ticks = 748, .tickrate_hz = 60, .date = "d", .name = "fast, later"});

  const std::vector<run_time_record_t> best = best_run_times(records, 3);
  assert(best.size() == 3);
  assert(best[0].name == "fast");
  assert(best[1].name == "fast, later");
  assert(best[2].name == "fast at 120");
  std::printf("  best sorted by seconds, ties in file order: ok\n");
}

void test_format_run_time()
{
  assert(format_run_time(0.0f) == "00:00.00");
  assert(format_run_time(12.25f) == "00:12.25");
  assert(format_run_time(83.5f) == "01:23.50");
  assert(format_run_time(-1.0f) == "00:00.00");
  std::printf("  format: ok\n");
}

void test_append_then_read_back()
{
  const std::string path = "cmake_build/run_times_test_fixture.times";
  std::filesystem::remove(path);

  assert(read_run_times(path).empty());

  append_run_time(path, {.ticks = 900, .tickrate_hz = 60, .date = "2026-09-12T14:03", .name = "a"});
  append_run_time(path, {.ticks = 600, .tickrate_hz = 60, .date = "2026-09-12T14:05", .name = "b"});

  const std::vector<run_time_record_t> records = read_run_times(path);
  assert(records.size() == 2);
  assert(records[0].ticks == 900 && records[0].name == "a");
  assert(records[1].ticks == 600 && records[1].name == "b");

  std::filesystem::remove(path);
  std::printf("  append then read back: ok\n");
}

} // namespace

int main()
{
  std::printf("[run_times]\n");
  test_path_is_beside_the_map();
  test_line_round_trip_keeps_spaces_in_the_name();
  test_bad_lines_are_skipped_and_the_rest_count();
  test_best_is_fastest_first_across_tickrates();
  test_format_run_time();
  test_append_then_read_back();
  std::printf("run_times_test: all passed\n");
  return 0;
}
