#include "entities/generated/entities_tables_generated.hpp"
#include "entity_outliner.hpp"

#include "../../shared/map/map.hpp"
#include "../../shared/map/map_group.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace client
{

namespace
{

const ImVec4 HIDDEN_TEXT_COLOR{0.55f, 0.55f, 0.60f, 1.0f};

// A sorted copy of a uid list. `contains` is a binary search, so the rows can
// ask it hundreds of times a frame without a set rebuilt from hundreds of
// allocations every frame.
struct sorted_uids_t
{
  std::vector<shared::entity_uid_t> uids;

  sorted_uids_t() = default;
  explicit sorted_uids_t(Span<const shared::entity_uid_t> source) : uids(source.begin(), source.end())
  {
    std::sort(uids.begin(), uids.end());
  }

  [[nodiscard]] bool   contains(shared::entity_uid_t uid) const { return std::binary_search(uids.begin(), uids.end(), uid); }
  [[nodiscard]] size_t size() const { return uids.size(); }
};

// Where a selection made elsewhere is shown: the group row when the whole group
// is selected, else the object's row with its group or the Geometry node opened.
struct reveal_t
{
  shared::entity_uid_t row           = shared::null_entity_uid;
  bool                 row_is_group  = false;
  shared::entity_uid_t open_group    = shared::null_entity_uid;
  bool                 open_geometry = false;
};

void scroll_to_if_revealed(reveal_t& reveal, shared::entity_uid_t uid, bool is_group)
{
  if (reveal.row != uid || reveal.row_is_group != is_group)
    return;
  if (!ImGui::IsItemVisible())
    ImGui::SetScrollHereY(0.5f);
  reveal.row = shared::null_entity_uid;
}

// The eye. A checkbox rather than a glyph button: it is a two-state control and
// ImGui already draws one, and the tick reads as "shown" without a legend.
bool draw_eye(const char* id, bool visible)
{
  ImGui::Checkbox(id, &visible);
  return visible;
}

// Geometry has no eye; the gap keeps its label in the entity rows' column.
void draw_eye_gap()
{
  ImGui::Dummy(ImVec2(ImGui::GetFrameHeight(), ImGui::GetFrameHeight()));
}

bool is_entity(const shared::map_t& map, shared::entity_uid_t uid)
{
  const shared::map_entity_t* entry = map.find_by_uid(uid);
  return entry != nullptr && entry->entity != nullptr;
}

std::string row_label(const entities::Entity& entity, shared::entity_uid_t uid)
{
  const std::string_view name{entity.name.data, entity.name.length};
  if (name.empty())
    return std::format("uid {}", uid);
  return std::format("{}  (uid {})", name, uid);
}

void draw_go_to_item(float& go_to_height, shared::entity_uid_t uid, outliner_result_t& result)
{
  if (ImGui::MenuItem("Go to"))
    result.go_to = uid;
  ImGui::SetNextItemWidth(120.0f);
  ImGui::DragFloat("height above", &go_to_height, 4.0f, 0.0f, 8192.0f, "%.0f");
}

constexpr size_t ENTITY_NAME_BUFFER_SIZE = sizeof(std::declval<entities::Entity>().name.data);

void begin_rename(row_rename_t& rename, shared::entity_uid_t uid, bool is_group, std::string_view name)
{
  rename.uid         = uid;
  rename.is_group    = is_group;
  rename.frames_open = 0;
  rename.name        = {};
  const size_t length = std::min(name.size(), (size_t)rename.name.size() - 1);
  std::memcpy(rename.name.data, name.data(), length);
}

std::string_view entity_name(const shared::map_t& map, shared::entity_uid_t uid)
{
  const entities::Entity& entity = *map.find_by_uid(uid)->entity;
  return {entity.name.data, entity.name.length};
}

// True once the field closes on a name that differs from `current`; Escape
// reverts the buffer, so an unchanged name is also the cancel.
bool draw_rename_field(row_rename_t& rename, std::string_view current, size_t capacity)
{
  if (rename.frames_open == 0)
    ImGui::SetKeyboardFocusHere();
  ++rename.frames_open;

  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::InputText("##rename", rename.name.data, capacity,
                   ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue);

  bool committed = false;
  if (ImGui::IsItemDeactivated())
  {
    committed  = current != rename.name.data;
    rename.uid = shared::null_entity_uid;
  }
  else if (rename.frames_open > 2 && !ImGui::IsItemActive())
  {
    rename.uid = shared::null_entity_uid;
  }
  return committed;
}

// `geometry` is the row's entry when the caller is already holding it, so a
// geometry row costs no map search; null means look the uid up.
void draw_object_row(const shared::map_t& map, entity_visibility_t& visibility, outliner_state_t& state,
                     const sorted_uids_t& selected, reveal_t& reveal, shared::entity_uid_t uid,
                     outliner_result_t& result, const shared::map_geometry_t* geometry = nullptr)
{
  ImGui::PushID((int)uid);

  const bool entity = geometry == nullptr && is_entity(map, uid);
  if (entity)
  {
    const bool shown = draw_eye("##eye", !visibility.is_hidden(uid));
    if (shown)
      visibility.hidden_entities.erase(uid);
    else
      visibility.hidden_entities.insert(uid);
  }
  else
  {
    draw_eye_gap();
  }
  ImGui::SameLine();

  row_rename_t& rename = state.rename;
  if (entity && !rename.is_group && rename.uid == uid)
  {
    if (draw_rename_field(rename, entity_name(map, uid), ENTITY_NAME_BUFFER_SIZE))
    {
      result.renamed_entity = uid;
      result.new_name       = rename.name.data;
    }
    scroll_to_if_revealed(reveal, uid, false);
    ImGui::PopID();
    return;
  }

  const bool hidden = entity && visibility.is_hidden(uid);
  if (hidden)
    ImGui::PushStyleColor(ImGuiCol_Text, HIDDEN_TEXT_COLOR);
  char label[160];
  if (geometry != nullptr)
    std::snprintf(label, sizeof(label), "mesh  (uid %u)", uid);
  else
    std::snprintf(label, sizeof(label), "%s", object_label(map, uid).c_str());
  if (ImGui::Selectable(label, selected.contains(uid)))
    result.clicked_object = uid;
  scroll_to_if_revealed(reveal, uid, false);
  if (hidden)
    ImGui::PopStyleColor();
  if (entity && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
    begin_rename(rename, uid, false, entity_name(map, uid));

  if (ImGui::BeginPopupContextItem("##object_menu"))
  {
    draw_go_to_item(state.go_to_height, uid, result);
    ImGui::Separator();
    if (ImGui::MenuItem("Select"))
      result.clicked_object = uid;
    if (entity && ImGui::MenuItem("Rename", "F2"))
      begin_rename(rename, uid, false, entity_name(map, uid));
    ImGui::EndPopup();
  }

  ImGui::PopID();
}

void draw_group_row(const shared::map_t& map, entity_visibility_t& visibility,
                    outliner_state_t& state, const sorted_uids_t& selected, reveal_t& reveal,
                    const shared::map_group_t& group, outliner_result_t& result)
{
  ImGui::PushID((int)group.uid);

  std::vector<shared::entity_uid_t> live_members;
  bool any_entity_member    = false;
  bool all_entities_shown   = true;
  bool all_members_selected = true;
  for (shared::entity_uid_t member : group.members)
  {
    if (!map.has_object(member))
      continue;
    live_members.push_back(member);
    all_members_selected &= selected.contains(member);
    if (is_entity(map, member))
    {
      any_entity_member = true;
      all_entities_shown &= !visibility.is_hidden(member);
    }
  }
  all_members_selected &= !live_members.empty();

  // The group's eye is its entity members' eyes together.
  if (any_entity_member)
  {
    const bool shown = draw_eye("##group_eye", all_entities_shown);
    if (shown != all_entities_shown)
      for (shared::entity_uid_t member : live_members)
        if (is_entity(map, member))
        {
          if (shown)
            visibility.hidden_entities.erase(member);
          else
            visibility.hidden_entities.insert(member);
        }
  }
  else
  {
    draw_eye_gap();
  }
  ImGui::SameLine();

  row_rename_t& rename = state.rename;
  if (rename.is_group && rename.uid == group.uid)
  {
    if (draw_rename_field(rename, group.name, rename.name.size()))
    {
      result.renamed_group = group.uid;
      result.new_name      = rename.name.data;
    }
    scroll_to_if_revealed(reveal, group.uid, true);
    ImGui::PopID();
    return;
  }

  ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
  if (all_members_selected)
    flags |= ImGuiTreeNodeFlags_Selected;

  if (reveal.open_group == group.uid)
    ImGui::SetNextItemOpen(true);
  const bool open = ImGui::TreeNodeEx("##group", flags, "%s  (%zu)", group.name.c_str(), live_members.size());
  const bool toggled = ImGui::IsItemToggledOpen();
  scroll_to_if_revealed(reveal, group.uid, true);
  if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !toggled)
    result.clicked_group = group.uid;
  if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !toggled)
    begin_rename(rename, group.uid, true, group.name);
  if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
    ImGui::SetTooltip("group, uid %u -- double-click to rename", group.uid);

  if (ImGui::BeginPopupContextItem("##group_menu"))
  {
    draw_go_to_item(state.go_to_height, group.uid, result);
    ImGui::Separator();
    if (ImGui::MenuItem("Select"))
      result.clicked_group = group.uid;
    if (ImGui::MenuItem("Rename", "F2"))
      begin_rename(rename, group.uid, true, group.name);
    if (ImGui::MenuItem("Ungroup"))
      result.ungroup = group.uid;
    ImGui::EndPopup();
  }

  if (open)
  {
    for (shared::entity_uid_t member : live_members)
      draw_object_row(map, visibility, state, selected, reveal, member, result);
    ImGui::TreePop();
  }

  ImGui::PopID();
}

reveal_t compute_outliner_reveal(const shared::map_t& map, Span<const shared::entity_uid_t> selection,
                   const sorted_uids_t& selected)
{
  reveal_t reveal;
  const shared::entity_uid_t first = selection[0];
  if (const shared::map_group_t* group = shared::find_group_containing_member(map, first))
  {
    bool whole_group = true;
    for (shared::entity_uid_t member : group->members)
      if (map.has_object(member) && !selected.contains(member))
        whole_group = false;

    if (whole_group)
    {
      reveal.row          = group->uid;
      reveal.row_is_group = true;
    }
    else
    {
      reveal.row        = first;
      reveal.open_group = group->uid;
    }
    return reveal;
  }

  reveal.row           = first;
  reveal.open_geometry = map.find_geometry_by_uid(first) != nullptr;
  return reveal;
}

// The selection is exactly one group's live members: that group. Else one
// entity alone: that entity. Geometry has no name.
void begin_requested_rename(const shared::map_t& map, Span<const shared::entity_uid_t> selection,
                            const sorted_uids_t& selected, row_rename_t& rename, reveal_t& reveal,
                            outliner_result_t& result)
{
  if (!selection.empty())
    if (const shared::map_group_t* group = shared::find_group_containing_member(map, selection[0]))
    {
      size_t live_members = 0;
      bool   whole_group  = true;
      for (shared::entity_uid_t member : group->members)
        if (map.has_object(member))
        {
          ++live_members;
          whole_group &= selected.contains(member);
        }
      if (whole_group && live_members == selected.size())
      {
        begin_rename(rename, group->uid, true, group->name);
        reveal              = {};
        reveal.row          = group->uid;
        reveal.row_is_group = true;
        return;
      }
    }

  if (selection.size() == 1 && is_entity(map, selection[0]))
  {
    begin_rename(rename, selection[0], false, entity_name(map, selection[0]));
    reveal     = {};
    reveal.row = selection[0];
    if (const shared::map_group_t* group = shared::find_group_containing_member(map, selection[0]))
      reveal.open_group = group->uid;
    return;
  }

  result.nothing_to_rename = true;
}

} // namespace

std::string object_label(const shared::map_t& map, shared::entity_uid_t uid)
{
  if (const shared::map_entity_t* entry = map.find_by_uid(uid); entry && entry->entity)
    return std::format("{}  {}", entities::entity_info(entry->entity->type).classname,
                       row_label(*entry->entity, uid));
  if (const shared::map_geometry_t* entry = map.find_geometry_by_uid(uid))
    return std::format("mesh  (uid {})", uid);
  return std::format("uid {} (gone)", uid);
}

void entity_visibility_t::refresh(const shared::map_t& map)
{
  hidden_this_frame.clear();
  for (const shared::map_entity_t& entry : map.entities)
    if (entry.entity && is_hidden(entry.uid))
      hidden_this_frame.push_back(entry.uid);
}

outliner_result_t draw_entity_outliner(const shared::map_t&              map,
                                       entity_visibility_t&              visibility,
                                       outliner_state_t&                 state,
                                       Span<const shared::entity_uid_t> selection)
{
  outliner_result_t result;

  ImGui::BeginDisabled(selection.size() < 2);
  if (ImGui::SmallButton("Group selection"))
    result.group_selection = true;
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::TextDisabled("(Ctrl+G, Ctrl+Shift+G ungroups)");

  // Always on screen when anything is hidden, and the reason the set is not
  // persisted: a map with something missing and no explanation is the failure
  // this line exists to make impossible.
  if (visibility.anything_hidden())
  {
    ImGui::TextColored(HIDDEN_TEXT_COLOR, "%zu hidden", visibility.hidden_this_frame.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Show all"))
      visibility.show_all();
  }
  ImGui::Separator();

  const sorted_uids_t selected(selection);

  reveal_t   reveal;
  const bool selection_changed =
      !std::equal(selection.begin(), selection.end(), state.previous_selection.begin(),
                  state.previous_selection.end());
  if (selection_changed)
  {
    state.previous_selection.assign(selection.begin(), selection.end());
    if (state.frames_ignoring_selection_change == 0 && !selection.empty())
      reveal = compute_outliner_reveal(map, selection, selected);
  }
  if (state.frames_ignoring_selection_change > 0)
    --state.frames_ignoring_selection_change;

  if (state.rename_requested)
  {
    state.rename_requested = false;
    begin_requested_rename(map, selection, selected, state.rename, reveal, result);
  }

  std::vector<shared::entity_uid_t> grouped_members;
  for (const shared::map_group_t& group : map.groups)
    grouped_members.insert(grouped_members.end(), group.members.begin(), group.members.end());
  const sorted_uids_t grouped(grouped_members);

  if (ImGui::BeginChild("##outliner_rows"))
  {
    for (const shared::map_group_t& group : map.groups)
      draw_group_row(map, visibility, state, selected, reveal, group, result);

    for (const shared::map_entity_t& entry : map.entities)
      if (entry.entity && !grouped.contains(entry.uid))
        draw_object_row(map, visibility, state, selected, reveal, entry.uid, result);

    std::vector<const shared::map_geometry_t*> loose_geometry;
    for (const shared::map_geometry_t& entry : map.geometry)
      if (!grouped.contains(entry.uid))
        loose_geometry.push_back(&entry);

    if (!loose_geometry.empty())
    {
      draw_eye_gap();
      ImGui::SameLine();
      if (reveal.open_geometry)
        ImGui::SetNextItemOpen(true);
      if (ImGui::TreeNodeEx("##geometry", ImGuiTreeNodeFlags_SpanAvailWidth, "Geometry  (%zu)",
                            loose_geometry.size()))
      {
        // Only the rows on screen are submitted; the revealed row is submitted
        // too, so it can scroll itself into view.
        ImGuiListClipper clipper;
        clipper.Begin((int)loose_geometry.size());
        for (size_t index = 0; index < loose_geometry.size(); ++index)
          if (loose_geometry[index]->uid == reveal.row && !reveal.row_is_group)
            clipper.IncludeItemByIndex((int)index);
        while (clipper.Step())
          for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index)
            draw_object_row(map, visibility, state, selected, reveal, loose_geometry[index]->uid,
                            result, loose_geometry[index]);
        ImGui::TreePop();
      }
    }
  }
  ImGui::EndChild();

  if (result.clicked_object || result.clicked_group)
  {
    state.frames_ignoring_selection_change = 2;
    result.toggles = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;
  }

  return result;
}

} // namespace client
