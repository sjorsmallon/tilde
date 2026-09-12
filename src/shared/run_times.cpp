#include "run_times.hpp"

#include "log.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>

namespace shared
{

std::string run_times_path_for(std::string_view map_path)
{
  return std::filesystem::path(map_path).replace_extension(".times").generic_string();
}

std::string run_time_line(const run_time_record_t& record)
{
  return std::format("{} {} {} {}", record.ticks, record.tickrate_hz, record.date, record.name);
}

namespace
{

std::optional<uint32_t> try_parse_u32_token(std::string_view& line)
{
  const size_t end = std::min(line.find(' '), line.size());
  uint32_t     value = 0;
  const std::from_chars_result result =
      std::from_chars(line.data(), line.data() + end, value);
  if (result.ec != std::errc{} || result.ptr != line.data() + end || end == 0)
    return std::nullopt;
  line.remove_prefix(std::min(end + 1, line.size()));
  return value;
}

std::string_view take_token(std::string_view& line)
{
  const size_t           end   = std::min(line.find(' '), line.size());
  const std::string_view token = line.substr(0, end);
  line.remove_prefix(std::min(end + 1, line.size()));
  return token;
}

} // namespace

std::optional<run_time_record_t> try_parse_run_time_line(std::string_view line)
{
  if (!line.empty() && line.back() == '\r')
    line.remove_suffix(1);

  run_time_record_t record;
  const std::optional<uint32_t> ticks = try_parse_u32_token(line);
  if (!ticks)
    return std::nullopt;
  const std::optional<uint32_t> tickrate = try_parse_u32_token(line);
  if (!tickrate || *tickrate == 0)
    return std::nullopt;
  const std::string_view date = take_token(line);
  if (date.empty())
    return std::nullopt;

  record.ticks       = *ticks;
  record.tickrate_hz = *tickrate;
  record.date        = std::string(date);
  record.name        = std::string(line);
  return record;
}

std::vector<run_time_record_t> parse_run_times(std::string_view text)
{
  std::vector<run_time_record_t> records;
  size_t                         line_start  = 0;
  size_t                         line_number = 0;
  while (line_start < text.size())
  {
    const size_t           line_end = std::min(text.find('\n', line_start), text.size());
    const std::string_view line     = text.substr(line_start, line_end - line_start);
    line_start                      = line_end + 1;
    ++line_number;

    if (line.empty() || line == "\r")
      continue;

    if (std::optional<run_time_record_t> record = try_parse_run_time_line(line))
      records.push_back(std::move(*record));
    else
      log_error("run times: line {} does not parse and is skipped: '{}'", line_number, line);
  }
  return records;
}

std::vector<run_time_record_t> read_run_times(const std::string& path)
{
  if (!std::filesystem::exists(path))
    return {};

  std::ifstream file(path, std::ios::binary);
  if (!file)
  {
    log_error("run times: could not open {}", path);
    return {};
  }
  std::stringstream text;
  text << file.rdbuf();
  return parse_run_times(text.str());
}

void append_run_time(const std::string& path, const run_time_record_t& record)
{
  std::ofstream file(path, std::ios::app);
  if (!file)
  {
    log_error("run times: could not open {} for append", path);
    return;
  }
  file << run_time_line(record) << '\n';
  if (!file)
    log_error("run times: write to {} failed", path);
}

std::vector<run_time_record_t> best_run_times(std::vector<run_time_record_t> records, size_t count)
{
  std::stable_sort(records.begin(), records.end(),
                   [](const run_time_record_t& a, const run_time_record_t& b) {
                     return run_time_seconds(a) < run_time_seconds(b);
                   });
  if (records.size() > count)
    records.resize(count);
  return records;
}

float run_time_seconds(const run_time_record_t& record)
{
  return static_cast<float>(record.ticks) / static_cast<float>(record.tickrate_hz);
}

std::string format_run_time(float seconds)
{
  const uint64_t total_centiseconds =
      static_cast<uint64_t>(std::floor(std::max(seconds, 0.0f) * 100.0f));
  const uint64_t minutes      = total_centiseconds / 6000;
  const uint64_t whole        = (total_centiseconds / 100) % 60;
  const uint64_t centiseconds = total_centiseconds % 100;
  return std::format("{:02}:{:02}.{:02}", minutes, whole, centiseconds);
}

std::string current_date_text()
{
  const std::time_t now = std::time(nullptr);
  std::tm           local{};
#ifdef _WIN32
  localtime_s(&local, &now);
#else
  localtime_r(&now, &local);
#endif
  char buffer[32] = {};
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M", &local);
  return buffer;
}

} // namespace shared
