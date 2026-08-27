#include "map_cvars_panel.hpp"

#include "../../shared/cvars/cvar_console.hpp"
#include "../../shared/cvars/generated/cvars_generated.hpp"
#include "imgui.h"
#include "transaction_system.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace client
{

namespace
{

const ImVec4 warning_color{1.0f, 0.75f, 0.2f, 1.0f};
const ImVec4 error_color{1.0f, 0.35f, 0.35f, 1.0f};
const ImVec4 mirrored_color{0.55f, 0.85f, 1.0f, 1.0f};

// Sorted by cvar name, which is the order the file is written and read back in
// -- the block's properties are a std::map. Keeping the panel in that same order
// means a save and reload never reshuffles the rows under the author.
std::vector<std::string> sorted_by_name(std::vector<std::string> lines)
{
  std::sort(lines.begin(), lines.end(),
            [](const std::string &a, const std::string &b)
            { return shared::split_cvar_line(a).name < shared::split_cvar_line(b).name; });
  return lines;
}

// Does this text parse as this cvar's type? The scratch state is a parse TARGET
// and is never read: try_cvar_from_text is the console's own parser, so the
// panel and the server agree on what a valid value is by construction.
bool value_parses(cvars::cvar_id id, const std::string &text)
{
  static cvars::cvar_state_t scratch{};
  return cvars::try_cvar_from_text(scratch, id, text);
}

// What a valid value looks like, for the "that isn't one" tooltip. An enum
// spells out its whole value set rather than saying "an enum": the set is the
// only thing an author could not have guessed, and it is short by construction.
std::string value_hint_for(const cvars::cvar_info_t &info)
{
  switch (info.type)
  {
    case cvars::CVAR_TYPE_BOOL:   return "0 or 1";
    case cvars::CVAR_TYPE_F32:    return "a number";
    case cvars::CVAR_TYPE_I32:
    case cvars::CVAR_TYPE_U32:    return "a whole number";
    case cvars::CVAR_TYPE_STRING: return "text";
    case cvars::CVAR_TYPE_ENUM:
    {
      std::string hint = "one of: ";
      for (uint32_t value = 0; value < info.enum_info->value_names.size(); ++value)
      {
        if (value > 0)
          hint += ", ";
        hint += info.enum_info->value_names[value];
      }
      return hint;
    }
  }
  return "";
}

std::string lowercased(std::string text)
{
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char c) { return (char)std::tolower(c); });
  return text;
}

bool map_already_sets(const shared::map_t &map, const char *name)
{
  return std::any_of(map.attached_cvars.begin(), map.attached_cvars.end(),
                     [&](const std::string &line)
                     { return shared::split_cvar_line(line).name == name; });
}

void draw_row_status(const shared::cvar_line_t &row,
                     const std::optional<cvars::cvar_id> &id)
{
  if (!id)
  {
    ImGui::TextColored(error_color, "unknown cvar");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("No cvar named '%s' in this build. The server refuses "
                        "the line at map load.",
                        row.name.c_str());
    return;
  }

  const cvars::cvar_info_t &info = cvars::cvar_info(*id);

  if (row.value.empty())
  {
    ImGui::TextColored(error_color, "no value");
    return;
  }

  if (!value_parses(*id, row.value))
  {
    ImGui::TextColored(error_color, "bad value");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("'%s' is not %s", row.value.c_str(), value_hint_for(info).c_str());
    return;
  }

  if (info.flags & cvars::CVAR_FLAG_CLIENT)
  {
    ImGui::TextColored(warning_color, "client-only");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("A @Client cvar is owned by each player's own process. "
                        "The server applies this line to itself, so on a "
                        "dedicated server it does nothing.");
    return;
  }

  if (info.flags & cvars::CVAR_FLAG_MIRRORED)
  {
    ImGui::TextColored(mirrored_color, "mirrored");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Server-owned and pushed to every client, so prediction "
                        "stays in agreement.");
    return;
  }

  ImGui::TextDisabled("server");
}

} // namespace

void draw_map_cvars_panel(shared::map_t &map, const cvars::cvar_state_t &live_values,
                          Transaction_System &transactions)
{
  ImGui::Begin("Map Cvars");

  ImGui::TextWrapped("Run by the server when it loads this map, and saved with "
                     "the map, so they travel with it.");
  ImGui::Separator();

  // Every mutation goes through here: the list is replaced wholesale and the
  // before/after pair becomes one undo entry, exactly as a geometry edit does.
  auto commit_list = [&](std::vector<std::string> next)
  {
    transaction_builder_t builder;
    builder.add_map_cvars_modified(map.attached_cvars, sorted_by_name(std::move(next)));
    transaction_t transaction = builder.take();
    if (transaction.empty())
      return;

    map.attached_cvars = std::get<diff_map_cvars_t>(transaction.diffs.front()).after;
    transactions.push(std::move(transaction));
  };

  int row_to_remove = -1;
  int row_to_rewrite = -1;
  std::string rewritten_value;

  if (map.attached_cvars.empty())
  {
    ImGui::TextDisabled("(none: this map uses whatever the server is set to)");
  }
  else if (ImGui::BeginTable("##map_cvars", 4,
                             ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
  {
    ImGui::TableSetupColumn("cvar", ImGuiTableColumnFlags_WidthStretch, 0.40f);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.28f);
    ImGui::TableSetupColumn("status", ImGuiTableColumnFlags_WidthStretch, 0.24f);
    ImGui::TableSetupColumn("##remove", ImGuiTableColumnFlags_WidthFixed, 26.0f);

    for (int index = 0; index < (int)map.attached_cvars.size(); ++index)
    {
      const shared::cvar_line_t row = shared::split_cvar_line(map.attached_cvars[index]);
      const std::optional<cvars::cvar_id> id = cvars::try_find_cvar(row.name);

      ImGui::PushID(index);
      ImGui::TableNextRow();

      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(row.name.c_str());
      if (id && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s%s",
                          cvars::describe_cvar_flags(cvars::cvar_info(*id).flags).c_str(),
                          cvars::cvar_info(*id).description);

      // The buffer is refilled from the map every frame and read back only when
      // the box is deactivated after an edit, so typing a value produces ONE
      // undo entry rather than one per keystroke.
      ImGui::TableNextColumn();
      char value_buffer[96];
      std::snprintf(value_buffer, sizeof(value_buffer), "%s", row.value.c_str());
      ImGui::SetNextItemWidth(-FLT_MIN);
      ImGui::InputText("##value", value_buffer, sizeof(value_buffer));
      if (ImGui::IsItemDeactivatedAfterEdit())
      {
        row_to_rewrite  = index;
        rewritten_value = value_buffer;
      }

      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      draw_row_status(row, id);

      ImGui::TableNextColumn();
      if (ImGui::SmallButton("x"))
        row_to_remove = index;
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Remove this setting from the map");

      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  ImGui::Separator();

  // A name is PICKED, never typed: the point of the panel is that a map cannot
  // carry a cvar this build does not have.
  static char filter_buffer[64] = {};
  ImGui::SetNextItemWidth(-FLT_MIN);
  ImGui::InputTextWithHint("##cvar_filter", "add a cvar (type to filter)",
                           filter_buffer, sizeof(filter_buffer));

  std::optional<cvars::cvar_id> cvar_to_add;
  if (ImGui::BeginChild("##cvar_choices", ImVec2(0, 140), true))
  {
    const std::string filter_text = lowercased(filter_buffer);

    int shown = 0;
    const Span<const cvars::cvar_info_t> infos = cvars::cvar_infos();
    for (uint32_t i = 0; i < infos.size(); ++i)
    {
      const cvars::cvar_info_t &info = infos[i];
      if (!filter_text.empty() &&
          lowercased(info.name).find(filter_text) == std::string::npos)
        continue;

      const bool already_present = map_already_sets(map, info.name);

      ImGui::BeginDisabled(already_present);
      if (ImGui::Selectable(info.name))
        cvar_to_add = (cvars::cvar_id)i;
      ImGui::EndDisabled();

      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s%s", cvars::describe_cvar_flags(info.flags).c_str(),
                          info.description);
      ++shown;
    }

    if (shown == 0)
      ImGui::TextDisabled("(no cvar matches)");
  }
  ImGui::EndChild();

  ImGui::End();

  // One action per frame, each its own undo entry. Deferred to here because
  // every one of them replaces the vector the loop above is walking.
  if (row_to_remove >= 0)
  {
    std::vector<std::string> next = map.attached_cvars;
    next.erase(next.begin() + row_to_remove);
    commit_list(std::move(next));
  }
  else if (row_to_rewrite >= 0)
  {
    std::vector<std::string> next = map.attached_cvars;
    next[row_to_rewrite] = shared::make_cvar_line(
        shared::split_cvar_line(next[row_to_rewrite]).name, rewritten_value);
    commit_list(std::move(next));
  }
  else if (cvar_to_add)
  {
    // Seeded with what the cvar holds right now, so a new row starts from a
    // valid value rather than from an empty box the author has to guess at.
    const cvars::cvar_info_t &info = cvars::cvar_info(*cvar_to_add);
    const std::optional<std::string> current =
        cvars::try_cvar_to_text(live_values, *cvar_to_add);

    std::vector<std::string> next = map.attached_cvars;
    next.push_back(shared::make_cvar_line(info.name, current ? *current : std::string()));
    commit_list(std::move(next));
    filter_buffer[0] = 0;
  }
}

} // namespace client
