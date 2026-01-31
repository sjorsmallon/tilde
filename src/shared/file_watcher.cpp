#include "file_watcher.hpp"
#include "log.hpp"

namespace shared
{

void File_Watcher::add_file(const std::filesystem::path &path,
                            callback_t callback)
{
  std::error_code ec;
  if (!std::filesystem::exists(path, ec))
  {
    log_error("File_Watcher: File does not exist: {}", path.string());
    return;
  }

  auto abs_path = std::filesystem::absolute(path, ec);
  if (ec)
  {
    log_terminal("File_Watcher: Could not get absolute path for: {}",
                 path.string());
    abs_path = path;
  }

  std::string key = abs_path.string();
  auto it = watched_files.find(key);
  if (it == watched_files.end())
  {
    auto last_write_time = std::filesystem::last_write_time(abs_path, ec);
    if (ec)
    {
      log_error("File_Watcher: Could not get last write time for: {}",
                abs_path.string());
      return;
    }
    watched_files[key] = {last_write_time, {callback}};
  }
  else
  {
    it->second.callbacks.push_back(callback);
  }
}

void File_Watcher::update()
{
  for (auto &pair : watched_files)
  {
    std::filesystem::path path(pair.first);
    std::error_code ec;

    if (!std::filesystem::exists(path, ec))
    {
      // File might have been deleted? Just ignore or maybe warn once.
      continue;
    }

    auto current_time = std::filesystem::last_write_time(path, ec);
    if (ec)
    {
      continue;
    }

    if (current_time != pair.second.last_write_time)
    {
      pair.second.last_write_time = current_time;
      for (const auto &cb : pair.second.callbacks)
      {
        cb(path);
      }
    }
  }
}

} // namespace shared
