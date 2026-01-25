#pragma once

#include <imgui.h>
#include <string>
#include <vector>

namespace client {

class Console {
public:
  static Console &Get();

  void Draw(const char *title, bool *p_open);
  void Print(const char *fmt, ...);
  void ExecuteCommand(const char *command_line);

private:
  Console();
  ~Console() = default;

  // Autocomplete
  int TextEditCallback(ImGuiInputTextCallbackData *data);
  static int TextEditCallbackStub(ImGuiInputTextCallbackData *data);

  char InputBuf[256];
  std::vector<char *> Items;
  bool ScrollToBottom;
  std::vector<std::string> Candidates;
  int HistoryPos; // -1: new line, 0..History.Size-1 browsing history.
  std::vector<std::string> History;

  // Commands
  std::vector<const char *> Commands;
};

} // namespace client
