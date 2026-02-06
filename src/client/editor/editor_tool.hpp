#pragma once

#include "editor_types.hpp"

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
  // Tools logic update
  virtual void on_update(editor_context_t &ctx,
                         const viewport_state_t &view) = 0;

  // Mouse events
  virtual void on_mouse_down(editor_context_t &ctx, const mouse_event_t &e) = 0;
  virtual void on_mouse_drag(editor_context_t &ctx, const mouse_event_t &e) = 0;
  virtual void on_mouse_up(editor_context_t &ctx, const mouse_event_t &e) = 0;

  // Keyboard Shortcuts
  virtual void on_key_down(editor_context_t &ctx, const key_event_t &e) = 0;

  // Visuals
  virtual void on_draw_overlay(editor_context_t &ctx,
                               overlay_renderer_t &renderer) = 0;

  // UI (2D)
  virtual void on_draw_ui(editor_context_t &ctx) {}
};

} // namespace client
