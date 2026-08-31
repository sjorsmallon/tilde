#pragma once

#include "editor_types.hpp"

#include <optional>

//@NOTE(SJM):
// full virtual root tool interface. the tool_editor state contains a list of all tools, 
// and forwards input/events to the active one.
// tools can also choose to capture keyboard input for shortcuts.

namespace client
{

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

  // Return true when the tool wants exclusive keyboard focus (suppresses
  // camera movement keys in tool_editor_state).
  virtual bool capture_keyboard() const { return false; }

  // overrides for numlock camera locking behavior.
  virtual std::optional<view_focus_t> view_focus() const { return std::nullopt; }

  // overlay (not ui).
  virtual void on_draw_overlay(editor_context_t& ctx,
                               pass_builder_t &draws) = 0;

  // UI (2D)
  virtual void on_draw_ui(editor_context_t& ctx) {}
};

} // namespace client
