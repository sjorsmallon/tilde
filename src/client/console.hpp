#pragma once

#include "../shared/cvars/cvar_console.hpp"
#include "input.hpp"

#include <functional>
#include <imgui.h>
#include <string>
#include <string_view>
#include <unordered_map>
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

  // Lend the console the launcher's cvar values and command table. Called once
  // from client::Init(); every line typed afterwards resolves against these.
  //
  // There is no RegisterRemoteCVar any more, and nothing left to register: the
  // client's CVAR_INFOS / COMMAND_INFOS tables are generated from the same
  // cvars.def as the server's, and the handshake refuses any client whose
  // SCHEMA_HASH differs — so autocomplete and forwarding both read a local
  // table that provably matches the server's. Forwarding to a server is a flag
  // check plus command_table_t::forward_to_server, which Play_State installs
  // when it connects.
  void SetCVarState(cvars::cvar_state_t *state, cvars::command_table_t *table);

  // Bind a single ASCII key (a-z) to a command line. The bound command is
  // executed via ExecuteCommand when the key transitions to pressed.
  bool BindKey(std::string_view key, std::string command_line);
  void ClearBindings();

  // Poll all bound keys and execute on rising edge. The caller is responsible
  // for skipping this while the console or any ImGui text input is focused.
  void PollBindings();

  bool IsOpen() const { return should_draw; }
  void Toggle() { should_draw = !should_draw; }
  void Close() { should_draw = false; }

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

  // Borrowed from the launcher via SetCVarState. Null before client::Init().
  cvars::cvar_state_t    *cvar_state_    = nullptr;
  cvars::command_table_t *command_table_ = nullptr;

  // Key bindings: Key -> command line.
  std::unordered_map<input::key_t, std::string> bindings_;

  // Commands
  std::vector<const char *> Commands;
};

} // namespace client
