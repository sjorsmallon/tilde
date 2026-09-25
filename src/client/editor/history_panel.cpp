#include "history_panel.hpp"

#include "../../shared/entities/entity_reflection.hpp"
#include "imgui.h"

#include <format>
#include <optional>
#include <string>

namespace client
{

namespace
{

constexpr size_t MAX_TOOLTIP_LINES = 16;

std::string describe_field_changes(const diff_entity_modified_t& diff)
{
  const Span<const field_info_t> fields = entities::entity_info(diff.type).fields;
  std::string names;
  for (const entities::field_change_t& change : diff.changes)
  {
    if (!names.empty())
      names += ", ";
    names += change.index < fields.size() ? fields[change.index].name : "?";
  }
  return names;
}

std::string describe_diff(const edit_diff_t& diff)
{
  return std::visit(
      overloaded{
          [](const diff_entity_created_t& d)
          { return std::format("created {} {}", entities::classname_of(d.snapshot.get()), d.uid); },
          [](const diff_entity_removed_t& d)
          { return std::format("deleted {} {}", entities::classname_of(d.snapshot.get()), d.uid); },
          [](const diff_entity_modified_t& d)
          {
            return std::format("{} {}: {}", entities::entity_info(d.type).classname, d.uid,
                               describe_field_changes(d));
          },
          [](const diff_geometry_created_t& d) { return std::format("created geometry {}", d.uid); },
          [](const diff_geometry_removed_t& d) { return std::format("deleted geometry {}", d.uid); },
          [](const diff_geometry_modified_t& d) { return std::format("geometry {}", d.uid); },
          [](const diff_map_cvars_t& d)
          { return std::format("map cvars: {} -> {}", d.before.size(), d.after.size()); },
          [](const diff_map_connections_t& d)
          { return std::format("connections: {} -> {}", d.before.size(), d.after.size()); },
          [](const diff_map_groups_t& d)
          { return std::format("groups: {} -> {}", d.before.size(), d.after.size()); }},
      diff);
}

void draw_transaction_tooltip(const transaction_t& transaction)
{
  if (!ImGui::IsItemHovered())
    return;

  ImGui::BeginTooltip();
  for (size_t index = 0; index < transaction.diffs.size() && index < MAX_TOOLTIP_LINES; ++index)
    ImGui::TextUnformatted(describe_diff(transaction.diffs[index]).c_str());
  if (transaction.diffs.size() > MAX_TOOLTIP_LINES)
    ImGui::TextDisabled("and %zu more", transaction.diffs.size() - MAX_TOOLTIP_LINES);
  ImGui::EndTooltip();
}

} // namespace

bool draw_history_panel(Transaction_System& transactions, shared::map_t& map, bool& open)
{
  static size_t scrolled_to_applied_count = SIZE_MAX;

  std::optional<size_t> requested_applied_count;

  if (ImGui::Begin("Edit History", &open, ImGuiWindowFlags_NoNav))
  {
    const Span<const transaction_t> applied = transactions.applied_transactions();
    const Span<const transaction_t> undone  = transactions.undone_transactions();
    const bool scroll_to_current = scrolled_to_applied_count != applied.size();

    if (ImGui::Selectable("Map loaded", applied.size() == 0))
      requested_applied_count = 0;
    if (scroll_to_current && applied.size() == 0)
      ImGui::SetScrollHereY();

    for (uint32_t index = 0; index < applied.size(); ++index)
    {
      ImGui::PushID((int)index);
      const bool is_current = index + 1 == applied.size();
      if (ImGui::Selectable(applied[index].name.c_str(), is_current))
        requested_applied_count = index + 1;
      draw_transaction_tooltip(applied[index]);
      if (scroll_to_current && is_current)
        ImGui::SetScrollHereY();
      ImGui::PopID();
    }

    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    for (uint32_t row = 0; row < undone.size(); ++row)
    {
      const transaction_t& transaction = undone[undone.size() - 1 - row];
      ImGui::PushID((int)(applied.size() + row));
      if (ImGui::Selectable(transaction.name.c_str(), false))
        requested_applied_count = applied.size() + row + 1;
      draw_transaction_tooltip(transaction);
      ImGui::PopID();
    }
    ImGui::PopStyleColor();

    scrolled_to_applied_count = applied.size();
  }
  ImGui::End();

  if (!requested_applied_count)
    return false;
  transactions.jump_to(map, *requested_applied_count);
  return true;
}

} // namespace client
