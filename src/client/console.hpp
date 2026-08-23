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

class console
{
public:
  static console &get();

  void draw();
  void print(const char *fmt, ...);
  void execute_command(const char *command_line);

  // Lend the console the launcher's cvar values and command table. Called once
  // from client::init(); every line typed afterwards resolves against these.
  //
  // There is no RegisterRemoteCVar any more, and nothing left to register: the
  // client's CVAR_INFOS / COMMAND_INFOS tables are generated from the same
  // cvars.def as the server's, and the handshake refuses any client whose
  // SCHEMA_HASH differs — so autocomplete and forwarding both read a local
  // table that provably matches the server's. Forwarding to a server is a flag
  // check plus command_table_t::forward_to_server, which Play_State installs
  // when it connects.
  void set_cvar_state(cvars::cvar_state_t *state, cvars::command_table_t *table);

  // Bind a single ASCII key (a-z) to a command line. The bound command is
  // executed via execute_command when the key transitions to pressed.
  bool bind_key(std::string_view key, std::string command_line);
  void clear_bindings();

  // Self-gating: a no-op while the console is open, so callers need not check.
  void execute_pressed_bindings();

  bool is_open() const { return should_draw; }
  void toggle() { should_draw = !should_draw; }
  void close() { should_draw = false; }

private:
  console();
  ~console();

  bool should_draw;
  bool is_folded_open;

  // Autocomplete
  int TextEditCallback(ImGuiInputTextCallbackData *data);
  static int TextEditCallbackStub(ImGuiInputTextCallbackData *data);

  char InputBuf[256];
  std::vector<char *> items;
  bool scroll_to_bottom;
  std::vector<std::string> candidates;
  int history_position_cursor; // -1: new line, 0..history.Size-1 browsing history.
  std::vector<std::string> history;

  // Borrowed from the launcher via set_cvar_state. Null before client::init().
  cvars::cvar_state_t    *cvar_state_    = nullptr;
  cvars::command_table_t *command_table_ = nullptr;

  // Key bindings: Key -> command line.
  std::unordered_map<input::key_t, std::string> bindings_;

  // Commands
  std::vector<const char *> Commands;
};

} // namespace client
