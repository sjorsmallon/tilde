#include "editor_sidebar.hpp"

#include "../../shared/entities/generated/entities_generated.hpp"
#include "../../shared/map.hpp"
#include "editor_tool.hpp"
#include "editor_types.hpp"

#include <imgui.h>

#include <algorithm>
#include <format>
#include <optional>
#include <string>

namespace client
{

namespace
{

constexpr float MINIMUM_WIDTH        = 260.0f;
constexpr float SPLITTER_THICKNESS   = 6.0f;
constexpr float MINIMUM_PANE_HEIGHT  = 80.0f;
const ImU32     MULTI_EDIT_COLOR     = IM_COL32(255, 160, 0, 255);
const ImU32     MULTI_EDIT_TEXT      = IM_COL32(20, 20, 20, 255);
const ImVec4    MULTI_EDIT_TINT{1.0f, 0.63f, 0.0f, 0.07f};

// The one entity type every selected object has, if they are all entities of one.
std::optional<entities::entity_type> shared_entity_type(const shared::map_t&              map,
                                                        Span<const shared::entity_uid_t> selection)
{
  std::optional<entities::entity_type> type;
  for (shared::entity_uid_t uid : selection)
  {
    const shared::map_entity_t* entry = map.find_by_uid(uid);
    if (entry == nullptr || !entry->entity)
      return std::nullopt;
    if (type && *type != entry->entity->type)
      return std::nullopt;
    type = entry->entity->type;
  }
  return type;
}

void draw_banner(const std::string& text)
{
  const ImVec2 start  = ImGui::GetCursorScreenPos();
  const ImVec2 size   = ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight());
  ImDrawList*  canvas = ImGui::GetWindowDrawList();
  canvas->AddRectFilled(start, ImVec2(start.x + size.x, start.y + size.y), MULTI_EDIT_COLOR, 3.0f);
  canvas->AddText(ImVec2(start.x + ImGui::GetStyle().FramePadding.x,
                         start.y + (size.y - ImGui::GetTextLineHeight()) * 0.5f),
                  MULTI_EDIT_TEXT, text.c_str());
  ImGui::Dummy(size);
}

// Sticky above the scrolling fields, so what an edit will touch is never
// scrolled out of view.
void draw_selection_header(const shared::map_t& map, Span<const shared::entity_uid_t> selection)
{
  if (selection.empty())
  {
    ImGui::TextDisabled("Nothing selected");
    return;
  }
  if (selection.size() == 1)
  {
    ImGui::TextUnformatted(object_label(map, selection[0]).c_str());
    return;
  }

  const std::optional<entities::entity_type> type = shared_entity_type(map, selection);
  if (type)
    draw_banner(std::format("EDITING {} x {}", selection.size(), entities::entity_info(*type).classname));
  else
    draw_banner(std::format("{} OBJECTS SELECTED", selection.size()));
}

void draw_splitter(editor_sidebar_t& sidebar, float available_height)
{
  ImGui::InvisibleButton("##sidebar_splitter", ImVec2(-FLT_MIN, SPLITTER_THICKNESS));
  if (ImGui::IsItemHovered() || ImGui::IsItemActive())
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
  if (ImGui::IsItemActive() && available_height > 0.0f)
    sidebar.list_fraction += ImGui::GetIO().MouseDelta.y / available_height;

  const ImVec2 minimum = ImGui::GetItemRectMin();
  const ImVec2 maximum = ImGui::GetItemRectMax();
  const float  middle  = (minimum.y + maximum.y) * 0.5f;
  ImGui::GetWindowDrawList()->AddLine(ImVec2(minimum.x, middle), ImVec2(maximum.x, middle),
                                      ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_SeparatorActive
                                                                               : ImGuiCol_Separator));
}

} // namespace

outliner_result_t draw_editor_sidebar(editor_sidebar_t&                sidebar,
                                      editor_context_t&                context,
                                      entity_visibility_t&             visibility,
                                      Editor_Tool*                     active_tool,
                                      Span<const shared::entity_uid_t> selection)
{
  outliner_result_t result;
  if (context.map == nullptr)
    return result;

  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x, viewport->WorkPos.y),
                          ImGuiCond_Always, ImVec2(1.0f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(sidebar.width, viewport->WorkSize.y), ImGuiCond_Always);
  ImGui::SetNextWindowSizeConstraints(ImVec2(MINIMUM_WIDTH, viewport->WorkSize.y),
                                      ImVec2(EDITOR_SIDEBAR_MAXIMUM_WIDTH, viewport->WorkSize.y));

  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoNav |
                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus;
  if (ImGui::Begin("Sidebar##editor_sidebar", nullptr, flags))
  {
    sidebar.width = ImGui::GetWindowWidth();

    const float available   = ImGui::GetContentRegionAvail().y - SPLITTER_THICKNESS;
    const float list_height = std::clamp(sidebar.list_fraction * available, MINIMUM_PANE_HEIGHT,
                                         std::max(MINIMUM_PANE_HEIGHT, available - MINIMUM_PANE_HEIGHT));
    sidebar.list_fraction = available > 0.0f ? list_height / available : sidebar.list_fraction;

    if (ImGui::BeginChild("##sidebar_list", ImVec2(0.0f, list_height)))
      result = draw_entity_outliner(*context.map, visibility, sidebar.outliner, selection);
    ImGui::EndChild();

    draw_splitter(sidebar, available);

    draw_selection_header(*context.map, selection);

    const bool multi_edit = selection.size() > 1;
    if (multi_edit)
      ImGui::PushStyleColor(ImGuiCol_ChildBg, MULTI_EDIT_TINT);
    if (ImGui::BeginChild("##sidebar_inspector"))
    {
      if (active_tool != nullptr && !selection.empty())
        active_tool->on_draw_inspector(context);
    }
    ImGui::EndChild();
    if (multi_edit)
      ImGui::PopStyleColor();
  }
  ImGui::End();

  return result;
}

} // namespace client
