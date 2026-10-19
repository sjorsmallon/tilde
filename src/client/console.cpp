#include "console.hpp"
#include "../shared/collision/predicted_world.hpp"
#include "../shared/frame_timing.hpp"
#include "../shared/memory_audit.hpp"
#include "../shared/network/network_types.hpp"
#include "../shared/network/udp_socket.hpp"
#include "hud.hpp"
#include "input.hpp"
#include "key_names.hpp"
#include "log.hpp"
#include "renderer.hpp"
#include "state_manager.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>

namespace client
{

// Working directory, like last_map.txt.
static constexpr const char *BINDS_FILE_PATH = "binds.cfg";

console::console() = default;

console &console::get()
{
  static console instance;
  return instance;
}

void console::print_line(std::string line)
{
  items.push_back(std::move(line));
  scroll_to_bottom = true;
}

void console::set_cvar_state(cvars::cvar_state_t *state,
                             cvars::command_table_t *table)
{
  cvar_state = state;
  command_table = table;
}

bool console::bind_key(std::string_view key, std::string command_line)
{
  const std::optional<input::key_t> bound_key = input::try_key_from_name(key);
  if (!bound_key)
  {
    log_error("bind: no key is called '{}' (a-z, 0-9, f1-f12, space, lshift, "
              "uparrow, pgup, kp_0-kp_9, ...)", key);
    return false;
  }
  bindings[*bound_key] = std::move(command_line);
  save_bindings_to_file();
  return true;
}

// The whole file is rewritten sorted by key, so a removed bind cannot linger.
void console::save_bindings_to_file() const
{
  std::vector<std::pair<input::key_t, const std::string *>> sorted;
  sorted.reserve(bindings.size());
  for (const auto &[key, line] : bindings)
    sorted.push_back({key, &line});
  std::sort(sorted.begin(), sorted.end(),
            [](const auto &left, const auto &right) { return left.first < right.first; });

  std::ofstream file(BINDS_FILE_PATH, std::ios::trunc);
  if (!file.is_open())
  {
    log_error("bind: could not open '{}' for writing; this session's binds will "
              "not persist", BINDS_FILE_PATH);
    return;
  }
  for (const auto &[key, line] : sorted)
    file << "bind " << input::key_name(key) << ' ' << *line << '\n';
}

void console::load_bindings_from_file()
{
  auto lines = std::vector<std::string>{};
  {
    // Read to the end and close BEFORE executing: every bind rewrites the file.
    std::ifstream file(BINDS_FILE_PATH);

    if (!file.is_open()) return;

    auto line = std::string{};
    while (std::getline(file, line))
    {
      while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
        line.pop_back();
      if (!line.empty() && !line.starts_with("//"))
        lines.push_back(line);
    }
  }

  
  for (const std::string &line : lines)
  {
    execute_command(line.c_str());
  }
}

void console::execute_pressed_bindings()
{
  if (should_draw || input::imgui_wants_text_input()) return;

  for (const auto &[key, line] : bindings)
  {
    if (input::is_key_pressed(key)) execute_command(line.c_str());
  }
}

void console::execute_command(const char *command_line)
{
  print("# {}", command_line);

  history_position_cursor = -1;
  std::erase(history, command_line);
  history.push_back(command_line);

  if (!cvar_state || !command_table)
  {
    print("[error] console has no cvar state; client::init() has not run.");
    log_error("console::execute_command before set_cvar_state: '{}'", command_line);
    return;
  }

  auto reply = std::string{};
  const cvars::console_result_t result = cvars::execute_console_line(
      *cvar_state, *command_table, command_line, cvars::command_context_t{}, &reply);

  switch (result)
  {
    case cvars::console_result_t::empty:
    case cvars::console_result_t::forwarded:
      // A forwarded line is answered by S2C_ServerMessage; echoing here would double up.
      break;

    case cvars::console_result_t::ok:
    case cvars::console_result_t::unknown_name:
    case cvars::console_result_t::not_connected:
    case cvars::console_result_t::bad_arguments:
    case cvars::console_result_t::no_handler:
      if (!reply.empty())
        print_line(std::move(reply));
      break;
  }
}

int console::text_edit_callback_stub(ImGuiInputTextCallbackData *data)
{
  return console::get().text_edit_callback(data);
}

int console::text_edit_callback(ImGuiInputTextCallbackData *data)
{
  if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
  {
    const char *word_end = data->Buf + data->CursorPos;
    const char *word_start = word_end;
    while (word_start > data->Buf)
    {
      const char c = word_start[-1];
      if (c == ' ' || c == '\t' || c == ',' || c == ';')
        break;
      word_start--;
    }

    const std::string_view prefix(word_start, static_cast<size_t>(word_end - word_start));

    auto candidates = std::vector<std::string_view>{};
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
      print("No match for \"{}\"", prefix);
    }
    else if (candidates.size() == 1)
    {
      data->DeleteChars(static_cast<int>(word_start - data->Buf),
                        static_cast<int>(word_end - word_start));
      data->InsertChars(data->CursorPos, candidates[0].data(),
                        candidates[0].data() + candidates[0].size());
      data->InsertChars(data->CursorPos, " ");
    }
    else
    {
      size_t common_length = prefix.size();
      for (;;)
      {
        bool all_candidates_match = common_length < candidates[0].size();
        for (size_t i = 1; i < candidates.size() && all_candidates_match; i++)
        {
          all_candidates_match = common_length < candidates[i].size() &&
                                 candidates[i][common_length] == candidates[0][common_length];
        }
        if (!all_candidates_match)
          break;
        common_length++;
      }

      data->DeleteChars(static_cast<int>(word_start - data->Buf),
                        static_cast<int>(word_end - word_start));
      data->InsertChars(data->CursorPos, candidates[0].data(),
                        candidates[0].data() + common_length);

      print("Possible matches:");
      for (const std::string_view candidate : candidates)
        print("- {}", candidate);
    }
  }

  if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory)
  {
    const int history_count = static_cast<int>(history.size());
    const int previous_cursor = history_position_cursor;

    if (data->EventKey == ImGuiKey_UpArrow)
    {
      if (history_position_cursor == -1)
        history_position_cursor = history_count - 1;
      else if (history_position_cursor > 0)
        history_position_cursor--;
    }
    else if (data->EventKey == ImGuiKey_DownArrow)
    {
      if (history_position_cursor != -1)
        history_position_cursor = history_position_cursor == history_count - 1 ? -1 : history_position_cursor + 1;
    }

    if (history_position_cursor != previous_cursor)
    {
      data->DeleteChars(0, data->BufTextLen);
      if (history_position_cursor >= 0)
        data->InsertChars(0, history[history_position_cursor].c_str());
    }
  }

  return 0;
}

void console::draw()
{
  if (!should_draw) return;

  ImGui::SetNextWindowSize(ImVec2(520, 600), ImGuiCond_FirstUseEver);
  if (!ImGui::Begin("console", &should_draw))
  {
    ImGui::End();
    return;
  }

  const float footer_height_to_reserve =
      ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
  if (ImGui::BeginChild("ScrollingRegion", ImVec2(0, -footer_height_to_reserve),
                        false, ImGuiWindowFlags_HorizontalScrollbar))
  {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1));
    for (const std::string &item : items)
    {
      ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
      if (item.starts_with("[error]"))
        color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
      else if (item.starts_with("# "))
        color = ImVec4(1.0f, 0.8f, 0.6f, 1.0f);

      ImGui::PushStyleColor(ImGuiCol_Text, color);
      ImGui::TextUnformatted(item.c_str());
      ImGui::PopStyleColor();
    }
    ImGui::PopStyleVar();

    if (scroll_to_bottom || (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()))
      ImGui::SetScrollHereY(1.0f);
    scroll_to_bottom = false;
  }
  ImGui::EndChild();

  ImGui::Separator();

  bool reclaim_focus = false;
  const ImGuiInputTextFlags input_text_flags = ImGuiInputTextFlags_EnterReturnsTrue |
                                               ImGuiInputTextFlags_CallbackCompletion |
                                               ImGuiInputTextFlags_CallbackHistory;

  if (ImGui::IsWindowAppearing())
    ImGui::SetKeyboardFocusHere();

  if (ImGui::InputText("Input", input_buffer, IM_ARRAYSIZE(input_buffer),
                       input_text_flags, &text_edit_callback_stub, this))
  {
    char *s = input_buffer;
    while (*s && isspace(static_cast<unsigned char>(*s)))
      s++;
    if (*s)
      execute_command(s);

    memset(input_buffer, 0, sizeof(input_buffer));
    reclaim_focus = true;
  }

  if (reclaim_focus || ImGui::IsItemHovered() ||
      (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
       !ImGui::IsAnyItemActive() && !ImGui::IsMouseClicked(0)))
    ImGui::SetKeyboardFocusHere(-1);

  ImGui::End();
}

} // namespace client

// Each handler below is declared @Client in cvars.def; the generated binder takes its
// address, so a rename or signature drift is a link error.
namespace cvars::commands
{

void bind(std::string_view key, std::string_view command,
          const command_context_t &)
{
  auto command_line = std::string(command);

  if (client::console::get().bind_key(key, command_line))
  {
    client::console::get().print("bound '{}' to: {}", key, command_line);
  }
}

void connect(std::string_view address, const command_context_t &)
{
  auto server_address = network::Address{};
  auto parse_error = std::string{};
  if (!network::Address::parse_endpoint(std::string(address),
                                        network::server_port_number,
                                        server_address, parse_error))
  {
    client::console::get().print("connect: {}", parse_error);
    return;
  }

  client::state_manager::get_client_context().requested_server_address = server_address;
  client::console::get().print("connecting to {}...", server_address.to_string());
  client::state_manager::switch_to(client::game_state::play);
}

void noclip(const command_context_t &)
{
  cvars::cvar_state_t& cvars = *client::state_manager::get_client_context().cvars;
  cvars.cl_noclip = !cvars.cl_noclip;
  client::hud::set_announcement(cvars.cl_noclip ? "Noclip ON" : "Noclip OFF");
}

void grab_frame(const command_context_t &)
{
  client::renderer::request_frame_grab();
}

void grab_clear(const command_context_t &)
{
  client::renderer::clear_grabbed_frames();
}

void mem_report(int32_t top, const command_context_t &)
{
  memory_audit::report(top <= 0 ? 20u : static_cast<uint32_t>(top));
  client::console::get().print("mem_report: written to the terminal");
}

void mem_frame(const command_context_t &)
{
  memory_audit::report_frame();
  client::console::get().print("mem_frame: written to the terminal");
}

void mem_stacks(bool capture, const command_context_t &)
{
  memory_audit::set_capture_stacks(capture);
  client::console::get().print("mem_stacks: stack capture {}", capture ? "on" : "off");
}

void frame_report(const command_context_t &)
{
  frame_timing::report();
  client::console::get().print("frame_report: written to the terminal");
}

void shadow_volume_report(const command_context_t &)
{
  client::client_context_t& ctx = client::state_manager::get_client_context();
  if (!ctx.world.ready)
  {
    client::console::get().print("shadow_volume_report: no world");
    return;
  }
  shared::predicted_world_storage_t storage;
  shared::build_movers(ctx.world.session,
                       {.tick        = ctx.prediction.latest_server_tick,
                        .state_tick  = ctx.prediction.latest_server_tick,
                        .tickrate_hz = static_cast<float>(ctx.connection.server_tickrate),
                        .gravity     = ctx.cvars->g_gravity},
                       storage);
  const shared::shadow_volume_report_t counts = shared::build_shadow_volumes(ctx.world.session, storage);
  client::console::get().print_line(shared::describe_shadow_volume_report(counts, storage.shadow_volumes));
}

void hitch_report(int32_t top, const command_context_t &)
{
  // Zones first: they say WHERE the time went; the allocation sites say what was allocated.
  frame_timing::report_worst_frame_zones();
  memory_audit::report_captured_frame(top <= 0 ? 15u : static_cast<uint32_t>(top));
  client::console::get().print("hitch_report: written to the terminal");
}

void zone_report(const command_context_t &)
{
  frame_timing::report_zone_distribution();
  client::console::get().print("zone_report: written to the terminal");
}

void frame_reset(const command_context_t &)
{
  frame_timing::reset();
  client::console::get().print("frame_reset: distribution cleared");
}

void gpu_report(const command_context_t &)
{
  client::renderer::report_gpu_timing();
  client::console::get().print("gpu_report: written to the terminal");
}

void gpu_reset(const command_context_t &)
{
  client::renderer::reset_gpu_timing();
  client::console::get().print("gpu_reset: distribution cleared");
}

} // namespace cvars::commands
