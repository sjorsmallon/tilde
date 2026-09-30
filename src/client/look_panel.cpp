#include "look_panel.hpp"

#include "../shared/network/cvar_mirror.hpp"
#include "cvars/generated/cvars_generated.hpp"
#include "hud/announcement.hpp"
#include "imgui.h"
#include "log.hpp"

#include <format>
#include <fstream>
#include <string_view>

namespace client
{

namespace
{

constexpr const char* LOOK_FILE_PATH = "look.cfg";

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
    {"Frame", cvars::cvar_id::r_fxaa},
    {"Frame", cvars::cvar_id::r_fxaa_subpixel},
    {"Cel", cvars::cvar_id::r_cel},
    {"Cel", cvars::cvar_id::r_cel_terminator},
    {"Cel", cvars::cvar_id::r_cel_shadow_edge},
    {"Cel", cvars::cvar_id::r_cel_softness, 0.0f, 0.5f},
    {"Cel", cvars::cvar_id::r_cel_shadow_red, 0.0f, 1.0f, 3, "r_cel_shadow rgb"},
    {"Hatch", cvars::cvar_id::r_cel_hatch},
    {"Hatch", cvars::cvar_id::r_cel_hatch_spacing, 2.0f, 32.0f},
    {"Hatch", cvars::cvar_id::r_cel_hatch_width, 0.5f, 8.0f},
    {"Hatch", cvars::cvar_id::r_cel_hatch_edge, 0.0f, 4.0f},
    {"Speckle", cvars::cvar_id::r_cel_speckle},
    {"Speckle", cvars::cvar_id::r_cel_speckle_spacing, 2.0f, 40.0f},
    {"Speckle", cvars::cvar_id::r_cel_speckle_density},
    {"Speckle", cvars::cvar_id::r_cel_speckle_radius, 0.0f, 0.5f},
    {"Ink", cvars::cvar_id::r_ink},
    {"Ink", cvars::cvar_id::r_ink_threshold, 0.0f, 20.0f},
    {"Ink", cvars::cvar_id::r_ink_crease_degrees, 1.0f, 90.0f},
    {"Ink", cvars::cvar_id::r_ink_width, 1.0f, 8.0f},
};

static_assert(static_cast<uint32_t>(cvars::cvar_id::r_cel_shadow_green) ==
                      static_cast<uint32_t>(cvars::cvar_id::r_cel_shadow_red) + 1 &&
                  static_cast<uint32_t>(cvars::cvar_id::r_cel_shadow_blue) ==
                      static_cast<uint32_t>(cvars::cvar_id::r_cel_shadow_red) + 2,
              "The colour row reads r_cel_shadow_red, _green, _blue as three consecutive cvar ids.");

[[nodiscard]] cvars::cvar_id channel_of(const look_row_t& row, uint32_t channel)
{
  return static_cast<cvars::cvar_id>(static_cast<uint32_t>(row.id) + channel);
}

template <typename T> [[nodiscard]] T& value_of(cvars::cvar_state_t& state, cvars::cvar_id id)
{
  return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(&state) + cvars::cvar_info(id).offset);
}

void draw_colour_row(cvars::cvar_state_t& state, const look_row_t& row)
{
  float colour[3];
  for (uint32_t channel = 0; channel < 3; ++channel)
    colour[channel] = value_of<float>(state, channel_of(row, channel));
  if (ImGui::ColorEdit3(row.colour_label, colour, ImGuiColorEditFlags_Float))
    for (uint32_t channel = 0; channel < 3; ++channel)
      value_of<float>(state, channel_of(row, channel)) = colour[channel];
}

void draw_value_row(cvars::cvar_state_t& state, const look_row_t& row)
{
  const cvars::cvar_info_t& info = cvars::cvar_info(row.id);
  switch (info.type)
  {
  case cvars::CVAR_TYPE_BOOL:
    ImGui::Checkbox(info.name, &value_of<bool>(state, row.id));
    break;
  case cvars::CVAR_TYPE_F32:
    ImGui::SliderFloat(info.name, &value_of<float>(state, row.id), row.minimum, row.maximum, "%.3f");
    break;
  case cvars::CVAR_TYPE_I32:
    ImGui::SliderInt(info.name, &value_of<int32_t>(state, row.id), static_cast<int>(row.minimum),
                     static_cast<int>(row.maximum));
    break;
  case cvars::CVAR_TYPE_U32:
  case cvars::CVAR_TYPE_STRING:
  case cvars::CVAR_TYPE_ENUM:
    fatal_error("look panel: '{}' is of a type the panel has no widget for", info.name);
  }
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", info.description);
}

[[nodiscard]] bool try_write_look_file(const cvars::cvar_state_t& state)
{
  std::ofstream file(LOOK_FILE_PATH, std::ios::trunc);
  if (!file.is_open())
  {
    log_error("look panel: could not open '{}' for writing", LOOK_FILE_PATH);
    return false;
  }
  for (const look_row_t& row : LOOK_ROWS)
    for (uint32_t channel = 0; channel < row.channel_count; ++channel)
    {
      const cvars::cvar_id id = channel_of(row, channel);
      file << cvars::cvar_info(id).name << ' ' << *cvars::try_cvar_to_text(state, id) << '\n';
    }
  return true;
}

void revert_look_to_defaults(cvars::cvar_state_t& state)
{
  for (const look_row_t& row : LOOK_ROWS)
    for (uint32_t channel = 0; channel < row.channel_count; ++channel)
    {
      const cvars::cvar_id id = channel_of(row, channel);
      shared::revert_cvars_to_defaults(state, Span<const cvars::cvar_id>(&id, 1));
    }
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

    ImGui::PushItemWidth(-ImGui::CalcTextSize("r_ink_crease_degrees  ").x);
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

    ImGui::Separator();
    if (ImGui::Button("Export"))
      hud::set_announcement(try_write_look_file(state)
                                ? std::format("Look written to {}", LOOK_FILE_PATH)
                                : std::format("Could not write {}", LOOK_FILE_PATH));
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Write every value above to %s as console lines", LOOK_FILE_PATH);
    ImGui::SameLine();
    if (ImGui::Button("Defaults"))
      revert_look_to_defaults(state);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Put every value above back to its cvars.def default");
  }
  ImGui::End();
}

} // namespace client
