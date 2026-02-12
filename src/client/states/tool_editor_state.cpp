#include "tool_editor_state.hpp"
#include "../../shared/asset.hpp"
#include "../../shared/entities/player_entity.hpp"
#include "../../shared/entities/static_entities.hpp"
#include "../editor/editor_entity.hpp"
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
#include <cstring>
#include <fstream>

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

  // Load map from last_map.txt
  bool map_loaded = false;
  std::ifstream f("last_map.txt");
  if (f.is_open())
  {
    log_terminal("Loading map from last_map.txt");
    std::string line;
    std::getline(f, line);
    log_terminal(line);
    map_loaded = load_map(line, map);
    if (!map_loaded)
    {
      log_terminal("Failed to load map");
    }
  }

  // Initialize map with a floor
  if (!map_loaded)
  {
    map.name = "Tool Editor Map";
    auto floor_ent = std::make_shared<::network::AABB_Entity>();
    floor_ent->center = {0, -2.0f, 0};
    floor_ent->half_extents = {10.0f, 0.5f, 10.0f};

    shared::entity_placement_t placement;
    placement.entity = floor_ent;
    placement.position = {0, -2.0f, 0};
    placement.scale = {1, 1, 1};
    placement.rotation = {0, 0, 0};

    map.entities.push_back(placement);
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
  context.editor_entities = &editor_entities;
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
  context.editor_entities = &editor_entities;
  context.bvh = &bvh;
  context.geometry_updated = &geometry_updated_flag;
  context.transaction_system = &transaction_system;
  context.time += dt;
  viewport = transform_viewport_state();

  static bool was_lmb_down = false;
  static bool tool_processing_mouse = false;

  if (ImGui::GetIO().WantCaptureMouse && !tool_processing_mouse)
  {
    // Use a ray that won't hit anything to prevent hovering
    // Origin far away, direction pointing away
    viewport.mouse_ray.origin = {0, 1e20f, 0};
    viewport.mouse_ray.dir = {0, 1.0f, 0};
  }

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
    mouse_e.button = 1;       // Left Button

    bool is_lmb_down = input::is_mouse_down(1);

    if (is_lmb_down && !was_lmb_down)
    {
      if (!ImGui::GetIO().WantCaptureMouse)
      {
        tool_processing_mouse = true;
        tools[active_tool_index]->on_mouse_down(context, mouse_e);
      }
    }
    else if (is_lmb_down && was_lmb_down)
    {
      if (tool_processing_mouse)
      {
        tools[active_tool_index]->on_mouse_drag(context, mouse_e);
      }
    }
    else if (!is_lmb_down && was_lmb_down)
    {
      if (tool_processing_mouse)
      {
        tools[active_tool_index]->on_mouse_up(context, mouse_e);
        tool_processing_mouse = false;
      }
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
    if (!ImGui::GetIO().WantCaptureKeyboard)
    {
      for (int scancode = 0; scancode < SDL_NUM_SCANCODES; ++scancode)
      {
        if (input::is_key_pressed(scancode))
        {
          key_event_t key_e;
          key_e.scancode = scancode;
          key_e.shift_down = shift;
          key_e.ctrl_down = ctrl;
          key_e.alt_down = alt;
          key_e.repeat =
              false; // input::is_key_pressed checks for new press only

          tools[active_tool_index]->on_key_down(context, key_e);
        }
      }
    }
  }
}

void ToolEditorState::render_ui()
{
  ImGui::Begin("Map Info");
  ImGui::Text("Map: %s", map.name.c_str());

  bool should_open_popup = false;
  if (ImGui::Button("Save Map As..."))
  {
    renderer::draw_announcement("is the gerg ever open?");
    // Popup for Save Map
    should_open_popup = true;
  }
  ImGui::End();

  if (should_open_popup)
  {
    ImGui::OpenPopup("Save Map as");
    should_open_popup = false;
  }

  if (ImGui::BeginPopupModal("Save Map as", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize))
  {
    renderer::draw_announcement("is the opup ever open?");

    static char filename_buf[128] = "map.source";

    if (ImGui::IsWindowAppearing())
    {
      if (!map.name.empty())
      {
        strncpy(filename_buf, map.name.c_str(), sizeof(filename_buf) - 1);
        filename_buf[sizeof(filename_buf) - 1] = '\0';
      }
    }

    ImGui::InputText("Filename", filename_buf, sizeof(filename_buf));

    if (ImGui::Button("Save", ImVec2(120, 0)))
    {
      if (shared::save_map(filename_buf, map))
      {
        map.name = filename_buf;

        std::ofstream last_map("last_map.txt");
        if (last_map.is_open())
        {
          last_map << filename_buf;
          last_map.close();
        }
      }
      else
      {
        std::cerr << "Failed to save map!" << std::endl;
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0)))
    {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

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
  for (const auto &placement : map.entities)
  {
    if (!placement.entity)
      continue;

    auto *ent_ptr = placement.entity.get();

    if (auto *aabb = dynamic_cast<::network::AABB_Entity *>(ent_ptr))
    {
      // Use placement position instead of entity's center
      linalg::vec3 center = placement.position;
      renderer::DrawAABB(cmd, center - aabb->half_extents,
                         center + aabb->half_extents, 0xFFFFFFFF);
    }
    else if (auto *wedge = dynamic_cast<::network::Wedge_Entity *>(ent_ptr))
    {
      shared::wedge_t w;
      w.center = placement.position; // Use placement position
      w.half_extents = wedge->half_extents;
      w.orientation = wedge->orientation;
      renderer::draw_wedge(cmd, w, 0xFFFFFFFF);
    }
    else if (auto *mesh = dynamic_cast<::network::Static_Mesh_Entity *>(ent_ptr))
    {
      // Map asset_id to mesh path
      const char *mesh_path = assets::get_mesh_path(mesh->asset_id);

      // Load and render mesh
      if (mesh_path)
      {
        auto mesh_handle = assets::load_mesh(mesh_path);
        if (mesh_handle.valid())
        {
          // Use placement position and scale
          renderer::DrawMesh(cmd, placement.position, placement.scale,
                             mesh_handle, 0xFF00FFFF);
        }
        else
        {
          // Fallback to AABB if mesh fails to load
          linalg::vec3 min = placement.position - placement.scale;
          linalg::vec3 max = placement.position + placement.scale;
          renderer::DrawAABB(cmd, min, max, 0xFF00FFFF);
        }
      }
      else
      {
        // Fallback to AABB if no asset path mapped
        linalg::vec3 min = placement.position - placement.scale;
        linalg::vec3 max = placement.position + placement.scale;
        renderer::DrawAABB(cmd, min, max, 0xFF00FFFF);
      }
    }
    else if (auto *player = dynamic_cast<::network::Player_Entity *>(ent_ptr))
    {
      linalg::vec3 center = placement.position; // Use placement position
      // Pyramid for player start!
      // Base:
      linalg::vec3 p0 = {center.x - 0.5f, center.y - 0.5f, center.z - 0.5f};
      linalg::vec3 p1 = {center.x + 0.5f, center.y - 0.5f, center.z - 0.5f};
      linalg::vec3 p2 = {center.x + 0.5f, center.y - 0.5f, center.z + 0.5f};
      linalg::vec3 p3 = {center.x - 0.5f, center.y - 0.5f, center.z + 0.5f};

      // Top:
      linalg::vec3 p4 = {center.x, center.y + 0.5f, center.z};

      uint32_t color = 0xFFFFFFFF;
      renderer::DrawLine(cmd, p0, p1, color);
      renderer::DrawLine(cmd, p1, p2, color);
      renderer::DrawLine(cmd, p2, p3, color);
      renderer::DrawLine(cmd, p3, p0, color);

      renderer::DrawLine(cmd, p0, p4, color);
      renderer::DrawLine(cmd, p1, p4, color);
      renderer::DrawLine(cmd, p2, p4, color);
      renderer::DrawLine(cmd, p3, p4, color);
    }
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
  editor_entities = build_editor_entities(map);
  bvh = build_editor_bvh(editor_entities);
}

} // namespace client
