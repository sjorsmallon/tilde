#include "connection_lines.hpp"

#include "../../shared/map.hpp"
#include "../../shared/map_connection.hpp"
#include "connection_panel.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <vector>

namespace client
{

namespace
{

constexpr float LINE_THICKNESS = 1.5f;

// Direction is the ARROWHEAD's job and colour is state's. Sharing colour
// between the two would leave nothing to say "this row is refused" in.
constexpr ImU32 RESTING_COLOR  = IM_COL32(150, 152, 160, 150);
constexpr ImU32 DIMMED_COLOR   = IM_COL32(140, 142, 150, 60);
constexpr ImU32 OUTBOUND_COLOR = IM_COL32(255, 176, 64, 225);
constexpr ImU32 INBOUND_COLOR  = IM_COL32(96, 208, 255, 225);
constexpr ImU32 REFUSED_COLOR  = IM_COL32(255, 88, 88, 235);
constexpr ImU32 HIGHLIGHT_COLOR = IM_COL32(255, 255, 255, 255);

constexpr float HIGHLIGHT_THICKNESS = 3.0f;

const ImVec4 REFUSED_TEXT_COLOR{1.0f, 0.35f, 0.35f, 1.0f};

constexpr float ARROW_SPACING_IN_PIXELS = 46.f;
constexpr float ARROW_SPEED_IN_PIXELS   = 30.f;
constexpr float ARROW_LENGTH            = 9.f;
constexpr float ARROW_HALF_WIDTH        = 4.5f;
constexpr float SHORTEST_ARROWED_LINE   = 14.f;

// A row whose target is a ROLE has no second endpoint. It still gets a mark, or
// a wired row would read in the viewport as a row that is not there.
constexpr float STUB_LENGTH_IN_PIXELS = 34.f;
constexpr float STUB_DIRECTION_X      = 0.74f;
constexpr float STUB_DIRECTION_Y      = -0.67f;

[[nodiscard]] std::optional<linalg::vec2> try_anchor_of(const shared::map_t&    map,
                                                        const viewport_state_t& view,
                                                        shared::entity_uid_t    uid)
{
  const shared::map_entity_t* entry = map.find_by_uid(uid);
  if (!entry || !entry->entity)
    return std::nullopt;

  return try_project_to_screen(view, entry->entity->position);
}

[[nodiscard]] bool selection_contains(Span<const shared::entity_uid_t> selection,
                                      shared::entity_uid_t             uid)
{
  for (shared::entity_uid_t selected : selection)
    if (selected == uid)
      return true;
  return false;
}

// Arrowheads march from sender toward target, spaced along the segment. The
// phase is wall time, so a still camera still says which way a row points.
void draw_marching_arrows(ImDrawList* draw_list, ImVec2 start, ImVec2 end, ImU32 color,
                          float time_seconds)
{
  const float dx     = end.x - start.x;
  const float dy     = end.y - start.y;
  const float length = std::sqrt(dx * dx + dy * dy);
  if (length < SHORTEST_ARROWED_LINE)
    return;

  const float direction_x = dx / length;
  const float direction_y = dy / length;

  const float phase =
      std::fmod(time_seconds * ARROW_SPEED_IN_PIXELS, ARROW_SPACING_IN_PIXELS);

  for (float travelled = phase; travelled < length; travelled += ARROW_SPACING_IN_PIXELS)
  {
    const float centre_x = start.x + direction_x * travelled;
    const float centre_y = start.y + direction_y * travelled;

    const ImVec2 tip{centre_x + direction_x * ARROW_LENGTH * 0.5f,
                     centre_y + direction_y * ARROW_LENGTH * 0.5f};
    const ImVec2 base{centre_x - direction_x * ARROW_LENGTH * 0.5f,
                      centre_y - direction_y * ARROW_LENGTH * 0.5f};

    draw_list->AddTriangleFilled(
        tip, ImVec2(base.x - direction_y * ARROW_HALF_WIDTH, base.y + direction_x * ARROW_HALF_WIDTH),
        ImVec2(base.x + direction_y * ARROW_HALF_WIDTH, base.y - direction_x * ARROW_HALF_WIDTH),
        color);
  }
}

} // namespace

const char* to_string(connection_line_mode_t mode)
{
  switch (mode)
  {
  case connection_line_mode_t::Off:       return "Off";
  case connection_line_mode_t::Selection: return "Selection";
  case connection_line_mode_t::All:       return "All";
  }
  return "Off";
}

void draw_connection_lines(const shared::map_t& map, const viewport_state_t& view,
                           connection_line_mode_t                  mode,
                           Span<const shared::entity_uid_t>         selection,
                           Span<const shared::connection_refusal_t> refusals,
                           size_t                                   highlighted_row,
                           float                                    time_seconds)
{
  if (map.connections.empty())
    return;
  if (mode == connection_line_mode_t::Off && highlighted_row >= map.connections.size())
    return;

  std::vector<bool> refused(map.connections.size(), false);
  for (const shared::connection_refusal_t& refusal : refusals)
    if (refusal.index < refused.size())
      refused[refusal.index] = true;

  ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

  for (size_t index = 0; index < map.connections.size(); ++index)
  {
    const shared::connection_t& row = map.connections[index];

    const bool sends_from_selection = selection_contains(selection, row.sender);
    const bool arrives_at_selection =
        row.target_kind == shared::connection_target_t::Uid &&
        selection_contains(selection, row.target);

    const bool is_highlighted = index == highlighted_row;

    if (!is_highlighted && mode == connection_line_mode_t::Off)
      continue;
    if (!is_highlighted && mode == connection_line_mode_t::Selection &&
        !sends_from_selection && !arrives_at_selection)
      continue;

    // Refusal outranks the selection colouring: a row that cannot run is the
    // one thing worth seeing from across the map.
    ImU32 color = RESTING_COLOR;
    if (is_highlighted)
      color = refused[index] ? REFUSED_COLOR : HIGHLIGHT_COLOR;
    else if (refused[index])
      color = REFUSED_COLOR;
    else if (sends_from_selection)
      color = OUTBOUND_COLOR;
    else if (arrives_at_selection)
      color = INBOUND_COLOR;
    else if (!selection.empty())
      color = DIMMED_COLOR;

    const std::optional<linalg::vec2> from = try_anchor_of(map, view, row.sender);
    if (!from)
      continue;

    if (row.target_kind != shared::connection_target_t::Uid)
    {
      const ImVec2 start{from->x, from->y};
      const ImVec2 end{from->x + STUB_DIRECTION_X * STUB_LENGTH_IN_PIXELS,
                       from->y + STUB_DIRECTION_Y * STUB_LENGTH_IN_PIXELS};

      const float thickness = is_highlighted ? HIGHLIGHT_THICKNESS : LINE_THICKNESS;
      draw_list->AddLine(start, end, color, thickness);
      draw_marching_arrows(draw_list, start, end, color, time_seconds);

      const std::string label = describe_connection_target(map, row);
      draw_list->AddText(ImVec2(end.x + 4.f, end.y - 7.f), color, label.c_str());
      continue;
    }

    const std::optional<linalg::vec2> to = try_anchor_of(map, view, row.target);
    if (!to)
      continue;

    const ImVec2 start{from->x, from->y};
    const ImVec2 end{to->x, to->y};

    draw_list->AddLine(start, end, color,
                       is_highlighted ? HIGHLIGHT_THICKNESS : LINE_THICKNESS);
    draw_marching_arrows(draw_list, start, end, color, time_seconds);
  }
}

std::optional<shared::entity_uid_t>
draw_connection_overview(const shared::map_t& map,
                         Span<const shared::connection_refusal_t> refusals,
                         connection_line_mode_t& mode, size_t& hovered_row)
{
  hovered_row = SIZE_MAX;

  ImGui::TextUnformatted("Show wiring");
  int selected_mode = (int)mode;
  ImGui::RadioButton("Off", &selected_mode, (int)connection_line_mode_t::Off);
  ImGui::SameLine();
  ImGui::RadioButton("Selection", &selected_mode, (int)connection_line_mode_t::Selection);
  ImGui::SameLine();
  ImGui::RadioButton("All", &selected_mode, (int)connection_line_mode_t::All);
  mode = (connection_line_mode_t)selected_mode;

  if (map.connections.empty())
  {
    ImGui::TextDisabled("No connections");
    return std::nullopt;
  }

  if (refusals.empty())
  {
    ImGui::Text("%zu connection%s", map.connections.size(),
                map.connections.size() == 1 ? "" : "s");
  }
  else
  {
    ImGui::Text("%zu connection%s,", map.connections.size(),
                map.connections.size() == 1 ? "" : "s");
    ImGui::SameLine();
    ImGui::TextColored(REFUSED_TEXT_COLOR, "%zu refused", refusals.size());
  }

  std::optional<shared::entity_uid_t> clicked_sender;

  // Height enough to read a handful of rows without the panel swallowing the
  // screen; the list scrolls past that.
  if (ImGui::BeginChild("##connection_list", ImVec2(0, 190.f), ImGuiChildFlags_Border))
  {
    for (size_t index = 0; index < map.connections.size(); ++index)
    {
      const shared::connection_t& row = map.connections[index];

      const bool is_refused =
          std::any_of(refusals.begin(), refusals.end(),
                      [&](const shared::connection_refusal_t& refusal)
                      { return refusal.index == index; });

      std::string label = std::format(
          "{}  {} -> {}  {}", shared::describe_map_entity(map, row.sender),
          entities::to_string(row.signal), describe_connection_target(map, row),
          entities::to_string(row.data.tag));

      if (row.delay_seconds > 0.0f && row.fire_once)
        label += std::format("  ({:.2f}s, once)", row.delay_seconds);
      else if (row.delay_seconds > 0.0f)
        label += std::format("  ({:.2f}s)", row.delay_seconds);
      else if (row.fire_once)
        label += "  (once)";

      ImGui::PushID((int)index);
      if (is_refused)
        ImGui::PushStyleColor(ImGuiCol_Text, REFUSED_TEXT_COLOR);

      if (ImGui::Selectable(label.c_str()))
        clicked_sender = row.sender;

      if (is_refused)
        ImGui::PopStyleColor();

      if (ImGui::IsItemHovered())
        hovered_row = index;

      ImGui::PopID();

      if (!is_refused)
        continue;

      ImGui::Indent();
      for (const shared::connection_refusal_t& refusal : refusals)
        if (refusal.index == index)
          ImGui::TextColored(REFUSED_TEXT_COLOR, "%s", refusal.reason.c_str());
      ImGui::Unindent();
    }
  }
  ImGui::EndChild();

  return clicked_sender;
}

} // namespace client
