#pragma once

#include "../shared/cvars/cvar_console.hpp"
#include "input.hpp"

#include <format>
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

  void print_line(std::string line);

  template <typename... Args>
  void print(std::format_string<Args...> format, Args &&...args)
  {
    print_line(std::format(format, std::forward<Args>(args)...));
  }

  void execute_command(const char *command_line);

  // Lends the launcher's values and the client's command table; null before client::init().
  void set_cvar_state(cvars::cvar_state_t *state, cvars::command_table_t *table);

  bool bind_key(std::string_view key, std::string command_line);

  // binds.cfg is a tiny autoexec: every line runs through execute_command once at init.
  void load_bindings_from_file();

  void execute_pressed_bindings();

  bool is_open() const { return should_draw; }
  void toggle() { should_draw = !should_draw; }
  void close() { should_draw = false; }

private:
  console();

  void save_bindings_to_file() const;

  int text_edit_callback(ImGuiInputTextCallbackData *data);
  static int text_edit_callback_stub(ImGuiInputTextCallbackData *data);

  bool should_draw = false;
  bool scroll_to_bottom = false;
  char input_buffer[256] = {};
  std::vector<std::string> items;
  int history_position_cursor = -1;
  std::vector<std::string> history;

  cvars::cvar_state_t *cvar_state = nullptr;
  cvars::command_table_t *command_table = nullptr;

  std::unordered_map<input::key_t, std::string> bindings;
};

} // namespace client
