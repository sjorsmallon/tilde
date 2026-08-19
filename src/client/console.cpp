#include "console.hpp"
#include "cvars/cvar_console.hpp"
#include "input.hpp"
#include "log.hpp"
#include "state_manager.hpp"
#include "../shared/network/network_types.hpp"
#include "../shared/network/udp_socket.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <sstream>

namespace client
{

console::console()
{
  memset(InputBuf, 0, sizeof(InputBuf));
  history_position = -1;
  scroll_to_bottom = false;
  is_folded_open = true;
  should_draw = false;

  print("console Initialized.");
}

console::~console() = default;

console &console::get()
{
  static console instance;
  return instance;
}

void console::print(const char *fmt, ...)
{
  char buffer[1024];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, IM_ARRAYSIZE(buffer), fmt, args);
  buffer[IM_ARRAYSIZE(buffer) - 1] = 0;
  va_end(args);
  items.push_back(strdup(buffer));
  scroll_to_bottom = true;
}

void console::set_cvar_state(cvars::cvar_state_t *state,
                           cvars::command_table_t *table)
{
  cvar_state_    = state;
  command_table_ = table;
}

bool console::bind_key(std::string_view key, std::string command_line)
{
  if (key.size() != 1)
  {
    log_error("bind: only single ASCII keys (a-z) are supported, got '{}'",
              std::string(key));
    return false;
  }
  char c = key[0];
  if (c < 'a' || c > 'z')
  {
    log_error("bind: only lowercase a-z keys are supported, got '{}'", c);
    return false;
  }
  input::key_t bound_key = static_cast<input::key_t>(
      static_cast<int>(input::key_t::A) + (c - 'a'));
  bindings_[bound_key] = std::move(command_line);
  return true;
}

void console::clear_bindings() { bindings_.clear(); }

void console::execute_pressed_bindings()
{
  if (should_draw)
    return; // never fire bindings while the console is open

  for (const auto &[key, line] : bindings_)
  {
    if (input::is_key_pressed(key))
      execute_command(line.c_str());
  }
}

void console::execute_command(const char *command_line)
{
  print("# %s", command_line);

  // Insert into history (if new)
  history_position = -1;
  for (int i = (int)history.size() - 1; i >= 0; i--)
  {
    if (history[i] == command_line)
    {
      history.erase(history.begin() + i);
      break;
    }
  }
  history.push_back(command_line);

  if (!cvar_state_ || !command_table_)
  {
    // Only reachable if something drove the console before client::init(),
    // which would mean the init order broke.
    print("[error] console has no cvar state; client::init() has not run.");
    log_error("console::execute_command before set_cvar_state: '{}'", command_line);
    return;
  }

  // One dispatcher, shared with the server: a line typed here and the same
  // line arriving over the wire take identical paths. Ownership (run it here
  // vs. forward it) is decided inside, from the declared flags plus whether a
  // forwarder is installed.
  std::string reply;
  cvars::console_result_t result = cvars::execute_console_line(
      *cvar_state_, *command_table_, command_line, cvars::command_context_t{},
      &reply);

  switch (result)
  {
    case cvars::console_result_t::empty:
    case cvars::console_result_t::forwarded:
      // Forwarded lines are answered by the server's S2C_ServerMessage, which
      // print()s when it arrives — echoing anything here would double up.
      break;

    case cvars::console_result_t::ok:
    case cvars::console_result_t::unknown_name:
    case cvars::console_result_t::not_connected:
    case cvars::console_result_t::bad_arguments:
    case cvars::console_result_t::no_handler:
      if (!reply.empty())
        print("%s", reply.c_str());
      break;
  }
}

int console::TextEditCallbackStub(ImGuiInputTextCallbackData *data)
{
  return console::get().TextEditCallback(data);
}

int console::TextEditCallback(ImGuiInputTextCallbackData *data)
{
  if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
  {
    // Locate beginning of current word
    const char *word_end = data->Buf + data->CursorPos;
    const char *word_start = word_end;
    while (word_start > data->Buf)
    {
      const char c = word_start[-1];
      if (c == ' ' || c == '\t' || c == ',' || c == ';')
        break;
      word_start--;
    }

    candidates.clear();
    std::string prefix(word_start, word_end - word_start);

    // Straight off the generated tables — including every @Server name. The
    // client knows the server's whole cvar/command universe at compile time
    // (same cvars.def, hash-checked at connect), so autocomplete no longer
    // needs the server to sync stubs down first.
    for (const cvars::cvar_info_t &info : cvars::cvar_infos())
    {
      if (std::string_view(info.name).starts_with(prefix))
        candidates.push_back(info.name);
    }
    for (const cvars::command_info_t &info : cvars::command_infos())
    {
      if (std::string_view(info.name).starts_with(prefix))
        candidates.push_back(info.name);
    }
    std::sort(candidates.begin(), candidates.end());

    if (candidates.empty())
    {
      print("No match for \"%.*s\"!", (int)(word_end - word_start), word_start);
    }
    else if (candidates.size() == 1)
    {
      // Single match. Delete the beginning of the word and replace it entirely
      // so we've got nice casing
      data->DeleteChars((int)(word_start - data->Buf),
                        (int)(word_end - word_start));
      data->InsertChars(data->CursorPos, candidates[0].c_str());
      data->InsertChars(data->CursorPos, " ");
    }
    else
    {
      // Multiple matches. Complete as much as possible.
      int match_len = (int)prefix.size();
      for (;;)
      {
        int c = 0;
        bool all_candidates_matches = true;
        for (int i = 0; i < candidates.size() && all_candidates_matches; i++)
        {
          if (i == 0)
            c = toupper(candidates[i][match_len]);
          else if (c == 0 || c != toupper(candidates[i][match_len]))
            all_candidates_matches = false;
        }
        if (!all_candidates_matches)
          break;
        match_len++;
      }

      if (match_len > 0)
      {
        data->DeleteChars((int)(word_start - data->Buf),
                          (int)(word_end - word_start));
        data->InsertChars(data->CursorPos, candidates[0].c_str(),
                          candidates[0].c_str() + match_len);
      }

      // List matches
      print("Possible matches:");
      for (const auto &cand : candidates)
        print("- %s", cand.c_str());
    }
  }
  return 0;
}

void console::draw()
{
  if (!should_draw)
    return;

  ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("console", &is_folded_open))
  {
    ImGui::End();
    return;
  }

  // Reserve enough left-over height for 1 separator + 1 input text
  const float footer_height_to_reserve =
      ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
  if (ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve),
                        false, ImGuiWindowFlags_HorizontalScrollbar))
  {
    // Display items
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(4, 1)); // Tighten spacing
    for (const char *item : items)
    {
      ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
      if (strncmp(item, "[error]", 7) == 0)
        color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
      else if (strncmp(item, "# ", 2) == 0)
        color = ImVec4(1.0f, 0.8f, 0.6f, 1.0f);

      ImGui::PushStyleColor(ImGuiCol_Text, color);
      ImGui::TextUnformatted(item);
      ImGui::PopStyleColor();
    }
    ImGui::PopStyleVar();

    if (scroll_to_bottom || (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
      ImGui::SetScrollHereY(1.0f);
    scroll_to_bottom = false;
  }
  ImGui::EndChild();

  ImGui::Separator();

  // Command-line
  bool reclaim_focus = false;
  ImGuiInputTextFlags input_text_flags =
      ImGuiInputTextFlags_EnterReturnsTrue |
      ImGuiInputTextFlags_CallbackCompletion |
      ImGuiInputTextFlags_CallbackHistory;

  // Auto-focus on window apparition
  if (ImGui::IsWindowAppearing())
    ImGui::SetKeyboardFocusHere();

  if (ImGui::InputText("Input", InputBuf, IM_ARRAYSIZE(InputBuf),
                       input_text_flags, &TextEditCallbackStub, (void *)this))
  {
    char *s = InputBuf;
    // Skip leading whitespace & check empty
    while (*s && isspace(*s))
      s++;
    if (*s)
      execute_command(s);

    memset(InputBuf, 0, sizeof(InputBuf));
    reclaim_focus = true;
  }

  // Auto-keep focus
  if (reclaim_focus || ImGui::IsItemHovered() ||
      (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
       !ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked(0)))
    ImGui::SetKeyboardFocusHere(-1); // Auto focus input

  ImGui::End();
}

} // namespace client

// Declared `bind(key: string, command: string...)` @Client in cvars.def, which
// obligates game_client to define exactly this symbol with exactly the
// signature that parameter list implies — client_command_bindings.cpp (a
// generated TU compiled into this DLL) takes its address, so a rename or a
// signature drift here is a link error rather than a command that silently
// stops working. Argument count and the usage reply live in the generated
// binder, not here; `command` is the line's untokenized tail, interior
// whitespace intact (the old token re-join collapsed runs of spaces).
namespace cvars::commands
{

void bind(std::string_view key, std::string_view command,
          const command_context_t &)
{
  std::string command_line(command);
  if (client::console::get().bind_key(key, command_line))
    client::console::get().print("bound '%.*s' to: %s",
                                 static_cast<int>(key.size()), key.data(),
                                 command_line.c_str());
}

// `connect(address: string)` @Client. Same contract as bind() above.
void connect(std::string_view address, const command_context_t &)
{
  network::Address server_address;
  std::string parse_error;
  if (!network::Address::parse_endpoint(std::string(address),
                                        network::server_port_number,
                                        server_address, parse_error))
  {
    client::console::get().print("connect: %s", parse_error.c_str());
    return;
  }

  // Play_State::on_enter reads this and connects; see client_context.hpp.
  client::state_manager::get_client_context().requested_server_address =
      server_address;
  client::console::get().print("connecting to %s...",
                               server_address.to_string().c_str());
  client::state_manager::switch_to(client::game_state::play);
}

} // namespace cvars::commands
