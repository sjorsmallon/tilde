#include "selection_tool.hpp"
#include "../../hud.hpp"
#include "../editor_object_bounds.hpp"
#include "imgui.h"

#include <algorithm>
#include <format>

// Tab walks a multi-selection one member at a time. The selection stays the
// group, so a drag, a delete or a copy still reaches all of it; the inspector
// and the outline follow the focused member alone, and the gizmo stays where
// the group is.

namespace client
{

void Selection_Tool::cycle_focus(bool backwards)
{
  if (selected_uids.size() < 2)
  {
    focused_uid.reset();
    return;
  }

  const size_t none = selected_uids.size();
  const std::vector<shared::entity_uid_t>::iterator at =
      focused_uid ? std::find(selected_uids.begin(), selected_uids.end(), *focused_uid)
                  : selected_uids.end();
  const size_t current = at == selected_uids.end() ? none : (size_t)(at - selected_uids.begin());
  const size_t count   = none + 1;
  const size_t next    = backwards ? (current + count - 1) % count : (current + 1) % count;

  if (next == none)
  {
    focused_uid.reset();
    hud::set_announcement(std::format("All {} selected objects", none));
    return;
  }
  focused_uid = selected_uids[next];
  hud::set_announcement(std::format("Object {} of {}", next + 1, none));
}

void Selection_Tool::on_draw_inspector(editor_context_t& ctx)
{
  const std::vector<shared::entity_uid_t>::iterator at =
      focused_uid ? std::find(selected_uids.begin(), selected_uids.end(), *focused_uid)
                  : selected_uids.end();
  if (selected_uids.size() < 2 || at == selected_uids.end())
    focused_uid.reset();
  if (!focused_uid)
  {
    draw_inspector_of_selection(ctx);
    return;
  }

  ImGui::Text("%zu objects selected, showing %zu of %zu (Tab, Shift+Tab)", selected_uids.size(),
              (size_t)(at - selected_uids.begin()) + 1, selected_uids.size());
  ImGui::Separator();

  // The inspector reads selected_uids as "what is inspected", so for its
  // duration that is the one member. A button in it may reselect (Tie to new
  // replaces the selection with the owner): then the group is gone and the
  // focus with it, otherwise the group comes back.
  std::vector<shared::entity_uid_t> group = std::move(selected_uids);
  selected_uids                           = {*focused_uid};
  draw_inspector_of_selection(ctx);
  if (selected_uids.size() == 1 && selected_uids[0] == *focused_uid)
    selected_uids = std::move(group);
  else
    focused_uid.reset();
}

void Selection_Tool::draw_focus_outline(editor_context_t& ctx, draw_lists_t& draws)
{
  if (!focused_uid)
    return;
  if (draws.outline_object(*focused_uid, renderer::outline_t::hovered))
    return;
  const shared::aabb_bounds_t bounds = editor_object_bounds(*ctx.map, *focused_uid);
  draws.debug.box((bounds.min + bounds.max) * 0.5f, (bounds.max - bounds.min) * 0.5f,
                  colors::yellow, renderer::fill_mode_t::wireframe, 0.f, 0.f,
                  /*draw_when_occluded*/ true);
}

} // namespace client
