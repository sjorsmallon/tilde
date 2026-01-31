#include "shared/file_watcher.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

int main()
{
  std::filesystem::path test_file = "test_watched_file.txt";

  // Create file
  {
    std::ofstream ofs(test_file);
    ofs << "Initial content";
  }

  shared::File_Watcher watcher;
  bool callback_called = false;

  watcher.add_file(test_file,
                   [&](const std::filesystem::path &path)
                   {
                     callback_called = true;
                     std::cout << "Callback invoked for: " << path << std::endl;
                   });

  // Initial update, should not trigger callback as file exists and we just
  // added it
  watcher.update();

  if (callback_called)
  {
    std::cerr << "Error: Callback called immediately after add_file"
              << std::endl;
    return 1;
  }

  // Sleep to ensure file modification time is different
  std::this_thread::sleep_for(std::chrono::seconds(2));

  // Modify file
  {
    std::ofstream ofs(test_file, std::ios::app);
    ofs << "\nNew content";
    std::cout << "Modified file: " << test_file << std::endl;
  }

  watcher.update();

  if (!callback_called)
  {
    std::cerr << "Error: Callback NOT called after file modification"
              << std::endl;
    return 1;
  }

  std::cout << "Success: File watcher detected change!" << std::endl;

  std::filesystem::remove(test_file);

  return 0;
}
