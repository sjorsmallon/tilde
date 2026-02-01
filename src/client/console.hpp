#pragma once

#include <imgui.h>
#include <string>
#include <vector>

namespace client
{

class Console
{
public:
  static Console &Get();

  void Draw();
  void Print(const char *fmt, ...);
  void ExecuteCommand(const char *command_line);

  bool IsOpen() const { return should_draw; }
  void Toggle() { should_draw = !should_draw; }

private:
  Console();
  ~Console() = default;

  bool should_draw;
  bool is_folded_open;

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
