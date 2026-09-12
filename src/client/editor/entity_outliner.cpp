#include "entity_outliner.hpp"

#include "../../shared/map.hpp"
#include "../../shared/map_group.hpp"

#include <imgui.h>

#include <algorithm>
#include <format>
#include <string>

namespace client
{

namespace
{

const ImVec4 HIDDEN_TEXT_COLOR{0.55f, 0.55f, 0.60f, 1.0f};

// The eye. A checkbox rather than a glyph button: it is a two-state control and
// ImGui already draws one, and the tick reads as "shown" without a legend.
bool draw_eye(const char* id, bool visible)
{
  ImGui::Checkbox(id, &visible);
  return visible;
}

std::string row_label(const entities::Entity& entity, shared::entity_uid_t uid)
{
  const std::string_view name{entity.name.data, entity.name.length};
  if (name.empty())
    return std::format("uid {}", uid);
  return std::format("{}  (uid {})", name, uid);
}

} // namespace

std::string object_label(const shared::map_t& map, shared::entity_uid_t uid)
{
  if (const shared::map_entity_t* entry = map.find_by_uid(uid); entry && entry->entity)
    return std::format("{}  {}", entities::entity_info(entry->entity->type).classname,
                       row_label(*entry->entity, uid));
  if (const shared::map_geometry_t* entry = map.find_geometry_by_uid(uid))
    return std::format("{}  (uid {})",
                       std::holds_alternative<shared::brush_geometry_t>(entry->value) ? "brush"
                                                                                       : "static mesh",
                       uid);
  return std::format("uid {} (gone)", uid);
}

bool entity_visibility_t::anything_hidden() const
{
  if (!hidden_entities.empty())
    return true;
  for (uint32_t index = 0; index < entities::ENTITY_TYPE_COUNT; ++index)
    if (hidden_types[(entities::entity_type)index])
      return true;
  return false;
}

void entity_visibility_t::show_all()
{
  hidden_entities.clear();
  hidden_types = {};
}

void entity_visibility_t::refresh(const shared::map_t& map)
{
  hidden_this_frame.clear();
  for (const shared::map_entity_t& entry : map.entities)
    if (entry.entity && is_hidden(entry.entity->type, entry.uid))
      hidden_this_frame.push_back(entry.uid);
}

outliner_result_t draw_entity_outliner(const shared::map_t&              map,
                                       entity_visibility_t&              visibility,
                                       Span<const shared::entity_uid_t> selection)
{
  outliner_result_t result;

  // Groups first: they are the rows an author made on purpose. The button is
  // the Ctrl+G everyone will not know about yet.
  {
    ImGui::BeginDisabled(selection.size() < 2);
    if (ImGui::SmallButton("Group selection"))
      result.group_selection = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("(Ctrl+G, Ctrl+Shift+G ungroups)");

    if (!map.groups.empty() && ImGui::TreeNode("##groups", "Groups (%zu)", map.groups.size()))
    {
      for (const shared::map_group_t& group : map.groups)
      {
        ImGui::PushID((int)group.uid);

        if (ImGui::SmallButton("Select"))
          result.clicked_group = group.uid;
        ImGui::SameLine();
        if (ImGui::SmallButton("Ungroup"))
          result.ungroup = group.uid;
        ImGui::SameLine();

        if (ImGui::TreeNode("##group", "%s  (uid %u, %zu)", group.name.c_str(), group.uid,
                            group.members.size()))
        {
          for (shared::entity_uid_t member : group.members)
          {
            ImGui::PushID((int)member);
            if (ImGui::Selectable(object_label(map, member).c_str()))
              result.clicked_object = member;
            ImGui::PopID();
          }
          ImGui::TreePop();
        }

        ImGui::PopID();
      }
      ImGui::TreePop();
    }
    ImGui::Separator();
  }

  // Always on screen when anything is hidden, and the reason the set is not
  // persisted: a map with something missing and no explanation is the failure
  // this line exists to make impossible.
  if (visibility.anything_hidden())
  {
    ImGui::TextColored(HIDDEN_TEXT_COLOR, "%zu hidden", visibility.hidden_this_frame.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Show all"))
      visibility.show_all();
    ImGui::Separator();
  }

  // Grouped by TYPE, which is what makes the group header the per-type control:
  // a separate type filter beside it would be a second place saying one thing.
  for (uint32_t index = 0; index < entities::ENTITY_TYPE_COUNT; ++index)
  {
    const entities::entity_type type = (entities::entity_type)index;

    std::vector<const shared::map_entity_t*> of_type;
    for (const shared::map_entity_t& entry : map.entities)
      if (entry.entity && entry.entity->type == type)
        of_type.push_back(&entry);

    if (of_type.empty())
      continue;

    ImGui::PushID((int)index);

    const bool type_shown = draw_eye("##type_eye", !visibility.hidden_types[type]);
    visibility.hidden_types[type] = !type_shown;
    ImGui::SameLine();

    const bool open = ImGui::TreeNode(
        "##type", "%s (%zu)", entities::entity_info(type).classname, of_type.size());

    if (open)
    {
      for (const shared::map_entity_t* entry : of_type)
      {
        ImGui::PushID((int)entry->uid);

        // A row's own eye stays meaningful while its TYPE is hidden -- it says
        // what happens when the type comes back, which is why it is not
        // disabled here.
        const bool row_shown = draw_eye("##eye", visibility.hidden_entities.count(entry->uid) == 0);
        if (row_shown)
          visibility.hidden_entities.erase(entry->uid);
        else
          visibility.hidden_entities.insert(entry->uid);

        ImGui::SameLine();

        const bool hidden = visibility.is_hidden(type, entry->uid);
        if (hidden)
          ImGui::PushStyleColor(ImGuiCol_Text, HIDDEN_TEXT_COLOR);

        if (ImGui::Selectable(row_label(*entry->entity, entry->uid).c_str()))
          result.clicked_object = entry->uid;

        if (hidden)
          ImGui::PopStyleColor();

        ImGui::PopID();
      }
      ImGui::TreePop();
    }

    ImGui::PopID();
  }

  return result;
}

} // namespace client
