#pragma once

#include "editor_types.hpp"

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

  // Lifecycle
  virtual void on_enable(editor_context_t &ctx) = 0;
  virtual void on_disable(editor_context_t &ctx) = 0;

  // Input Dispatch
  // Tools logic update. dt is the real frame delta-time in seconds.
  virtual void on_update(editor_context_t &ctx,
                         const viewport_state_t &view, float dt) = 0;

  // Mouse events
  virtual void on_mouse_down(editor_context_t &ctx, const input::mouse_event_t &e) = 0;
  virtual void on_mouse_drag(editor_context_t &ctx, const input::mouse_event_t &e) = 0;
  virtual void on_mouse_up(editor_context_t &ctx, const input::mouse_event_t &e) = 0;

  // Keyboard Shortcuts
  virtual void on_key_down(editor_context_t &ctx, const key_event_t &e) = 0;

  // Return true when the tool wants exclusive keyboard focus (suppresses
  // camera movement keys in tool_editor_state).
  virtual bool capture_keyboard() const { return false; }

  // Visuals
  virtual void on_draw_overlay(editor_context_t &ctx,
                               overlay_renderer_t &renderer) = 0;

  // UI (2D)
  virtual void on_draw_ui(editor_context_t &ctx) {}
};

} // namespace client
