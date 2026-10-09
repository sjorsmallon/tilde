#include "look_panel.hpp"

#include "../shared/network/cvar_mirror.hpp"
#include "cvars/generated/cvars_generated.hpp"
#include "hud/announcement.hpp"
#include "imgui.h"
#include "log.hpp"
#include "renderer.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

// look_file := { line }
// line      := cvar_name ' ' value '\n'

namespace client
{

namespace
{

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
    {"Beam", cvars::cvar_id::r_beam_surface_bias},
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

} // namespace

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

} // namespace client
