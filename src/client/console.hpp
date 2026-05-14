#pragma once

#include <functional>
#include <imgui.h>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cvar { struct Console_Entry_Base; }

namespace client
{

class Console
{
public:
  static Console &Get();

  void Draw();
  void Print(const char *fmt, ...);
  void ExecuteCommand(const char *command_line);

  // Set (or clear with nullptr) a callback that forwards command lines to the
  // server when the local registry has no matching entry.
  void SetNetworkForwarder(std::function<void(std::string_view)> fn);

  // Register a client-side stub for a server-declared cvar or command so it
  // shows up in autocomplete and (for Server-flagged commands) is forwarded
  // through the existing network path. No-op if name is already registered.
  void RegisterRemoteCVar(const std::string &name, const std::string &value,
                          uint64_t flags, bool is_command,
                          const std::string &description);

  // Bind a single ASCII key (a-z) to a command line. The bound command is
  // executed via ExecuteCommand when the key transitions to pressed.
  bool BindKey(std::string_view key, std::string command_line);
  void ClearBindings();

  // Poll all bound keys and execute on rising edge. The caller is responsible
  // for skipping this while the console or any ImGui text input is focused.
  void PollBindings();

  bool IsOpen() const { return should_draw; }
  void Toggle() { should_draw = !should_draw; }

private:
  Console();
  ~Console();

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

  std::function<void(std::string_view)> network_forwarder_;

  // Heap-owned stubs registered from server cvar sync. Each entry self-registers
  // with cvar::CVarSystem in its constructor and stays alive for the process.
  std::vector<std::unique_ptr<cvar::Console_Entry_Base>> remote_stubs_;

  // Key bindings: SDL scancode -> command line.
  std::unordered_map<int, std::string> bindings_;

  // Commands
  std::vector<const char *> Commands;
};

} // namespace client
