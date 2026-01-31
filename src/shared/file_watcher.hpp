#pragma once

#include <filesystem>
#include <functional>
#include <unordered_map>
#include <vector>

namespace shared
{

class File_Watcher
{
public:
  using callback_t = std::function<void(const std::filesystem::path &)>;

  void add_file(const std::filesystem::path &path, callback_t callback);
  void update();

private:
  struct file_info_t
  {
    std::filesystem::file_time_type last_write_time;
    std::vector<callback_t> callbacks;
  };

  std::unordered_map<std::string, file_info_t> watched_files;
};

} // namespace shared
