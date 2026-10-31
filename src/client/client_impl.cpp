#include "../shared/assets/asset.hpp"
#include "../shared/frame_timing.hpp"
#include "../shared/memory_audit.hpp"
#include "../shared/entity_system.hpp"
#include "../shared/network/cvar_mirror.hpp"
#include "audio/audio_system.hpp"
#include "client_api.hpp"
#include "console.hpp"
#include "imgui.h"
#include "hud.hpp"
#include "renderer.hpp"
#include "state_manager.hpp"
#include "ui/font.hpp"

#include <cmath>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <string_view>
#include <memory>
#include <vector>

#include <chrono>
#include <iostream>
#include <string>

#include <SDL.h>

#include "cvars/generated/cvars_generated.hpp"
#include "input.hpp"
#include "log.hpp"
#include "timed_function.hpp"

namespace client
{

static SDL_Window* g_window = nullptr;

namespace
{
struct window_placement_t
{
  int  x;
  int  y;
  int  width;
  int  height;
  bool maximized;
};

int resolve_window_measure(float value, int desktop_extent)
{
  return value <= 1.0f ? static_cast<int>(value * static_cast<float>(desktop_extent))
                       : static_cast<int>(value);
}

int resolve_window_size(float value, int desktop_extent)
{
  return value > 0.0f ? resolve_window_measure(value, desktop_extent) : desktop_extent;
}

window_placement_t compute_window_placement(const cvars::cvar_state_t& cvars)
{
  SDL_Rect desktop{};
  if (SDL_GetDisplayUsableBounds(0, &desktop) != 0)
    fatal_error("SDL_GetDisplayUsableBounds: {}", SDL_GetError());

  window_placement_t placement{SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, true};
  if (cvars.r_window_width > 0.0f || cvars.r_window_height > 0.0f)
  {
    placement.width     = resolve_window_size(cvars.r_window_width, desktop.w);
    placement.height    = resolve_window_size(cvars.r_window_height, desktop.h);
    placement.maximized = false;
  }
  if (cvars.r_window_x >= 0.0f)
    placement.x = desktop.x + resolve_window_measure(cvars.r_window_x, desktop.w);
  if (cvars.r_window_y >= 0.0f)
    placement.y = desktop.y + resolve_window_measure(cvars.r_window_y, desktop.h);
  return placement;
}

// look_file := { line }
// line      := cvar_name ' ' value '\n'


constexpr const char* LOOK_DIRECTORY = "looks";
constexpr const char* LOOK_EXTENSION = ".look";

char                     g_look_name[64] = "look";
std::vector<std::string> g_look_names;

struct look_row_t
{
  const char*    section;
  cvars::cvar_id id;
  float          minimum       = 0.0f;
  float          maximum       = 1.0f;
  uint32_t       channel_count = 1;
  const char*    colour_label  = nullptr;
};

constexpr look_row_t LOOK_ROWS[] = {
    {"Frame", cvars::cvar_id::r_exposure, 0.1f, 4.0f},
    {"Frame", cvars::cvar_id::r_ambient_floor, 0.0f, 0.5f},
    {"Frame", cvars::cvar_id::r_fxaa},
    {"Frame", cvars::cvar_id::r_fxaa_subpixel},
    {"Beam", cvars::cvar_id::r_beam},
    {"Beam", cvars::cvar_id::r_beam_alpha, 0.0f, 0.5f},
    {"Beam", cvars::cvar_id::r_beam_fill},
    {"Beam", cvars::cvar_id::r_beam_dot_spacing, 2.0f, 32.0f},
    {"Beam", cvars::cvar_id::r_beam_edge_pixels, 0.0f, 8.0f},
    {"Beam", cvars::cvar_id::r_shadow_volume},
    {"Beam", cvars::cvar_id::r_shadow_volume_alpha, 0.0f, 1.0f},
    {"Cel", cvars::cvar_id::r_cel},
    {"Cel", cvars::cvar_id::r_cel_terminator},
    {"Cel", cvars::cvar_id::r_cel_shadow_edge},
    {"Cel", cvars::cvar_id::r_cel_softness, 0.0f, 0.5f},
    {"Cel", cvars::cvar_id::r_cel_shadow_red, 0.0f, 1.0f, 3, "r_cel_shadow rgb"},
    {"Cel", cvars::cvar_id::r_cel_bands, 0.0f, 8.0f},
    {"Cel", cvars::cvar_id::r_cel_flat_albedo},
    {"Cel", cvars::cvar_id::r_cel_black, 0.0f, 0.5f},
    {"Fill", cvars::cvar_id::r_cel_halftone, 0.0f, 4.0f},
    {"Fill", cvars::cvar_id::r_cel_halftone_paper, 0.0f, 4.0f},
    {"Fill", cvars::cvar_id::r_cel_halftone_ink, 0.0f, 1.0f},
    {"Fill", cvars::cvar_id::r_cel_halftone_gamma, 0.5f, 3.0f},
    {"Fill", cvars::cvar_id::r_cel_fill},
    {"Fill", cvars::cvar_id::r_cel_fill_strength},
    {"Fill", cvars::cvar_id::r_cel_fill_spacing, 2.0f, 32.0f},
    {"Fill", cvars::cvar_id::r_cel_fill_edge, 0.0f, 4.0f},
    {"Fill", cvars::cvar_id::r_cel_fill_shadow_tone_dark, 0.0f, 0.75f},
    {"Fill", cvars::cvar_id::r_cel_fill_shadow_tone_light, 0.0f, 0.75f},
    {"Fill", cvars::cvar_id::r_cel_fill_ambient_dark, 0.0f, 0.5f},
    {"Fill", cvars::cvar_id::r_cel_fill_ambient_light, 0.0f, 2.0f},
    {"Fill", cvars::cvar_id::r_cel_fill_tone_lit, 0.0f, 0.75f},
    {"Fill", cvars::cvar_id::r_cel_fill_material},
    {"Fill", cvars::cvar_id::r_cel_hatch_width, 0.5f, 8.0f},
    {"Fill", cvars::cvar_id::r_cel_dither3d_size_variability},
    {"Fill", cvars::cvar_id::r_cel_dither3d_contrast, 0.0f, 2.0f},
    {"Fill", cvars::cvar_id::r_cel_dither3d_stretch_smoothness, 0.0f, 2.0f},
    {"Speckle", cvars::cvar_id::r_cel_speckle},
    {"Speckle", cvars::cvar_id::r_cel_speckle_spacing, 2.0f, 40.0f},
    {"Speckle", cvars::cvar_id::r_cel_speckle_density},
    {"Speckle", cvars::cvar_id::r_cel_speckle_radius, 0.0f, 0.5f},
    {"Pebble", cvars::cvar_id::r_cel_pebble},
    {"Pebble", cvars::cvar_id::r_cel_pebble_spacing, 4.0f, 256.0f},
    {"Pebble", cvars::cvar_id::r_cel_pebble_density},
    {"Pebble", cvars::cvar_id::r_cel_pebble_size, 0.0f, 0.5f},
    {"Pebble", cvars::cvar_id::r_cel_pebble_irregularity},
    {"Pebble", cvars::cvar_id::r_cel_pebble_width, 0.5f, 8.0f},
    {"Pattern preview", cvars::cvar_id::r_pattern_preview},
    {"Pattern preview", cvars::cvar_id::r_pattern_preview_spacing_along, 1.0f, 256.0f},
    {"Pattern preview", cvars::cvar_id::r_pattern_preview_spacing_across, 1.0f, 256.0f},
    {"Pattern preview", cvars::cvar_id::r_pattern_preview_angle, 0.0f, 180.0f},
    {"Pattern preview", cvars::cvar_id::r_pattern_preview_scroll, -128.0f, 128.0f},
    {"Pattern preview", cvars::cvar_id::r_pattern_preview_coverage},
    {"Pattern preview", cvars::cvar_id::r_pattern_preview_shape},
    {"Pattern preview", cvars::cvar_id::r_pattern_preview_strength},
    {"Pattern preview", cvars::cvar_id::r_pattern_preview_red, 0.0f, 1.0f, 3, "r_pattern_preview rgb"},
    {"Paint", cvars::cvar_id::r_paint},
    {"Paint", cvars::cvar_id::r_paint_stroke, 2.0f, 256.0f},
    {"Paint", cvars::cvar_id::r_paint_stretch, 1.0f, 8.0f},
    {"Paint", cvars::cvar_id::r_paint_angle, 0.0f, 180.0f},
    {"Paint", cvars::cvar_id::r_paint_warp, 0.0f, 2.0f},
    {"Ink", cvars::cvar_id::r_ink},
    {"Ink", cvars::cvar_id::r_ink_threshold, 0.0f, 20.0f},
    {"Ink", cvars::cvar_id::r_ink_crease_degrees, 1.0f, 90.0f},
    {"Ink", cvars::cvar_id::r_ink_width, 1.0f, 8.0f},
    {"Ink", cvars::cvar_id::r_ink_tint},
    {"Ink", cvars::cvar_id::r_ink_on_black},
    {"Ink", cvars::cvar_id::r_ink_wobble, 0.0f, 8.0f},
    {"Ink", cvars::cvar_id::r_ink_wobble_scale, 4.0f, 200.0f},
    {"Ink", cvars::cvar_id::r_ink_boil, 0.0f, 24.0f},
    {"Ink", cvars::cvar_id::r_ink_weight_near, 1.0f, 8.0f},
    {"Ink", cvars::cvar_id::r_ink_weight_distance, 16.0f, 2048.0f},
    {"Rim", cvars::cvar_id::r_rim},
    {"Rim", cvars::cvar_id::r_rim_width, 1.0f, 16.0f},
    {"Misprint", cvars::cvar_id::r_misprint, 0.0f, 8.0f},
    {"Misprint", cvars::cvar_id::r_misprint_distance, 64.0f, 8192.0f},
    {"Flashlight", cvars::cvar_id::r_flashlight_intensity, 0.0f, 200.0f},
    {"Flashlight", cvars::cvar_id::r_flashlight_red, 0.0f, 1.0f, 3, "r_flashlight rgb"},
    {"Flashlight", cvars::cvar_id::r_flashlight_inner, 0.0f, 0.99f},
};

static_assert(static_cast<uint32_t>(cvars::cvar_id::r_cel_shadow_green) ==
                      static_cast<uint32_t>(cvars::cvar_id::r_cel_shadow_red) + 1 &&
                  static_cast<uint32_t>(cvars::cvar_id::r_cel_shadow_blue) ==
                      static_cast<uint32_t>(cvars::cvar_id::r_cel_shadow_red) + 2,
              "The colour row reads r_cel_shadow_red, _green, _blue as three consecutive cvar ids.");
static_assert(static_cast<uint32_t>(cvars::cvar_id::r_pattern_preview_green) ==
                      static_cast<uint32_t>(cvars::cvar_id::r_pattern_preview_red) + 1 &&
                  static_cast<uint32_t>(cvars::cvar_id::r_pattern_preview_blue) ==
                      static_cast<uint32_t>(cvars::cvar_id::r_pattern_preview_red) + 2,
              "The colour row reads r_pattern_preview_red, _green, _blue as three consecutive cvar ids.");
static_assert(static_cast<uint32_t>(cvars::cvar_id::r_flashlight_green) ==
                      static_cast<uint32_t>(cvars::cvar_id::r_flashlight_red) + 1 &&
                  static_cast<uint32_t>(cvars::cvar_id::r_flashlight_blue) ==
                      static_cast<uint32_t>(cvars::cvar_id::r_flashlight_red) + 2,
              "The colour row reads r_flashlight_red, _green, _blue as three consecutive cvar ids.");

[[nodiscard]] cvars::cvar_id get_cvar_id_for_color_channel(const look_row_t& row, uint32_t channel)
{
  return static_cast<cvars::cvar_id>(static_cast<uint32_t>(row.id) + channel);
}

template <typename T> [[nodiscard]] T& get_cvar_value(cvars::cvar_state_t& state, cvars::cvar_id id)
{
  return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(&state) + cvars::cvar_info(id).offset);
}

void draw_colour_row(cvars::cvar_state_t& state, const look_row_t& row)
{
  float colour[3];
  for (uint32_t channel = 0; channel < 3; ++channel)
    colour[channel] = get_cvar_value<float>(state, get_cvar_id_for_color_channel(row, channel));
  if (ImGui::ColorEdit3(row.colour_label, colour, ImGuiColorEditFlags_Float))
    for (uint32_t channel = 0; channel < 3; ++channel)
      get_cvar_value<float>(state, get_cvar_id_for_color_channel(row, channel)) = colour[channel];
}

void draw_value_row(cvars::cvar_state_t& state, const look_row_t& row)
{
  const cvars::cvar_info_t& info = cvars::cvar_info(row.id);
  switch (info.type)
  {
  case cvars::CVAR_TYPE_BOOL:
    ImGui::Checkbox(info.name, &get_cvar_value<bool>(state, row.id));
    break;
  case cvars::CVAR_TYPE_F32:
    ImGui::SliderFloat(info.name, &get_cvar_value<float>(state, row.id), row.minimum, row.maximum, "%.3f");
    break;
  case cvars::CVAR_TYPE_I32:
    ImGui::SliderInt(info.name, &get_cvar_value<int32_t>(state, row.id), static_cast<int>(row.minimum),
                     static_cast<int>(row.maximum));
    break;
  case cvars::CVAR_TYPE_ENUM:
  {
    uint8_t&                       value = get_cvar_value<uint8_t>(state, row.id);
    const Span<const char* const>& names = info.enum_info->value_names;
    if (ImGui::BeginCombo(info.name, names[value]))
    {
      for (size_t index = 0; index < names.size(); ++index)
        if (ImGui::Selectable(names[index], index == value))
          value = static_cast<uint8_t>(index);
      ImGui::EndCombo();
    }
    break;
  }
  case cvars::CVAR_TYPE_U32:
  case cvars::CVAR_TYPE_STRING:
    fatal_error("look panel: '{}' is of a type the panel has no widget for", info.name);
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", info.description);
}

[[nodiscard]] std::string look_file_path(std::string_view name)
{
  return std::format("{}/{}{}", LOOK_DIRECTORY, name, LOOK_EXTENSION);
}

[[nodiscard]] std::vector<std::string> list_look_names()
{
  std::vector<std::string> names;
  std::error_code          error;
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(LOOK_DIRECTORY, error))
    if (entry.is_regular_file() && entry.path().extension() == LOOK_EXTENSION)
      names.push_back(entry.path().stem().string());
  std::sort(names.begin(), names.end());
  return names;
}

[[nodiscard]] bool is_look_cvar(cvars::cvar_id id)
{
  for (const look_row_t& row : LOOK_ROWS)
    for (uint32_t channel = 0; channel < row.channel_count; ++channel)
      if (get_cvar_id_for_color_channel(row, channel) == id)
        return true;
  return false;
}

[[nodiscard]] bool try_write_look_file(const cvars::cvar_state_t& state, const std::string& path)
{
  std::error_code error;
  std::filesystem::create_directories(LOOK_DIRECTORY, error);
  std::ofstream file(path, std::ios::trunc);
  if (!file.is_open())
  {
    log_error("look panel: could not open '{}' for writing", path);
    return false;
  }
  for (const look_row_t& row : LOOK_ROWS)
    for (uint32_t channel = 0; channel < row.channel_count; ++channel)
    {
      const cvars::cvar_id id = get_cvar_id_for_color_channel(row, channel);
      file << cvars::cvar_info(id).name << ' ' << *cvars::try_cvar_to_text(state, id) << '\n';
    }
  return true;
}

void revert_look_to_defaults(cvars::cvar_state_t& state)
{
  for (const look_row_t& row : LOOK_ROWS)
    for (uint32_t channel = 0; channel < row.channel_count; ++channel)
    {
      const cvars::cvar_id id = get_cvar_id_for_color_channel(row, channel);
      shared::revert_cvars_to_defaults(state, Span<const cvars::cvar_id>(&id, 1));
    }
}

[[nodiscard]] bool try_load_look_file(cvars::cvar_state_t& state, const std::string& path)
{
  std::ifstream file(path);
  if (!file.is_open())
  {
    log_error("look panel: could not open '{}' for reading", path);
    return false;
  }
  revert_look_to_defaults(state);
  std::string line;
  for (uint32_t line_number = 1; std::getline(file, line); ++line_number)
  {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (line.empty())
      continue;
    const size_t separator = line.find(' ');
    if (separator == std::string::npos)
    {
      log_error("look panel: {}:{}: '{}' is not a 'name value' line", path, line_number, line);
      continue;
    }
    const std::string_view               name  = std::string_view(line).substr(0, separator);
    const std::string_view               value = std::string_view(line).substr(separator + 1);
    const std::optional<cvars::cvar_id> id    = cvars::try_find_cvar(name);
    if (!id || !is_look_cvar(*id))
    {
      log_error("look panel: {}:{}: '{}' is not a look cvar, line skipped", path, line_number, name);
      continue;
    }
    if (!cvars::try_cvar_from_text(state, *id, value))
      log_error("look panel: {}:{}: '{}' is not a value for '{}'", path, line_number, value, name);
  }
  return true;
}

// Nothing when r_look_panel is off. Every control writes the cvar the console writes.
void draw_look_panel(cvars::cvar_state_t& state)
{
  if (!state.r_look_panel)
    return;

  ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Look##look_panel", &state.r_look_panel, ImGuiWindowFlags_NoNav))
  {
    ImGui::Checkbox("r_stylized", &state.r_stylized);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s\nNot exported: play turns it on and the editor turns it off on entry.",
                        cvars::cvar_info(cvars::cvar_id::r_stylized).description);

    ImGui::PushItemWidth(-ImGui::CalcTextSize("r_cel_dither3d_stretch_smoothness  ").x);
    std::string_view section;
    for (const look_row_t& row : LOOK_ROWS)
    {
      if (section != row.section)
      {
        section = row.section;
        ImGui::SeparatorText(row.section);
      }
      if (row.channel_count == 3)
        draw_colour_row(state, row);
      else
        draw_value_row(state, row);
    }
    ImGui::PopItemWidth();

    ImGui::SeparatorText("GPU ms per pass (last / median / p95)");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Timestamps read back when the frame's fence comes round; gpu_report "
                        "prints the full distribution, gpu_reset clears it");
    for (const renderer::gpu_pass_readout_t& readout : renderer::get_gpu_pass_readouts())
      ImGui::Text("%-24s %6.3f %6.3f %6.3f", readout.name, readout.last_milliseconds,
                  readout.median_milliseconds, readout.p95_milliseconds);

    ImGui::Separator();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputText("##look_name", g_look_name, sizeof(g_look_name));
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("The look's name: Export writes %s/<name>%s", LOOK_DIRECTORY, LOOK_EXTENSION);
    ImGui::SameLine();
    ImGui::BeginDisabled(g_look_name[0] == '\0');
    if (ImGui::Button("Export"))
    {
      const std::string path = look_file_path(g_look_name);
      hud::set_announcement(try_write_look_file(state, path) ? std::format("Look written to {}", path)
                                                             : std::format("Could not write {}", path));
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Write every value above to the named file as 'name value' lines");
    ImGui::SameLine();
    if (ImGui::Button("Load"))
    {
      g_look_names = list_look_names();
      ImGui::OpenPopup("##load_look");
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Pick a file from %s/: defaults first, then every line in it", LOOK_DIRECTORY);
    if (ImGui::BeginPopup("##load_look"))
    {
      if (g_look_names.empty())
        ImGui::TextDisabled("(no %s files in %s/)", LOOK_EXTENSION, LOOK_DIRECTORY);
      for (const std::string& name : g_look_names)
        if (ImGui::Selectable(name.c_str()))
        {
          const std::string path = look_file_path(name);
          const bool        loaded = try_load_look_file(state, path);
          if (loaded)
            std::snprintf(g_look_name, sizeof(g_look_name), "%s", name.c_str());
          hud::set_announcement(loaded ? std::format("Look loaded from {}", path)
                                       : std::format("Could not read {}", path));
        }
      ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Defaults"))
      revert_look_to_defaults(state);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Put every value above back to its cvars.def default");
  }
  ImGui::End();
}

} // namespace
static std::chrono::high_resolution_clock::time_point g_last_tick_time;
static bool g_tick_time_initialized = false;

static std::unique_ptr<ui::ui_font_t> g_ui_font;

void set_asset_state(assets::asset_state_t *asset_state)
{
  assets::set_state(asset_state);
}

void install_memory_audit(memory_audit::memory_audit_state_t *state)
{
  memory_audit::set_state(state);
}

void install_frame_timing(frame_timing::frame_timing_state_t *state)
{
  frame_timing::set_state(state);
}

bool init(cvars::cvar_state_t *cvar_state, cvars::command_table_t *command_table,
          assets::asset_state_t *asset_state)
{
  timed_function();
  log_terminal("--- Initializing Client (SDL + Vulkan) ---");

  if (!cvar_state || !command_table || !asset_state)
  {
    log_error("client::Init: the launcher must own and pass a cvar_state_t, a "
              "command_table_t and an asset_state_t (see cvar_def.md and the "
              "ownership note in asset.hpp)");
    return false;
  }

  // Before the renderer resolves a single mesh. This DLL's copy of the asset
  // accessors' state pointer is null until now -- that is precisely the bug
  // that made every get_mesh here return an invalid handle.
  assets::set_state(asset_state);

  // Before anything that could read a cvar or run a console line. The bind call
  // fills this DLL's @Client handler slots; the linker already proved every
  // symbol it references exists, so there is nothing to verify at runtime.
  state_manager::get_client_context().cvars    = cvar_state;
  state_manager::get_client_context().commands = command_table;
  cvars::bind_client_commands(*command_table);
  console::get().set_cvar_state(cvar_state, command_table);

  // After the handler slots are filled, since these are console lines.
  console::get().load_bindings_from_file();

  // Before SDL_Init, because SDL declares process DPI awareness during video
  // init and Windows refuses the change afterwards. Without it the process is
  // DPI-UNAWARE: Windows hands us a virtualized desktop (2560x1440 on a 4K
  // panel at 150%), we render every pixel at that size, and DWM bilinearly
  // upscales the finished frame to 3840x2160. Every glyph is resampled, which
  // no amount of care in the rasterizer or the blend can undo.
  SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
  {
    log_error("SDL_Init Error: {}", SDL_GetError());
    return false;
  }

  // The raw-input thread owns its own message-only window, so it needs no
  // handle from ours and can come up before the window does. Failure is not
  // fatal -- see input::init.
  input::init();

  // MAXIMIZED by default, not borderless-fullscreen: the title bar and the
  // taskbar stay, which is what you want while a console and an editor are open
  // beside the game. 1280x720 is then the RESTORE size. The r_window_* cvars
  // (the launch line's -w -h -x -y) override both. The swapchain never reads
  // these numbers: renderer::init asks SDL_Vulkan_GetDrawableSize for the extent.
  const window_placement_t placement = compute_window_placement(*cvar_state);
  g_window =
      SDL_CreateWindow("MVMT Client", placement.x, placement.y, placement.width,
                       placement.height,
                       SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE |
                           SDL_WINDOW_ALLOW_HIGHDPI |
                           (placement.maximized ? SDL_WINDOW_MAXIMIZED : 0));

  if (!g_window)
  {
    log_error("SDL_CreateWindow Error: {}", SDL_GetError());
    return false;
  }

  if (!renderer::init(g_window))
  {
    log_error("Renderer Init Failed");
    return false;
  }

  // load a font.
  {
    // BAKED at the real pixel height, never scaled at draw time: a raster atlas
    // resampled to fit a denser display is the same blur the DPI fix removed.
    // The three logical sizes stay the design; the scale is applied here.
    const float scale = renderer::display_scale();

    const std::optional<ui::font_atlas_t> atlas = ui::try_bake_font(
        assets::read_asset_bytes(ui::DEFAULT_FONT_PATH),
        {std::floor(18.f * scale), std::floor(28.f * scale), std::floor(48.f * scale)});
    if (!atlas)
      fatal_error("client::init: could not bake the UI font from '{}'", ui::DEFAULT_FONT_PATH);

    g_ui_font = std::make_unique<ui::ui_font_t>();
    g_ui_font->atlas = renderer::register_texture(atlas->image, /*srgb*/ false);
    g_ui_font->sizes = atlas->sizes;
    state_manager::get_client_context().font = g_ui_font.get();
  }

  // Set initial state
  state_manager::init();
  state_manager::switch_to(game_state::main_menu);

  // A failed audio init is non-fatal: the system stays inert and every play_*
  // is a no-op, so a machine with no sound device still runs.
  state_manager::get_client_context().audio.init();

  return true;
}

bool Tick()
{
  FRAME_ZONE("client::Tick");

  client::input::new_frame();

  {
  FRAME_ZONE("SDL event pump");
  SDL_Event event;
  while (SDL_PollEvent(&event))
  {
    renderer::process_event(&event);
    input::process_sdl_event(&event);

    if (event.type == SDL_QUIT)
    {
      return false;
    }
    // Resize needs no handling here: new_frame() rebuilds the swapchain when
    // vkAcquireNextImageKHR reports it out of date, and skips that frame.
  }
  }

  // Compute real dt
  auto now = std::chrono::high_resolution_clock::now();
  if (!g_tick_time_initialized)
  {
    g_last_tick_time = now;
    g_tick_time_initialized = true;
  }
  float dt = static_cast<float>(
      std::chrono::duration<double>(now - g_last_tick_time).count());
  g_last_tick_time = now;
  if (dt > 0.25f)
    dt = 0.25f;
  if (dt < 0.0001f)
    dt = 0.0001f;

  // Apply time scale. Same cvar_state_t the integrated launcher scales the
  // SERVER accumulator from, so slow-mo now slows simulation and rendering
  // together instead of only the half that happened to link this DLL's copy.
  float timescale = state_manager::get_client_context().cvars->cl_timescale;
  if (timescale < 0.01f)
    timescale = 0.01f;
  dt *= timescale;

  // Update state
  {
    FRAME_ZONE("state_manager::update");
    if (!state_manager::update(dt))
    {
      return false;
    }
  }

  // Render. The two-phase shape is ImGui's doing and nothing else's: the UI is
  // built imperatively between the calls. Everything the 3D scene needs is a
  // value collected in draw, and render_frame runs EXACTLY ONCE.
  {
    // Where a GPU stall shows up: this acquires a swapchain image and waits on
    // the in-flight fence, and it is what rebuilds the swapchain on a resize.
    FRAME_ZONE("renderer::new_frame (acquire + fence)");
    if (!renderer::new_frame())
    {
      return true; // minimized, or the swapchain is being rebuilt
    }
  }

  // Ordered the way the frame composites, bottom to top: the UI list first, the
  // ImGui panels over it.
  //
  // Retained across frames so the per-frame pass list and UI vertices cost no
  // allocation after the first few frames.
  static std::vector<renderer::view_pass_t> frame_passes;
  static renderer::ui_draw_list_t           frame_ui;
  frame_passes.clear();
  frame_ui.clear();
  {
    FRAME_ZONE("state_manager::draw");
    state_manager::draw(dt, frame_passes, frame_ui);
  }

  // Appended after the state's own UI so a banner draws over it, and appended
  // HERE rather than by each state because set_announcement() is fire-and-forget
  // from thirty call sites that share no state.
  {
    FRAME_ZONE("announcement + imgui panels + console");
    hud::announcement_t &announcement = hud::current_announcement();
    hud::advance_announcement(announcement, dt);
    hud::draw_announcement(frame_ui, *g_ui_font, renderer::screen_size(),
                           renderer::display_scale(), announcement);

    state_manager::draw_imgui_panels();
    draw_look_panel(*state_manager::get_client_context().cvars);

    // Global console Overlay
    if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false))
    {
      client::console::get().toggle();
    }
    client::console::get().draw();
  }

  {
    FRAME_ZONE("renderer::render_frame (submit + present)");
    const client_context_t    &context = state_manager::get_client_context();
    const cvars::cvar_state_t &cvars   = *context.cvars;

    // While no cut is peeling, the frame is kept aside each frame so the next cut has the view it left; while
    // one is, the kept frame stays the one before the cut.
    const bool cut_wipe_on = cvars.r_cut_wipe_seconds > 0.0f;
    const bool cut_wiping  = cut_wipe_on && context.visuals.cut_wipe_seconds >= 0.0f;
    renderer::tonemap_settings_t tonemap;
    tonemap.exposure          = cvars.r_exposure;
    tonemap.keep_last_frame   = cut_wipe_on && !cut_wiping;
    tonemap.cut_wipe_fraction = cut_wiping ? context.visuals.cut_wipe_seconds / cvars.r_cut_wipe_seconds : -1.0f;
    tonemap.cut_wipe_distance = cvars.r_cut_wipe_distance;
    tonemap.lens_view_fraction = context.visuals.lens_view_fraction;
    tonemap.lens_view_color    = shared::get_color_for_lens_filter(context.visuals.lens_view_filter);
    renderer::look_settings_t look;
    look.ambient_floor    = cvars.r_ambient_floor;
    look.cel              = cvars.r_stylized && cvars.r_cel;
    look.cel_terminator   = cvars.r_cel_terminator;
    look.cel_shadow_edge  = cvars.r_cel_shadow_edge;
    look.cel_softness     = cvars.r_cel_softness;
    look.cel_shadow_tint  = {cvars.r_cel_shadow_red, cvars.r_cel_shadow_green,
                             cvars.r_cel_shadow_blue};
    look.cel_bands = cvars.r_cel_bands;
    look.cel_flat_albedo = cvars.r_cel_flat_albedo;
    look.cel_black       = cvars.r_cel_black;
    look.cel_halftone    = cvars.r_cel_halftone;
    look.cel_halftone_paper = cvars.r_cel_halftone_paper;
    look.cel_halftone_ink   = cvars.r_cel_halftone_ink;
    look.cel_halftone_gamma = cvars.r_cel_halftone_gamma;
    look.cel_fill                 = cvars.r_cel_fill;
    look.cel_fill_strength        = cvars.r_cel_fill_strength;
    look.cel_fill_spacing_pixels  = cvars.r_cel_fill_spacing;
    look.cel_fill_edge            = cvars.r_cel_fill_edge;
    look.cel_fill_shadow_tone_dark            = cvars.r_cel_fill_shadow_tone_dark;
    look.cel_fill_shadow_tone_light      = cvars.r_cel_fill_shadow_tone_light;
    look.cel_fill_ambient_dark    = cvars.r_cel_fill_ambient_dark;
    look.cel_fill_ambient_light   = cvars.r_cel_fill_ambient_light;
    look.cel_fill_tone_lit        = cvars.r_cel_fill_tone_lit;
    look.cel_fill_material        = cvars.r_cel_fill_material;
    look.cel_hatch_width_pixels   = cvars.r_cel_hatch_width;
    look.cel_dither3d_size_variability   = cvars.r_cel_dither3d_size_variability;
    look.cel_dither3d_contrast           = cvars.r_cel_dither3d_contrast;
    look.cel_dither3d_stretch_smoothness = cvars.r_cel_dither3d_stretch_smoothness;
    look.cel_speckle                = cvars.r_cel_speckle;
    look.cel_speckle_spacing_pixels = cvars.r_cel_speckle_spacing;
    look.cel_speckle_density        = cvars.r_cel_speckle_density;
    look.cel_speckle_radius         = cvars.r_cel_speckle_radius;
    look.cel_pebble                   = cvars.r_cel_pebble;
    look.cel_pebble_spacing           = cvars.r_cel_pebble_spacing;
    look.cel_pebble_density           = cvars.r_cel_pebble_density;
    look.cel_pebble_size              = cvars.r_cel_pebble_size;
    look.cel_pebble_irregularity      = cvars.r_cel_pebble_irregularity;
    look.cel_pebble_line_width_pixels = cvars.r_cel_pebble_width;
    look.pattern_preview               = cvars.r_pattern_preview;
    look.pattern_preview_spacing       = {cvars.r_pattern_preview_spacing_along,
                                          cvars.r_pattern_preview_spacing_across};
    look.pattern_preview_angle_degrees = cvars.r_pattern_preview_angle;
    look.pattern_preview_scroll_speed  = cvars.r_pattern_preview_scroll;
    look.pattern_preview_coverage      = cvars.r_pattern_preview_coverage;
    look.pattern_preview_shape         = cvars.r_pattern_preview_shape;
    look.pattern_preview_strength      = cvars.r_pattern_preview_strength;
    look.pattern_preview_ink           = {cvars.r_pattern_preview_red, cvars.r_pattern_preview_green,
                                          cvars.r_pattern_preview_blue};
    look.paint               = cvars.r_stylized ? cvars.r_paint : 0.0f;
    look.paint_stroke_width  = cvars.r_paint_stroke;
    look.paint_stretch       = cvars.r_paint_stretch;
    look.paint_angle_degrees = cvars.r_paint_angle;
    look.paint_warp          = cvars.r_paint_warp;
    look.ink              = cvars.r_stylized && cvars.r_ink;
    look.ink_threshold    = cvars.r_ink_threshold;
    look.ink_width_pixels = cvars.r_ink_width;
    look.ink_crease_degrees = cvars.r_ink_crease_degrees;
    look.ink_tint         = cvars.r_ink_tint;
    look.ink_wobble_pixels       = cvars.r_ink_wobble;
    look.ink_wobble_scale_pixels = cvars.r_ink_wobble_scale;
    look.ink_boil_per_second     = cvars.r_ink_boil;
    look.ink_weight_near         = cvars.r_ink_weight_near;
    look.ink_weight_distance     = cvars.r_ink_weight_distance;
    look.rim              = cvars.r_stylized ? cvars.r_rim : 0.0f;
    look.rim_width_pixels = cvars.r_rim_width;
    look.ink_on_black      = cvars.r_ink_on_black;
    look.beam_alpha              = cvars.r_beam_alpha;
    look.beam_fill               = cvars.r_beam_fill;
    look.beam_dot_spacing_pixels = cvars.r_beam_dot_spacing;
    look.beam_edge_pixels        = cvars.r_beam_edge_pixels;
    look.shadow_volume           = cvars.r_shadow_volume;
    look.shadow_volume_alpha     = cvars.r_shadow_volume_alpha;
    look.misprint_pixels   = cvars.r_stylized ? cvars.r_misprint : 0.0f;
    look.misprint_distance = cvars.r_misprint_distance;
    look.fog              = cvars.r_fog;
    look.fog_distance     = cvars.r_fog_distance;
    look.fog_anisotropy   = cvars.r_fog_anisotropy;

    const renderer::antialiasing_settings_t antialiasing{cvars.r_fxaa, cvars.r_fxaa_subpixel};

    renderer::shadow_settings_t shadows;
    shadows.map_size             = (uint32_t)std::max(cvars.r_shadow_map_size, 0);
    shadows.layer_count          = (uint32_t)std::max(cvars.r_shadow_layer_count, 0);
    shadows.light_offset_texels  = cvars.r_shadow_light_offset;
    shadows.bias_slope           = cvars.r_shadow_bias_slope;
    shadows.normal_offset_texels = cvars.r_shadow_normal_offset;
    shadows.pcf_radius           = cvars.r_shadow_pcf_radius;
    shadows.pcss                 = cvars.r_shadow_pcss && !look.cel;
    shadows.pcss_max_radius_texels = cvars.r_shadow_pcss_max_radius;
    shadows.debug_light_uid      = (shared::entity_uid_t)std::max(cvars.r_shadow_debug_light, 0);
    shadows.cascade_count         = (uint32_t)std::max(cvars.r_shadow_cascade_count, 1);
    shadows.cascade_lambda        = cvars.r_shadow_cascade_lambda;
    shadows.cascade_distance      = cvars.r_shadow_cascade_distance;
    shadows.cascade_blend         = cvars.r_shadow_cascade_blend;
    shadows.cascade_caster_extent = cvars.r_shadow_cascade_caster_extent;
    shadows.freeze_cascades       = cvars.r_shadow_freeze;
    renderer::render_frame(frame_passes, frame_ui, tonemap, look, antialiasing, shadows);
  }

  // WHAT WAS ON SCREEN, recorded at the moment it becomes true. A shot is
  // judged against the world the shooter was looking at, and this is the only
  // point in the program that knows a frame was actually handed over -- the
  // early return above means a minimized or rebuilding client presents nothing
  // and correctly records nothing.
  //
  // The stamp is the same clock the input edges carry, so the lookup that pairs
  // them needs no calibration between two clocks read at different points in
  // the frame. It is PRESENT time, not photon time: queued frames, the
  // compositor and the panel all sit past this line, and cl_display_latency_ms
  // is what the reader subtracts to cross them.
  {
    client_context_t &frame_context = state_manager::get_client_context();
    client::push_drawn_frame(frame_context.replication.drawn_history,
                             {input::arrival_clock_now(),
                              frame_context.replication.interpolation_cursor.tick,
                              frame_context.replication.interpolation_cursor.newest_received_tick});
  }

  return true;
}

void shutdown()
{
  timed_function();
  log_terminal("--- Shutting down Client ---");

  state_manager::shutdown();

  input::shutdown();

  // Before renderer::shutdown, which frees the atlas texture the font points at.
  state_manager::get_client_context().font = nullptr;
  g_ui_font.reset();

  renderer::shutdown();

  state_manager::get_client_context().audio.shutdown();

  if (g_window)
  {
    SDL_DestroyWindow(g_window);
  }
  SDL_Quit();
}

void set_integrated_server_session(const shared::game_session_t *session)
{
  state_manager::get_client_context().server_session = session;
}

static server_map_reload_hook_t g_server_map_reload_hook = nullptr;

void set_server_map_reload_hook(server_map_reload_hook_t hook)
{
  g_server_map_reload_hook = hook;
}

// Internal accessors for tool_editor_state.cpp / play_state.cpp.
bool server_map_reload_hook_is_installed()
{
  return g_server_map_reload_hook != nullptr;
}

bool invoke_server_map_reload_hook(const std::string &map_path)
{
  if (!g_server_map_reload_hook)
    return false;
  return g_server_map_reload_hook(map_path.c_str());
}

} // namespace client
