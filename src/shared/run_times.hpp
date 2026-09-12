#pragma once

// A map's completed runs, one line each, appended and never rewritten:
//
//   maps/bunnyhop.times
//     748 60 2026-09-12T14:03 sjors
//     791 60 2026-09-12T13:58 sjors
//
//   line  ::= ticks SP tickrate_hz SP date SP name
//   ticks ::= decimal u32          the run's length, exact, never rounded
//   tickrate_hz ::= decimal u32    what a tick was worth when the run was made
//   date  ::= YYYY-MM-DDTHH:MM     local time, no spaces
//   name  ::= the rest of the line, spaces included
//
// Sorting is the reader's job: the server appends in complete_level and reads
// the file straight back to announce the top five.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace shared
{

struct run_time_record_t
{
  uint32_t    ticks       = 0;
  uint32_t    tickrate_hz = 0;
  std::string date;
  std::string name;
};

// maps/bunnyhop.source -> maps/bunnyhop.times
[[nodiscard]] std::string run_times_path_for(std::string_view map_path);

[[nodiscard]] std::string run_time_line(const run_time_record_t& record);
[[nodiscard]] std::optional<run_time_record_t> try_parse_run_time_line(std::string_view line);

// A line that does not parse is logged and skipped; the rest still count.
[[nodiscard]] std::vector<run_time_record_t> parse_run_times(std::string_view text);

// An absent file is a map nobody has finished yet, so it reads as empty.
[[nodiscard]] std::vector<run_time_record_t> read_run_times(const std::string& path);
void append_run_time(const std::string& path, const run_time_record_t& record);

// Fastest first, ties in file order, at most `count` of them.
[[nodiscard]] std::vector<run_time_record_t> best_run_times(std::vector<run_time_record_t> records,
                                                            size_t count);

[[nodiscard]] float run_time_seconds(const run_time_record_t& record);

// "mm:ss.cc"
[[nodiscard]] std::string format_run_time(float seconds);

[[nodiscard]] std::string current_date_text();

} // namespace shared
