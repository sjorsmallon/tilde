#include "console.hpp"
#include "cvar.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <sstream>

namespace client {

Console::Console() {
  memset(InputBuf, 0, sizeof(InputBuf));
  HistoryPos = -1;
  ScrollToBottom = false;
  Print("Console Initialized.");
}

Console &Console::Get() {
  static Console instance;
  return instance;
}

void Console::Print(const char *fmt, ...) {
  char buf[1024];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, IM_ARRAYSIZE(buf), fmt, args);
  buf[IM_ARRAYSIZE(buf) - 1] = 0;
  va_end(args);
  Items.push_back(strdup(buf));
  ScrollToBottom = true;
}

void Console::ExecuteCommand(const char *command_line) {
  Print("# %s", command_line);

  // Insert into history (if new)
  HistoryPos = -1;
  for (int i = (int)History.size() - 1; i >= 0; i--) {
    if (History[i] == command_line) {
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

  // Check CVar
  auto *cvar = cvar::CVarSystem::Get().Find(cmd);
  if (cvar) {
    std::string val;
    // Remaining part of string is value?
    // simple parsing: skip whitespace after cmd
    size_t cmd_end = line.find(cmd) + cmd.length();
    while (cmd_end < line.length() && std::isspace(line[cmd_end]))
      cmd_end++;

    if (cmd_end < line.length()) {
      std::string value_str = line.substr(cmd_end);
      cvar->SetFromString(value_str);
      Print("Set %s to %s", cmd.c_str(), value_str.c_str());
    } else {
      Print("%s is %s", cmd.c_str(), cvar->GetString().c_str());
      Print("  %s", cvar->GetDescription().c_str());
    }
    return;
  }

  Print("Unknown command: %s", cmd.c_str());
}

int Console::TextEditCallbackStub(ImGuiInputTextCallbackData *data) {
  return Console::Get().TextEditCallback(data);
}

int Console::TextEditCallback(ImGuiInputTextCallbackData *data) {
  if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion) {
    // Locate beginning of current word
    const char *word_end = data->Buf + data->CursorPos;
    const char *word_start = word_end;
    while (word_start > data->Buf) {
      const char c = word_start[-1];
      if (c == ' ' || c == '\t' || c == ',' || c == ';')
        break;
      word_start--;
    }

    Candidates.clear();
    std::string prefix(word_start, word_end - word_start);

    cvar::CVarSystem::Get().VisitAll(
        [&](const std::string &name, cvar::ICVar *) {
          if (name.compare(0, prefix.size(), prefix) == 0)
            Candidates.push_back(name);
        });

    if (Candidates.empty()) {
      Print("No match for \"%.*s\"!", (int)(word_end - word_start), word_start);
    } else if (Candidates.size() == 1) {
      // Single match. Delete the beginning of the word and replace it entirely
      // so we've got nice casing
      data->DeleteChars((int)(word_start - data->Buf),
                        (int)(word_end - word_start));
      data->InsertChars(data->CursorPos, Candidates[0].c_str());
      data->InsertChars(data->CursorPos, " ");
    } else {
      // Multiple matches. Complete as much as possible.
      int match_len = (int)prefix.size();
      for (;;) {
        int c = 0;
        bool all_candidates_matches = true;
        for (int i = 0; i < Candidates.size() && all_candidates_matches; i++) {
          if (i == 0)
            c = toupper(Candidates[i][match_len]);
          else if (c == 0 || c != toupper(Candidates[i][match_len]))
            all_candidates_matches = false;
        }
        if (!all_candidates_matches)
          break;
        match_len++;
      }

      if (match_len > 0) {
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

void Console::Draw(const char *title, bool *p_open) {
  ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin(title, p_open)) {
    ImGui::End();
    return;
  }

  // Reserve enough left-over height for 1 separator + 1 input text
  const float footer_height_to_reserve =
      ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
  if (ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve),
                        false, ImGuiWindowFlags_HorizontalScrollbar)) {
    // Display items
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(4, 1)); // Tighten spacing
    for (const char *item : Items) {
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
                       input_text_flags, &TextEditCallbackStub, (void *)this)) {
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
