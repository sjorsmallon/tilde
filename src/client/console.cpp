#include "console.hpp"
#include "cvar.hpp"
#include "input.hpp"
#include "log.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <sstream>

namespace client
{

Console::Console()
{
  memset(InputBuf, 0, sizeof(InputBuf));
  HistoryPos = -1;
  ScrollToBottom = false;
  is_folded_open = true;
  should_draw = false;

  Print("Console Initialized.");
}

Console::~Console() = default;

Console &Console::Get()
{
  static Console instance;
  return instance;
}

void Console::Print(const char *fmt, ...)
{
  char buf[1024];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, IM_ARRAYSIZE(buf), fmt, args);
  buf[IM_ARRAYSIZE(buf) - 1] = 0;
  va_end(args);
  Items.push_back(strdup(buf));
  ScrollToBottom = true;
}

void Console::SetNetworkForwarder(std::function<void(std::string_view)> fn)
{
  network_forwarder_ = std::move(fn);
}

void Console::RegisterRemoteCVar(const std::string &name,
                                 const std::string &value, uint64_t flags,
                                 bool is_command,
                                 const std::string &description)
{
  auto &registry = cvar::CVarSystem::Get();
  if (auto *existing = registry.Find(name))
  {
    // Already known locally — keep the value in sync if it's a data cvar.
    // Commands have no value to mirror.
    if (!existing->IsCommand() && !is_command)
      existing->SetFromString(value);
    return;
  }

  if (is_command)
  {
    // Stub command: empty handler. ExecuteCommand sees the Server flag and
    // forwards the whole line to the network forwarder.
    remote_stubs_.push_back(std::make_unique<cvar::Console_Command>(
        name,
        [](Span<std::string_view>, const cvar::command_context_t &) {},
        description, flags));
  }
  else
  {
    // Stub data cvar: snapshot of server value as a string. Setting it
    // locally has no effect on the server; the value is just informational.
    remote_stubs_.push_back(
        std::make_unique<cvar::CVar<std::string>>(name, value, description, flags));
  }
}

bool Console::BindKey(std::string_view key, std::string command_line)
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

void Console::ClearBindings() { bindings_.clear(); }

void Console::PollBindings()
{
  if (should_draw)
    return; // never fire bindings while the console is open

  for (const auto &[key, line] : bindings_)
  {
    if (input::is_key_pressed(key))
      ExecuteCommand(line.c_str());
  }
}

// Register the `bind` command in the client's CVarSystem. Captures Console::Get()
// at invocation time (the singleton is local-static, lazily initialised).
static cvar::Console_Command cmd_bind(
    "bind",
    [](Span<std::string_view> args, const cvar::command_context_t &)
    {
      if (args.size() < 2)
      {
        Console::Get().Print("usage: bind <key> <command...>");
        return;
      }
      std::string command_line;
      for (size_t i = 1; i < args.size(); ++i)
      {
        if (i > 1)
          command_line.push_back(' ');
        command_line.append(args[i].begin(), args[i].end());
      }
      if (Console::Get().BindKey(args[0], command_line))
        Console::Get().Print("bound '%.*s' to: %s",
                             static_cast<int>(args[0].size()), args[0].data(),
                             command_line.c_str());
    },
    "Bind a key (a-z) to a command line. Usage: bind <key> <command...>",
    cvar::flags::Client);

void Console::ExecuteCommand(const char *command_line)
{
  Print("# %s", command_line);

  // Insert into history (if new)
  HistoryPos = -1;
  for (int i = (int)History.size() - 1; i >= 0; i--)
  {
    if (History[i] == command_line)
    {
      History.erase(History.begin() + i);
      break;
    }
  }
  History.push_back(command_line);

  // Parse command
  std::string line = command_line;
  std::stringstream ss(line);
  std::string cmd;
  ss >> cmd;

  if (cmd.empty())
    return;

  auto *obj = cvar::CVarSystem::Get().Find(cmd);
  if (obj)
  {
    if (obj->IsCommand())
    {
      // Server-flagged commands are forwarded over the network.
      if (obj->GetFlags() & cvar::flags::Server)
      {
        if (network_forwarder_)
          network_forwarder_(command_line);
        else
          Print("[error] Not connected to a server.");
      }
      else
      {
        // Local command — execute immediately via the unified dispatcher.
        cvar::CVarSystem::Get().Execute(command_line);
      }
    }
    else
    {
      // CVar: print current value or set it.
      size_t cmd_end = line.find(cmd) + cmd.length();
      while (cmd_end < line.length() && std::isspace(line[cmd_end]))
        cmd_end++;

      if (cmd_end < line.length())
      {
        std::string value_str = line.substr(cmd_end);
        obj->SetFromString(value_str);
        Print("Set %s to %s", cmd.c_str(), value_str.c_str());
      }
      else
      {
        std::string flags_str;
        uint64_t flags = obj->GetFlags();
        if (flags & cvar::flags::Admin)
          flags_str += "[ADMIN] ";
        if (flags & cvar::flags::Client)
          flags_str += "[CLIENT] ";
        if (flags & cvar::flags::Cheat)
          flags_str += "[CHEAT] ";

        Print("%s is %s %s", cmd.c_str(), obj->GetString().c_str(),
              flags_str.c_str());
        Print("  %s", obj->GetDescription().c_str());
      }
    }
    return;
  }

  // Command not found locally — try forwarding to the server.
  // Server-only commands (e.g. spawn_bot) live in the server DLL's CVarSystem
  // and aren't visible to the client, so we forward unknown commands rather
  // than requiring client-side stubs.
  if (network_forwarder_)
  {
    network_forwarder_(command_line);
  }
  else
  {
    Print("Unknown command: %s", cmd.c_str());
  }
}

int Console::TextEditCallbackStub(ImGuiInputTextCallbackData *data)
{
  return Console::Get().TextEditCallback(data);
}

int Console::TextEditCallback(ImGuiInputTextCallbackData *data)
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

    Candidates.clear();
    std::string prefix(word_start, word_end - word_start);

    cvar::CVarSystem::Get().VisitAll(
        [&](const std::string &name, cvar::Console_Entry_Base *)
        {
          if (name.compare(0, prefix.size(), prefix) == 0)
            Candidates.push_back(name);
        });

    if (Candidates.empty())
    {
      Print("No match for \"%.*s\"!", (int)(word_end - word_start), word_start);
    }
    else if (Candidates.size() == 1)
    {
      // Single match. Delete the beginning of the word and replace it entirely
      // so we've got nice casing
      data->DeleteChars((int)(word_start - data->Buf),
                        (int)(word_end - word_start));
      data->InsertChars(data->CursorPos, Candidates[0].c_str());
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
        for (int i = 0; i < Candidates.size() && all_candidates_matches; i++)
        {
          if (i == 0)
            c = toupper(Candidates[i][match_len]);
          else if (c == 0 || c != toupper(Candidates[i][match_len]))
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
        data->InsertChars(data->CursorPos, Candidates[0].c_str(),
                          Candidates[0].c_str() + match_len);
      }

      // List matches
      Print("Possible matches:");
      for (const auto &cand : Candidates)
        Print("- %s", cand.c_str());
    }
  }
  return 0;
}

void Console::Draw()
{
  if (!should_draw)
    return;

  ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("Console", &is_folded_open))
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
    for (const char *item : Items)
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

    if (ScrollToBottom || (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
      ImGui::SetScrollHereY(1.0f);
    ScrollToBottom = false;
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
      ExecuteCommand(s);

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
