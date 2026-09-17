#include "replay_panel.hpp"

#include "../shared/asset.hpp"
#include "../shared/entities/generated/entities_generated.hpp"
#include "../shared/run_times.hpp"
#include "client_context.hpp"
#include "renderer.hpp"
#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>

namespace client
{

namespace
{

struct replay_speed_choice_t
{
  const char* label;
  float       factor;
};

constexpr replay_speed_choice_t REPLAY_SPEEDS[] = {
    {"0.1x", 0.1f}, {"0.25x", 0.25f}, {"0.5x", 0.5f}, {"1x", 1.0f}, {"2x", 2.0f}, {"4x", 4.0f},
};

// The art carries its own bevelled frame, so the button's frame is pulled in to
// a thin hover/press highlight around it rather than a second border.
constexpr float REPLAY_ICON_SIZE    = 80.0f;
constexpr float REPLAY_ICON_PADDING = 2.0f;

// The icon, or its label when the upload failed -- register_texture has already
// said why, and a panel that loses a control to a missing file is worse than an
// ugly one.
[[nodiscard]] bool icon_button(assets::texture_asset icon, const char* label, const char* tooltip)
{
  ImTextureID texture = (ImTextureID)renderer::imgui_texture_id(assets::get_texture(icon));
  const bool  pressed =
      texture ? ImGui::ImageButton(label, texture, ImVec2(REPLAY_ICON_SIZE, REPLAY_ICON_SIZE))
              : ImGui::Button(label, ImVec2(REPLAY_ICON_SIZE + 8.0f, REPLAY_ICON_SIZE + 8.0f));
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", tooltip);
  return pressed;
}

// The speed steps through the table rather than being typed: replay_speed takes
// any factor, and this is the row of the ones worth a click.
[[nodiscard]] float stepped_replay_speed(float current, int direction)
{
  const int count = static_cast<int>(std::size(REPLAY_SPEEDS));
  int       index = 0;
  float     best  = 0.0f;
  for (int i = 0; i < count; ++i)
  {
    const float distance = std::abs(REPLAY_SPEEDS[i].factor - current);
    if (i == 0 || distance < best)
    {
      best  = distance;
      index = i;
    }
  }
  return REPLAY_SPEEDS[std::clamp(index + direction, 0, count - 1)].factor;
}

[[nodiscard]] double seconds_of_tick(const replay_playback_t& playback, uint32_t tick)
{
  const uint32_t first = playback.replay.index.first_tick;
  const uint32_t rate  = playback.replay.header.tickrate_hz;
  if (rate == 0 || tick <= first)
    return 0.0;
  return static_cast<double>(tick - first) / rate;
}

// Where the slider sits: the jump that has been asked for but not yet serviced,
// else the clock. The same choice replay_skip makes.
[[nodiscard]] double scrub_position_of(const replay_playback_t& playback)
{
  if (playback.pending_seek_tick)
    return seconds_of_tick(playback, *playback.pending_seek_tick);
  return replay_seconds_elapsed(playback);
}

void draw_keyframe_marks(const replay_playback_t& playback, const ImVec2& rect_min, const ImVec2& rect_max)
{
  const shared::replay_index_t& index = playback.replay.index;
  if (index.last_tick <= index.first_tick)
    return;

  ImDrawList*  draw_list = ImGui::GetWindowDrawList();
  const float  span      = static_cast<float>(index.last_tick - index.first_tick);
  const float  width     = rect_max.x - rect_min.x;
  const ImU32  color     = ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.35f));
  for (const shared::replay_keyframe_t& keyframe : index.keyframes)
  {
    const float fraction = (static_cast<float>(keyframe.tick) - index.first_tick) / span;
    const float x        = rect_min.x + std::clamp(fraction, 0.0f, 1.0f) * width;
    draw_list->AddLine(ImVec2(x, rect_max.y - 4.0f), ImVec2(x, rect_max.y), color, 1.0f);
  }
}

struct replay_pov_row_t
{
  int32_t     slot        = -1;
  const char* name        = "";
  bool        has_view    = false;
};

[[nodiscard]] std::vector<replay_pov_row_t> collect_pov_rows(const client_context_t& context)
{
  std::vector<replay_pov_row_t> rows;
  for (const entities::Player_Entity& player :
       context.world.session.entity_system.entities_of<entities::Player_Entity>())
  {
    replay_pov_row_t row;
    row.slot     = player.client_slot_index;
    row.name     = player.display_name.c_str();
    row.has_view = shared::replay_has_view_track(context.replay.view_tracks, row.slot);
    rows.push_back(row);
  }
  std::sort(rows.begin(), rows.end(),
            [](const replay_pov_row_t& a, const replay_pov_row_t& b) { return a.slot < b.slot; });
  return rows;
}

void draw_pov_label(char* buffer, size_t capacity, const replay_pov_row_t& row)
{
  snprintf(buffer, capacity, "slot %d  %s%s", row.slot, row.name[0] != '\0' ? row.name : "(unnamed)",
           row.has_view ? "  [recorded view]" : "");
}

} // namespace

void draw_replay_panel(client_context_t& context)
{
  replay_playback_t& playback = context.replay;
  if (!playback.active || context.cvars == nullptr || !context.cvars->cl_replay_panel)
    return;

  const double total = replay_seconds_total(playback);

  ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Once);
  ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y - 20.0f),
                          ImGuiCond_Once, ImVec2(0.5f, 1.0f));
  ImGui::SetNextWindowBgAlpha(0.75f);
  if (ImGui::Begin("Replay##replay_panel", &context.cvars->cl_replay_panel,
                   ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
  {
    ImGui::Text("%s   recorded %s   %u Hz", playback.replay.header.map_name.c_str(),
                playback.replay.header.date.c_str(), playback.replay.header.tickrate_hz);

    const double position = scrub_position_of(playback);
    ImGui::Text("%s / %s   tick %u of %u..%u%s",
                shared::format_run_time(static_cast<float>(position)).c_str(),
                shared::format_run_time(static_cast<float>(total)).c_str(),
                static_cast<uint32_t>(playback.clock_tick), playback.replay.index.first_tick,
                playback.replay.index.last_tick, playback.reached_end ? "   END" : "");

    float scrub = static_cast<float>(position);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##replay_scrub", &scrub, 0.0f, static_cast<float>(total), ""))
      request_replay_seek(playback, scrub);
    draw_keyframe_marks(playback, ImGui::GetItemRectMin(), ImGui::GetItemRectMax());

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(REPLAY_ICON_PADDING, REPLAY_ICON_PADDING));
    if (icon_button(assets::texture_asset::replay_restart, "|<", "Back to the start"))
      request_replay_seek(playback, 0.0);
    ImGui::SameLine();
    if (icon_button(assets::texture_asset::replay_go_back, "<<", "Back 5 seconds"))
      request_replay_seek(playback, std::clamp(position - 5.0, 0.0, total));
    ImGui::SameLine();
    if (icon_button(playback.paused ? assets::texture_asset::replay_play
                                    : assets::texture_asset::replay_pause,
                    playback.paused ? ">" : "||", playback.paused ? "Play" : "Pause"))
      playback.paused = !playback.paused;
    ImGui::SameLine();
    if (icon_button(assets::texture_asset::replay_advance, ">>", "Forward 5 seconds"))
      request_replay_seek(playback, std::clamp(position + 5.0, 0.0, total));

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(12.0f, 0.0f));
    ImGui::SameLine();
    if (icon_button(assets::texture_asset::replay_speed_down, "-", "Slower"))
      playback.speed = stepped_replay_speed(playback.speed, -1);
    ImGui::SameLine();
    if (icon_button(assets::texture_asset::replay_speed_up, "+", "Faster"))
      playback.speed = stepped_replay_speed(playback.speed, 1);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%gx", playback.speed);
    ImGui::PopStyleVar();

    ImGui::Separator();

    // The camera's arms are noclip, then the spectated slot, then the predicted
    // eye -- and a replay has no local body, so that last one parks the view
    // wherever prediction left it. So a row here writes BOTH cvars: the free
    // camera IS cl_noclip, and picking a player has to clear it or the arm above
    // keeps winning and the pick does nothing.
    const std::vector<replay_pov_row_t> rows       = collect_pov_rows(context);
    const bool                          flying     = context.cvars->cl_noclip;
    char                                preview[96] = "Free camera";
    if (!flying)
      for (const replay_pov_row_t& row : rows)
        if (row.slot == context.cvars->cl_spectate_slot)
          draw_pov_label(preview, sizeof(preview), row);

    ImGui::SetNextItemWidth(260.0f);
    if (ImGui::BeginCombo("view", preview))
    {
      if (ImGui::Selectable("Free camera", flying))
      {
        context.cvars->cl_noclip        = true;
        context.cvars->cl_spectate_slot = -1;
      }
      for (const replay_pov_row_t& row : rows)
      {
        char label[96];
        draw_pov_label(label, sizeof(label), row);
        ImGui::PushID(row.slot);
        if (ImGui::Selectable(label, !flying && row.slot == context.cvars->cl_spectate_slot))
        {
          context.cvars->cl_noclip        = false;
          context.cvars->cl_spectate_slot = row.slot;
        }
        ImGui::PopID();
      }
      ImGui::EndCombo();
    }

    const bool spectating_a_recorded_slot =
        !flying && context.cvars->cl_spectate_slot >= 0 &&
        shared::replay_has_view_track(playback.view_tracks, context.cvars->cl_spectate_slot);

    ImGui::SameLine();
    ImGui::BeginDisabled(!spectating_a_recorded_slot);
    ImGui::Checkbox("their screen", &context.cvars->cl_replay_player_view);
    ImGui::EndDisabled();
    if (!spectating_a_recorded_slot && !flying && context.cvars->cl_spectate_slot >= 0)
    {
      ImGui::SameLine();
      ImGui::TextDisabled("(no recorded view)");
    }
  }
  ImGui::End();
}

} // namespace client
