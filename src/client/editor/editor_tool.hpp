#pragma once

#include "../../shared/entity_uid.hpp"
#include "../../shared/span.hpp"
#include "editor_types.hpp"

#include <algorithm>
#include <optional>

//@NOTE(SJM):
// full virtual root tool interface. the tool_editor state contains a list of all tools, 
// and forwards input/events to the active one.
// tools can also choose to capture keyboard input for shortcuts.

namespace client
{

// Where a dragged map object is drawn ahead of the map: its translation from where the map still holds it.
struct live_drag_offset_t
{
  shared::entity_uid_t uid = 0;
  linalg::vec3         translation{0, 0, 0};
};

inline const live_drag_offset_t* find_live_drag_offset(Span<const live_drag_offset_t> offsets_sorted,
                                                       shared::entity_uid_t uid)
{
  const live_drag_offset_t* slot = std::lower_bound(
      offsets_sorted.begin(), offsets_sorted.end(), uid,
      [](const live_drag_offset_t& entry, shared::entity_uid_t key) { return entry.uid < key; });
  return slot != offsets_sorted.end() && slot->uid == uid ? slot : nullptr;
}

class Editor_Tool
{
public:
  virtual ~Editor_Tool() = default;

  virtual void on_enable(editor_context_t& ctx) = 0;
  virtual void on_disable(editor_context_t& ctx) = 0;
  virtual void on_update(editor_context_t& ctx,
                         const viewport_state_t &view, float dt) = 0;

  // mouse events
  virtual void on_mouse_down(editor_context_t& ctx, const input::mouse_event_t& e) = 0;
  virtual void on_mouse_drag(editor_context_t& ctx, const input::mouse_event_t& e) = 0;
  virtual void on_mouse_up(editor_context_t& ctx, const input::mouse_event_t& e) = 0;

  // keyboard events
  virtual void on_key_down(editor_context_t& ctx, const key_event_t& e) = 0;

  // overrides for numlock camera locking behavior.
  virtual std::optional<view_focus_t> view_focus() const { return std::nullopt; }

  // overlay (not ui).
  virtual void on_draw_overlay(editor_context_t& ctx,
                               pass_builder_t &draws) = 0;

  // UI (2D)
  virtual void on_draw_ui(editor_context_t& ctx) {}

  // The sidebar's lower pane, drawn inside it: no window of its own.
  virtual void on_draw_inspector(editor_context_t& ctx) {}

  // Whether the sidebar shows the outliner above the pane. False for a tool
  // that edits an asset or runs a job, which then takes the whole height.
  virtual bool edits_map_objects() const { return true; }

  // What this tool currently has selected, for panels OUTSIDE the tools that
  // colour by it -- the Map Info connection lines are the one caller. Empty for
  // every tool that has no selection, which is most of them.
  virtual Span<const shared::entity_uid_t> selected_objects() const { return {}; }

  // Geometry this tool is dragging but has not written yet, sorted by uid; the
  // editor draws each at its offset. Empty for every tool that writes as it drags.
  virtual Span<const live_drag_offset_t> live_drag_offsets() const { return {}; }
};

} // namespace client
