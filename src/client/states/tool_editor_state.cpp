#include "tool_editor_state.hpp"
#include "../editor/tools/placement_tool.hpp"
#include "../editor/tools/sculpting_tool.hpp"
#include "../editor/tools/selection_tool.hpp"
#include "../input.hpp"
#include "../renderer.hpp"
#include "../shared/linalg.hpp"
#include "../shared/math.hpp"
#include "../state_manager.hpp"
#include "SDL_scancode.h"
#include "imgui.h"
#include <SDL.h>

namespace client
{

// Concrete renderer adapter
struct VulkanOverlayRenderer : public overlay_renderer_t
{
  VkCommandBuffer cmd;

  VulkanOverlayRenderer(VkCommandBuffer c) : cmd(c) {}

  VkCommandBuffer get_command_buffer() override { return cmd; }

  void draw_line(const linalg::vec3 &start, const linalg::vec3 &end,
                 uint32_t color) override
  {
    renderer::DrawLine(cmd, start, end, color);
  }

  void draw_wire_box(const linalg::vec3 &center,
                     const linalg::vec3 &half_extents, uint32_t color) override
  {
    linalg::vec3 min = center - half_extents;
    linalg::vec3 max = center + half_extents;
    renderer::DrawAABB(cmd, min, max, color);
  }

  void draw_solid_box(const linalg::vec3 &center,
                      const linalg::vec3 &half_extents, uint32_t color) override
  {
    // Fallback to wireframe for now as renderer implementation is limited
    draw_wire_box(center, half_extents, color);
  }

  void draw_circle(const linalg::vec3 &center, float radius,
                   const linalg::vec3 &normal, uint32_t color) override
  {
    // Approximate circle with lines
    const int segments = 16;
    linalg::vec3 tangent, bitangent;

    // Simple basis construction
    if (std::abs(normal.y) > 0.9f)
      tangent = {1, 0, 0};
    else
      tangent = linalg::normalize(linalg::cross({0, 1, 0}, normal));
    bitangent = linalg::cross(normal, tangent);

    for (int i = 0; i < segments; ++i)
    {
      float t1 = (float)i / segments * 2.0f * 3.14159f;
      float t2 = (float)(i + 1) / segments * 2.0f * 3.14159f;

      linalg::vec3 p1 =
          center + (tangent * std::cos(t1) + bitangent * std::sin(t1)) * radius;
      linalg::vec3 p2 =
          center + (tangent * std::cos(t2) + bitangent * std::sin(t2)) * radius;

      draw_line(p1, p2, color);
    }
  }

  void draw_text(const linalg::vec3 &pos, const char *text,
                 uint32_t color) override
  {
    // Not supported in immediate 3d cmd buffer easily without font texture
    // binding Could use ImGui::GetBackgroundDrawList()->AddText but that
    // requires projection
  }
};

void ToolEditorState::on_enter()
{
  log_terminal("Entered ToolEditorState");
  // Initialize map with a floor
  if (map.static_geometry.empty())
  {
    map.name = "Tool Editor Map";
    shared::aabb_t floor_aabb;
    floor_aabb.center = {0, -2.0f, 0};
    floor_aabb.half_extents = {10.0f, 0.5f, 10.0f};
    map.static_geometry.push_back({floor_aabb});
    renderer::draw_announcement("Welcome to the Tool Editor!");
  }

  // Initialize Camera
  camera.x = 0;
  camera.y = 5;
  camera.z = 10;
  camera.pitch = -30.0f;
  camera.yaw = 0.0f;
  fov = 90.0f;
  aspect = 1.77f; // Will update
  z_near = 0.1f;
  z_far = 1000.0f;

  // Initialize Tools
  if (tools.empty())
  {
    tools.push_back(std::make_unique<Selection_Tool>());
    tools.push_back(std::make_unique<Placement_Tool>());
    tools.push_back(std::make_unique<Sculpting_Tool>());
  }

  // Enable first tool
  switch_tool(0);

  update_bvh();
}

void ToolEditorState::on_exit()
{
  if (active_tool_index >= 0 && active_tool_index < (int)tools.size())
  {
    tools[active_tool_index]->on_disable(context);
  }
}

void ToolEditorState::switch_tool(int index)
{
  if (active_tool_index == index)
    return;

  if (active_tool_index >= 0 && active_tool_index < (int)tools.size())
  {
    tools[active_tool_index]->on_disable(context);
  }

  active_tool_index = index;

  // Update context
  context.map = &map;
  context.bvh = &bvh;
  context.geometry_updated = &geometry_updated_flag;
  context.time = 0; // TODO: Get real time

  if (active_tool_index >= 0 && active_tool_index < (int)tools.size())
  {
    tools[active_tool_index]->on_enable(context);
  }
}

viewport_state_t ToolEditorState::transform_viewport_state()
{
  viewport_state_t view;
  view.camera = camera;
  view.camera.orthographic = camera.orthographic; // Redundant if simple copy
  // Wait, camera copy copies everything.
  // But camera struct doesn't have `camera_forward` etc. It has x,y,z, yaw,
  // pitch. Basis vectors were calculated manually in `transform_viewport_state`
  // before. `viewport_state_t` had `camera_forward` etc. I REMOVED
  // `camera_forward` etc from `viewport_state_t` in previous step. So I don't
  // need to assign them. But wait, if tools NEED forward/right/up, they must
  // call `get_orientation_vectors(view.camera)`. My plan said "Update usages
  // ... and helper functions".

  // So here just copy camera.
  view.camera = camera;

  int mx, my;
  input::get_mouse_pos(&mx, &my);

  // Ray construct
  // Normalized Device Coordinates
  ImGuiIO &io = ImGui::GetIO();
  float width = io.DisplaySize.x;
  float height = io.DisplaySize.y;
  if (width == 0 || height == 0)
  {
    width = 1280;
    height = 720;
  }

  float x_ndc = (2.0f * mx) / width - 1.0f;
  float y_ndc = 1.0f - (2.0f * my) / height;

  view.mouse_ray = client::get_pick_ray(camera, x_ndc, y_ndc, width / height);
  // view.orthographic etc were removed from struct.
  // view.camera has them.

  view.display_size = {width, height};
  view.aspect_ratio = width / height;
  view.fov = fov;

  return view;
}

void ToolEditorState::update(float dt)
{
  // Update Camera
  ImGuiIO &io = ImGui::GetIO();
  if (!io.WantCaptureMouse)
  {
    float speed = 10.0f * dt;
    if (input::is_key_down(SDL_SCANCODE_LSHIFT))
      speed *= 2.0f;

    auto vectors = client::get_orientation_vectors(camera);
    linalg::vec3 F = vectors.forward;
    linalg::vec3 R = vectors.right;
    linalg::vec3 U = vectors.up;

    if (input::is_key_pressed(SDL_SCANCODE_Z))
    {
      if (input::is_key_down(SDL_SCANCODE_LCTRL))
      {
        if (input::is_key_down(SDL_SCANCODE_LSHIFT))
        {
          if (transaction_system.can_redo())
          {
            transaction_system.redo(map);
            geometry_updated_flag = true;
          }
        }
        else
        {
          if (transaction_system.can_undo())
          {
            transaction_system.undo(map);
            geometry_updated_flag = true;
          }
        }
      }
    }

    if (input::is_key_pressed(SDL_SCANCODE_Y))
    {
      if (input::is_key_down(SDL_SCANCODE_LCTRL))
      {
        if (transaction_system.can_redo())
        {
          transaction_system.redo(map);
          geometry_updated_flag = true;
        }
      }
    }

    if (input::is_key_pressed(SDL_SCANCODE_O))
    {
      camera.orthographic = !camera.orthographic;
      if (camera.orthographic)
      {
        camera.yaw = iso_yaw;
        camera.pitch = iso_pitch;
      }
    }

    if (input::is_key_pressed(SDL_SCANCODE_1))
    {
      renderer::draw_announcement("Hello World");
    }

    if (input::is_key_down(SDL_SCANCODE_W))
    {
      if (camera.orthographic)
      {
        camera.x += U.x * speed;
        camera.y += U.y * speed;
        camera.z += U.z * speed;
      }
      else
      {
        camera.x += F.x * speed;
        camera.y += F.y * speed;
        camera.z += F.z * speed;
      }
    }
    if (input::is_key_down(SDL_SCANCODE_S))
    {
      if (camera.orthographic)
      {
        camera.x -= U.x * speed;
        camera.y -= U.y * speed;
        camera.z -= U.z * speed;
      }
      else
      {
        camera.x -= F.x * speed;
        camera.y -= F.y * speed;
        camera.z -= F.z * speed;
      }
    }
    if (input::is_key_down(SDL_SCANCODE_D))
    {
      camera.x += R.x * speed;
      camera.z += R.z * speed;
    }
    if (input::is_key_down(SDL_SCANCODE_A))
    {
      camera.x -= R.x * speed;
      camera.z -= R.z * speed;
    }
    if (input::is_key_down(SDL_SCANCODE_SPACE))
    {
      if (camera.orthographic)
        camera.ortho_height += speed;
      else
        camera.y += speed;
    }
    if (input::is_key_down(SDL_SCANCODE_LCTRL))
    {
      if (camera.orthographic)
      {
        camera.ortho_height -= speed;
        if (camera.ortho_height < 1.0f)
          camera.ortho_height = 1.0f;
      }
      else
      {
        camera.y -= speed;
      }
    }
    if (input::is_key_down(SDL_SCANCODE_Q))
    {
      if (!camera.orthographic)
        camera.y -= speed;
    }

    if (input::is_mouse_down(SDL_BUTTON_RIGHT))
    {
      input::set_relative_mouse_mode(true);
      int dx, dy;
      input::get_mouse_delta(&dx, &dy);
      camera.yaw += dx * 0.1f;
      camera.pitch -= dy * 0.1f;
      shared::clamp_this(camera.pitch, -89.0f, 89.0f);
    }
    else
    {
      input::set_relative_mouse_mode(false);
    }
  }

  if (geometry_updated_flag)
  {
    update_bvh();
    geometry_updated_flag = false;
  }

  // Update Viewport
  context.map = &map;
  context.bvh = &bvh;
  context.geometry_updated = &geometry_updated_flag;
  context.transaction_system = &transaction_system;
  context.time += dt;
  viewport = transform_viewport_state();

  if (active_tool_index >= 0 && active_tool_index < (int)tools.size())
  {
    tools[active_tool_index]->on_update(context, viewport);

    // Mouse Events
    int mx, my;
    input::get_mouse_pos(&mx, &my);
    int mdx, mdy;
    input::get_mouse_delta(&mdx, &mdy);

    mouse_event_t mouse_e;
    mouse_e.pos = {mx, my};
    mouse_e.delta = {mdx, mdy};
    mouse_e.shift_down = input::is_key_down(
        SDL_SCANCODE_LSHIFT); // LSHIFT SDL_SCANCODE_LSHIFT = 225

    static bool was_lmb_down = false;
    bool is_lmb_down = input::is_mouse_down(1);

    mouse_e.button = 1; // Left Button

    if (is_lmb_down && !was_lmb_down)
    {
      tools[active_tool_index]->on_mouse_down(context, mouse_e);
    }
    else if (is_lmb_down && was_lmb_down)
    {
      tools[active_tool_index]->on_mouse_drag(context, mouse_e);
    }
    else if (!is_lmb_down && was_lmb_down)
    {
      tools[active_tool_index]->on_mouse_up(context, mouse_e);
    }
    was_lmb_down = is_lmb_down;

    // Forward all confirmed key presses to the tool
    // We iterate over all scancodes to find what was pressed this frame.
    // 512 checks is negligible.
    bool shift = input::is_key_down(SDL_SCANCODE_LSHIFT) ||
                 input::is_key_down(SDL_SCANCODE_RSHIFT);
    bool ctrl = input::is_key_down(SDL_SCANCODE_LCTRL) ||
                input::is_key_down(SDL_SCANCODE_RCTRL);
    bool alt = input::is_key_down(SDL_SCANCODE_LALT) ||
               input::is_key_down(SDL_SCANCODE_RALT);

    //@FIXME: This is crazy, why can't we forward the key events from the input
    // system?
    for (int scancode = 0; scancode < SDL_NUM_SCANCODES; ++scancode)
    {
      if (input::is_key_pressed(scancode))
      {
        key_event_t key_e;
        key_e.scancode = scancode;
        key_e.shift_down = shift;
        key_e.ctrl_down = ctrl;
        key_e.alt_down = alt;
        key_e.repeat = false; // input::is_key_pressed checks for new press only

        tools[active_tool_index]->on_key_down(context, key_e);
      }
    }
  }
}

void ToolEditorState::render_ui()
{
  ImGui::Begin("Toolbox");

  if (ImGui::Button("Select"))
    switch_tool(0);
  if (ImGui::Button("Place"))
    switch_tool(1);
  if (ImGui::Button("Sculpt"))
    switch_tool(2);

  ImGui::Separator();
  ImGui::Text("Active Tool: %d", active_tool_index);

  if (ImGui::Button("Back to Menu"))
  {
    state_manager::switch_to(GameStateKind::MainMenu);
  }

  ImGui::End();

  // Draw Tool UI (e.g. selection rectangle)
  if (active_tool_index >= 0 && active_tool_index < (int)tools.size())
  {
    tools[active_tool_index]->on_draw_ui(context);
  }
}

void ToolEditorState::render_3d(VkCommandBuffer cmd)
{
  renderer::render_view_t view_def;
  view_def.viewport = {{0, 0}, {1, 1}};
  view_def.camera = camera;

  // We need to bind pipeline/viewport first?
  // renderer::render_view probably does a lot.
  // We can just rely on `BeginFrame` having set up stuff or use `DrawAABB`
  // directly. `DrawAABB` likely binds pipeline if needed? No, usually handled
  // by caller or specific flow. `EditorState::render_3d` calls `draw_grid` etc.

  // Setup render view (binds pipeline, updates matrices, etc.)
  ecs::Registry reg; // Empty registry for now
  renderer::render_view(cmd, view_def, reg);

  // Set viewport explicitly if needed (render_view might do it, but good to be
  // sure for overlay)
  renderer::set_viewport(cmd, view_def.viewport);

  // Draw Grid
  {
    int grid_size = 50;
    float step = 1.0f;
    uint32_t color = 0x44FFFFFF;        // Faint white
    uint32_t axis_color_x = 0xFF0000FF; // Red
    uint32_t axis_color_z = 0xFFFF0000; // Blue

    for (int i = -grid_size; i <= grid_size; ++i)
    {
      if (i == 0)
        continue; // Skip axis lines for now

      float p = (float)i * step;
      // Z-lines (vary X)
      renderer::DrawLine(cmd, {-grid_size * step, 0, p},
                         {grid_size * step, 0, p}, color);
      // X-lines (vary Z)
      renderer::DrawLine(cmd, {p, 0, -grid_size * step},
                         {p, 0, grid_size * step}, color);
    }

    // Axes
    renderer::DrawLine(cmd, {-grid_size * step, 0, 0}, {grid_size * step, 0, 0},
                       axis_color_x);
    renderer::DrawLine(cmd, {0, 0, -grid_size * step}, {0, 0, grid_size * step},
                       axis_color_z);
  }

  // Draw map elements
  for (const auto &geo : map.static_geometry)
  {
    std::visit(
        [&](auto &&shape)
        {
          using T = std::decay_t<decltype(shape)>;
          if constexpr (std::is_same_v<T, shared::aabb_t>)
          {
            renderer::DrawAABB(cmd, shape.center - shape.half_extents,
                               shape.center + shape.half_extents, 0xFFFFFFFF);
          }
          else if constexpr (std::is_same_v<T, shared::wedge_t>)
          {
            // Simplified wireframe for now: draw bounds
            renderer::DrawAABB(cmd, shape.center - shape.half_extents,
                               shape.center + shape.half_extents, 0xFFFFFFFF);
          }
          else if constexpr (std::is_same_v<T, shared::mesh_t>)
          {
            renderer::DrawAABB(
                cmd, shape.local_aabb.center - shape.local_aabb.half_extents,
                shape.local_aabb.center + shape.local_aabb.half_extents,
                0xFF00FFFF);
          }
        },
        geo.data);
  }

  // Draw Tool Overlay
  VulkanOverlayRenderer overlay(cmd);
  if (active_tool_index >= 0 && active_tool_index < (int)tools.size())
  {
    tools[active_tool_index]->on_draw_overlay(context, overlay);
  }
}

void ToolEditorState::update_bvh()
{
  // Rebuild BVH from map geometry
  std::vector<BVH_Input> inputs;
  inputs.reserve(map.static_geometry.size());

  for (size_t i = 0; i < map.static_geometry.size(); ++i)
  {
    const auto &geo = map.static_geometry[i];
    shared::aabb_bounds_t bounds = shared::get_bounds(geo);

    BVH_Input input;
    input.aabb.min = bounds.min;
    input.aabb.max = bounds.max;
    input.id.type = Collision_Id::Type::Static_Geometry;
    input.id.index = (uint32_t)i;

    inputs.push_back(input);
  }

  bvh = build_bvh(inputs);
}

} // namespace client
