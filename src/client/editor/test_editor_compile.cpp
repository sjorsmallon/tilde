#include "editor_tool.hpp"
#include "editor_types.hpp"
#include "tools/placement_tool.hpp"
#include "tools/sculpting_tool.hpp"
#include "tools/selection_tool.hpp"
#include <vector>

// Mock Renderer
struct MockRenderer : public client::overlay_renderer_t
{
  void draw_line(const linalg::vec3 &start, const linalg::vec3 &end,
                 uint32_t color) override
  {
  }
  void draw_wire_box(const linalg::vec3 &center,
                     const linalg::vec3 &half_extents, uint32_t color) override
  {
  }
  void draw_solid_box(const linalg::vec3 &center,
                      const linalg::vec3 &half_extents, uint32_t color) override
  {
  }
  void draw_circle(const linalg::vec3 &center, float radius,
                   const linalg::vec3 &normal, uint32_t color) override
  {
  }
  void draw_text(const linalg::vec3 &pos, const char *text,
                 uint32_t color) override
  {
  }
};

int main()
{
  // Ensure types behave as expected
  client::editor_context_t ctx = {};
  client::viewport_state_t view = {};
  client::mouse_event_t mouse = {};
  client::key_event_t key = {};
  MockRenderer renderer;

  std::vector<client::Editor_Tool *> tools;
  tools.push_back(new client::Selection_Tool());
  tools.push_back(new client::Placement_Tool());
  tools.push_back(new client::Sculpting_Tool());

  for (auto *tool : tools)
  {
    tool->on_enable(ctx);
    tool->on_update(ctx, view);
    tool->on_mouse_down(ctx, mouse);
    tool->on_mouse_drag(ctx, mouse);
    tool->on_mouse_up(ctx, mouse);
    tool->on_key_down(ctx, key);
    tool->on_draw_overlay(ctx, renderer);
    tool->on_disable(ctx);
    delete tool;
  }

  return 0;
}
