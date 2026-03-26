#include "tool_editor_state.hpp"
#include "../../shared/asset.hpp"
#include "../../shared/debug_collision.hpp"
#include "../../shared/entities/player_entity.hpp"
#include "../../shared/entities/displacement_entity.hpp"
#include "../../shared/entities/static_entities.hpp"
#include "../../shared/entities/particle_emitter_entity.hpp"
#include "../../shared/map_baker.hpp"
#include "../editor/editor_entity.hpp"
#include "../editor/tools/pathfinding_test_tool.hpp"
#include "../editor/tools/placement_tool.hpp"
#include "../editor/tools/sculpting_tool.hpp"
#include "../editor/tools/particle_editor_tool.hpp"
#include "../editor/tools/displacement_tool.hpp"
#include "../editor/tools/selection_tool.hpp"
#include "../input.hpp"
#include "../renderer.hpp"
#include "../shared/linalg.hpp"
#include "../shared/math.hpp"
#include "../state_manager.hpp"
#include "SDL_scancode.h"
#include "imgui.h"
#include <SDL.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace client
{

// Returns the absolute path to the maps/ directory, creating it if needed.
// Resolved relative to the executable: <exe_dir>/../../maps/ which puts it at
// the project root when running from a cmake_build/bin/ layout.
static std::string get_maps_dir()
{
  std::filesystem::path base;

  char *sdl_base = SDL_GetBasePath();
  if (sdl_base)
  {
    // exe is at <project>/cmake_build/bin/, so ../../ is the project root
    base = std::filesystem::weakly_canonical(
        std::filesystem::path(sdl_base) / ".." / ".." / "maps");
    SDL_free(sdl_base);
  }
  else
  {
    base = std::filesystem::weakly_canonical("maps");
  }

  std::filesystem::create_directories(base);
  return base.string() + "/";
}

// Returns sorted list of all regular files in the maps directory.
static std::vector<std::string> list_map_files()
{
  std::vector<std::string> files;
  std::error_code ec;
  for (const auto &entry :
       std::filesystem::directory_iterator(get_maps_dir(), ec))
  {
    if (entry.is_regular_file())
      files.push_back(entry.path().filename().string());
  }
  std::sort(files.begin(), files.end());
  return files;
}

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
    renderer::DrawWireAABB(cmd, min, max, color);
  }

  void draw_solid_box(const linalg::vec3 &center,
                      const linalg::vec3 &half_extents, uint32_t color) override
  {
    linalg::vec3 min = center - half_extents;
    linalg::vec3 max = center + half_extents;
    renderer::DrawAABB(cmd, min, max, color);
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

  // Only load from disk on first entry. When returning from play mode the
  // in-memory map is already correct; reloading would discard unsaved edits
  // and could pick up the wrong file if last_map.txt is stale.
  if (map.entities.empty())
  {
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
        log_terminal("Failed to load map");
    }

    if (!map_loaded)
    {
      map.name = "Tool Editor Map";
      auto floor_ent = std::make_shared<::network::AABB_Entity>();
      floor_ent->position = {0, editor::DEFAULT_FLOOR_Y, 0};
      floor_ent->half_extents = {editor::DEFAULT_FLOOR_HALF_W,
                                 editor::DEFAULT_FLOOR_HALF_H,
                                 editor::DEFAULT_FLOOR_HALF_W};
      map.add_entity(floor_ent);
      renderer::draw_announcement("Welcome to the Tool Editor!");
    }
  }

  // Initialize Camera
  camera.x = 0;
  camera.y = 1024;
  camera.z = 10;
  camera.pitch = -30.0f;
  camera.yaw = 0.0f;
  fov = 90.0f;
  aspect = 1.77f; // Will update
  z_near = 0.1f;
  z_far = 16000.0f;

  // Initialize Tools
  if (tools.empty())
  {
    tools.push_back(std::make_unique<Selection_Tool>());
    tools.push_back(std::make_unique<Placement_Tool>());
    tools.push_back(std::make_unique<Sculpting_Tool>());
    tools.push_back(std::make_unique<PathfindingTestTool>());
    tools.push_back(std::make_unique<ParticleEditorTool>());
    tools.push_back(std::make_unique<Displacement_Tool>());
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
  context.grid = &grid_settings;
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
  last_dt = dt;

  if (input::is_key_pressed(SDL_SCANCODE_ESCAPE))
  {
    state_manager::switch_to(GameStateKind::MainMenu);
    return;
  }

  // Update Camera
  ImGuiIO &io = ImGui::GetIO();
  if (!io.WantCaptureMouse)
  {
    float speed = 1200.0f * dt;
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

    if (camera.orthographic)
    {
      if (input::is_key_pressed(SDL_SCANCODE_RIGHT))
        camera.yaw = fmodf(camera.yaw + 90.0f, 360.0f);
      if (input::is_key_pressed(SDL_SCANCODE_LEFT))
        camera.yaw = fmodf(camera.yaw - 90.0f + 360.0f, 360.0f);
    }

    if (input::is_key_pressed(SDL_SCANCODE_RIGHTBRACKET))
    {
      grid_settings.increase();
      char buf[64];
      snprintf(buf, sizeof(buf), "Grid: %.0f", grid_settings.step());
      renderer::draw_announcement(buf);
    }
    if (input::is_key_pressed(SDL_SCANCODE_LEFTBRACKET))
    {
      grid_settings.decrease();
      char buf[64];
      snprintf(buf, sizeof(buf), "Grid: %.0f", grid_settings.step());
      renderer::draw_announcement(buf);
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
  context.grid = &grid_settings;
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
    if (!ImGui::GetIO().WantTextInput)
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

// Returns a new map with all AABB entities split so no two overlap.
// Non-AABB entities are copied unchanged.
// The result map name is "<source_name>_baked".
static shared::map_t bake_map_csg(const shared::map_t &src)
{
  shared::map_t result;

  // Derive baked filename
  std::string base = src.name;
  auto ext_pos = base.rfind('.');
  if (ext_pos != std::string::npos)
    result.name = base.substr(0, ext_pos) + "_baked" + base.substr(ext_pos);
  else
    result.name = base + "_baked";

  // Copy non-AABB entities unchanged
  for (const auto &entry : src.entities)
  {
    if (!entry.entity)
      continue;
    if (!dynamic_cast<network::AABB_Entity *>(entry.entity.get()))
      result.add_entity(entry.entity);
  }

  // Gather AABB inputs
  struct InputAABB
  {
    shared::aabb_t shape;
    network::render_component_t render;
  };
  std::vector<InputAABB> inputs;
  for (const auto &entry : src.entities)
  {
    if (!entry.entity)
      continue;
    auto *aabb = dynamic_cast<network::AABB_Entity *>(entry.entity.get());
    if (!aabb)
      continue;
    shared::aabb_t shape;
    shape.center = aabb->position;
    shape.half_extents = aabb->half_extents;
    inputs.push_back({shape, aabb->render});
  }

  // CSG union: each new AABB is clipped against all already-placed ones
  // so the final set has zero overlaps. Earlier AABBs win where they overlap.
  struct BakedPiece
  {
    shared::aabb_t shape;
    network::render_component_t render;
  };
  std::vector<BakedPiece> baked;

  for (const auto &inp : inputs)
  {
    std::vector<shared::aabb_t> pieces = {inp.shape};
    for (const auto &existing : baked)
    {
      std::vector<shared::aabb_t> clipped;
      for (const auto &piece : pieces)
      {
        auto sub = shared::subtract_aabb(piece, existing.shape);
        clipped.insert(clipped.end(), sub.begin(), sub.end());
      }
      pieces = std::move(clipped);
    }
    for (const auto &piece : pieces)
      baked.push_back({piece, inp.render});
  }

  // Emit one AABB_Entity per piece
  for (const auto &piece : baked)
  {
    auto ent = std::make_shared<network::AABB_Entity>();
    ent->position = piece.shape.center;
    ent->half_extents = piece.shape.half_extents;
    ent->render = piece.render;
    result.add_entity(ent);
  }

  return result;
}

void ToolEditorState::render_ui()
{
  // Ctrl+S / Cmd+S — quick save with CSG simplification
  if (input::is_key_pressed(SDL_SCANCODE_S) &&
      (input::is_key_down(SDL_SCANCODE_LCTRL) ||
       input::is_key_down(SDL_SCANCODE_RCTRL) ||
       input::is_key_down(SDL_SCANCODE_LGUI) ||
       input::is_key_down(SDL_SCANCODE_RGUI)))
  {
    std::string full_path = get_maps_dir() + map.name;
    auto simplified = bake_map_csg(map);
    simplified.name = map.name;
    simplified.navmesh = map.navmesh;
    if (shared::save_map(full_path, simplified))
    {
      map = std::move(simplified);
      geometry_updated_flag = true;

      std::ofstream last_map_f("last_map.txt");
      if (last_map_f.is_open())
        last_map_f << full_path;
      renderer::draw_announcement("Saved & simplified!");
    }
    else
    {
      renderer::draw_announcement("Save failed!");
    }
  }

  ImGui::Begin("Map Info", nullptr, ImGuiWindowFlags_NoNav);
  ImGui::Text("Map: %s", map.name.c_str());

  bool should_open_popup = false;
  bool should_open_load_popup = false;
  bool should_open_new_map_popup = false;

  if (ImGui::Button("Save Map As..."))
  {
    renderer::draw_announcement("is the gerg ever open?");
    // Popup for Save Map
    should_open_popup = true;
  }

  if (ImGui::Button("Load Map..."))
    should_open_load_popup = true;

  if (ImGui::Button("New Map"))
    should_open_new_map_popup = true;

  ImGui::Checkbox("Solid Entities", &draw_entities_solid);
  ImGui::Checkbox("Hide Geometry", &hide_geometry);
  ImGui::Checkbox("Show Grid", &show_grid);

  ImGui::Separator();

  // Navmesh status
  if (map.navmesh.valid())
  {
    int num_islands = 0;
    for (const auto &p : map.navmesh.polygons)
      if (p.island >= num_islands) num_islands = p.island + 1;
    ImGui::TextColored({0.2f, 1.f, 0.4f, 1.f}, "Navmesh: %d vertices, %d polygons, %d islands",
                       (int)map.navmesh.vertices.size(),
                       (int)map.navmesh.polygons.size(),
                       num_islands);
  }
  else
  {
    ImGui::TextDisabled("Navmesh: not baked");
  }

  constexpr float navmesh_cell_size_min = 128.f;
  constexpr float navmesh_cell_size_max = 512.f;
  ImGui::SliderFloat("Cell size", &navmesh_cell_size, navmesh_cell_size_min, navmesh_cell_size_max, "%.0f");

  if (ImGui::Button("Bake Navmesh"))
  {
    std::string full_path = get_maps_dir() + map.name;
    map.navmesh = {};
    shared::bake_map(map, navmesh_cell_size);  // raw triangles only
    m_raw_navmesh = map.navmesh;               // save before simplification
    m_simplify_steps = 0;
    shared::simplify_navmesh(map.navmesh);     // full simplify
    if (shared::save_navmesh_sidecar(full_path, map.navmesh))
      renderer::draw_announcement("Navmesh baked!");
    else
      renderer::draw_announcement("Navmesh bake failed (save map first?)");
  }

  // Step-by-step simplification for debugging.
  if (m_raw_navmesh.valid())
  {
    ImGui::SameLine();
    if (ImGui::Button("Simplify Step"))
    {
      map.navmesh = m_raw_navmesh;
      ++m_simplify_steps;
      shared::simplify_navmesh(map.navmesh, m_simplify_steps);
    }
    ImGui::SameLine();
    ImGui::Text("(step %d)", m_simplify_steps);
  }

  bool show_navmesh = debug_collision::debug_show_navmesh.Get();
  if (ImGui::Checkbox("Show Navmesh", &show_navmesh))
    debug_collision::debug_show_navmesh.Set(show_navmesh);

  ImGui::End();

  if (should_open_popup)
  {
    ImGui::OpenPopup("Save Map as");
    should_open_popup = false;
  }

  if (should_open_load_popup)
  {
    ImGui::OpenPopup("Load Map");
    should_open_load_popup = false;
  }

  if (should_open_new_map_popup)
  {
    ImGui::OpenPopup("New Map");
    should_open_new_map_popup = false;
  }

  if (ImGui::BeginPopupModal("Load Map", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    static std::vector<std::string> map_files;
    static int selected_idx = -1;

    if (ImGui::IsWindowAppearing())
    {
      map_files = list_map_files();
      selected_idx = -1;
    }

    ImGui::Text("maps/");
    if (ImGui::BeginChild("##maplist", ImVec2(320, 200), true))
    {
      if (map_files.empty())
      {
        ImGui::TextDisabled("(no files in maps/)");
      }
      else
      {
        for (int i = 0; i < (int)map_files.size(); ++i)
        {
          bool is_sel = (i == selected_idx);
          if (ImGui::Selectable(map_files[i].c_str(), is_sel))
            selected_idx = i;
        }
      }
    }
    ImGui::EndChild();

    bool can_load = selected_idx >= 0 && selected_idx < (int)map_files.size();
    if (!can_load)
      ImGui::BeginDisabled();
    if (ImGui::Button("Load", ImVec2(120, 0)))
    {
      std::string full_path = get_maps_dir() + map_files[selected_idx];
      shared::map_t new_map;
      if (shared::load_map(full_path, new_map))
      {
        map = std::move(new_map);
        transaction_system = Transaction_System{};
        geometry_updated_flag = true;

        std::ofstream last_map_f("last_map.txt");
        if (last_map_f.is_open())
          last_map_f << full_path;

        renderer::draw_announcement("Map loaded!");
      }
      else
      {
        renderer::draw_announcement("Failed to load map!");
      }
      ImGui::CloseCurrentPopup();
    }
    if (!can_load)
      ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0)))
      ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
  }

  if (ImGui::BeginPopupModal("Save Map as", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize))
  {
    static char filename_buf[128] = "map.source";

    if (ImGui::IsWindowAppearing())
    {
      // Pre-fill with just the filename part of the current map name
      std::string leaf = std::filesystem::path(map.name).filename().string();
      if (leaf.empty())
        leaf = "map.source";
      strncpy(filename_buf, leaf.c_str(), sizeof(filename_buf) - 1);
      filename_buf[sizeof(filename_buf) - 1] = '\0';
    }

    ImGui::Text("maps/");
    ImGui::SameLine();
    ImGui::InputText("##savename", filename_buf, sizeof(filename_buf));

    if (ImGui::Button("Save", ImVec2(120, 0)))
    {
      std::string full_path = get_maps_dir() + filename_buf;
      auto simplified = bake_map_csg(map);
      simplified.name = filename_buf;
      simplified.navmesh = map.navmesh;
      if (shared::save_map(full_path, simplified))
      {
        map = std::move(simplified);
        geometry_updated_flag = true;

        std::ofstream last_map_f("last_map.txt");
        if (last_map_f.is_open())
          last_map_f << full_path;

        renderer::draw_announcement("Saved & simplified!");
      }
      else
      {
        renderer::draw_announcement("Save failed!");
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0)))
      ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
  }

  if (ImGui::BeginPopupModal("New Map", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::Text("Save current map as backup and open a new empty map?");
    ImGui::TextDisabled("Backup: maps/%s_backup", map.name.c_str());

    if (ImGui::Button("OK", ImVec2(120, 0)))
    {
      // Save backup of current map
      std::string backup_path = get_maps_dir() + map.name + "_backup";
      shared::save_map(backup_path, map);

      // Reset to a new empty map with a default floor
      map = shared::map_t{};
      map.name = "new_map.source";
      auto floor_ent = std::make_shared<::network::AABB_Entity>();
      floor_ent->position = {0, editor::DEFAULT_FLOOR_Y, 0};
      floor_ent->half_extents = {editor::DEFAULT_FLOOR_HALF_W,
                                 editor::DEFAULT_FLOOR_HALF_H,
                                 editor::DEFAULT_FLOOR_HALF_W};
      map.add_entity(floor_ent);

      transaction_system = Transaction_System{};
      geometry_updated_flag = true;

      // Clear last_map.txt so the editor doesn't reload the old map on restart
      std::ofstream("last_map.txt");

      renderer::draw_announcement("New map created!");
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0)))
      ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
  }

  ImGui::Begin("Toolbox", nullptr, ImGuiWindowFlags_NoNav);

  if (ImGui::Button("Select"))
    switch_tool(0);
  if (ImGui::Button("Place"))
    switch_tool(1);
  if (ImGui::Button("Sculpt"))
    switch_tool(2);
  if (ImGui::Button("Pathfinding"))
    switch_tool(3);
  if (ImGui::Button("Particles"))
    switch_tool(4);
  if (ImGui::Button("Displacement"))
    switch_tool(5);

  ImGui::Separator();
  ImGui::Text("Active Tool: %d", active_tool_index);

  ImGui::Separator();
  if (ImGui::Button("Play"))
  {
    state_manager::switch_to(GameStateKind::Play);
  }

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
  if (show_grid)
  {
    constexpr int count = editor::MAJOR_GRID_COUNT;
    constexpr float major = editor::MAJOR_GRID_STEP;
    float minor = grid_settings.step();
    float extent = count * major;

    uint32_t major_color = 0x44FFFFFF;      // Faint white
    uint32_t minor_color = 0x22FFFFFF;      // Fainter
    uint32_t axis_color_x = 0xFF0000FF;     // Red
    uint32_t axis_color_z = 0xFFFF0000;     // Blue

    // Subdivision lines (only if grid step < major grid)
    if (minor < major)
    {
      int total = (int)(extent / minor);
      for (int i = -total; i <= total; ++i)
      {
        float p = (float)i * minor;
        // Skip lines that fall on the major grid (drawn below)
        if (std::fmod(std::abs(p), major) < 0.01f)
          continue;
        renderer::DrawLine(cmd, {-extent, 0, p}, {extent, 0, p}, minor_color);
        renderer::DrawLine(cmd, {p, 0, -extent}, {p, 0, extent}, minor_color);
      }
    }

    // Major grid lines
    for (int i = -count; i <= count; ++i)
    {
      if (i == 0)
        continue;
      float p = (float)i * major;
      renderer::DrawLine(cmd, {-extent, 0, p}, {extent, 0, p}, major_color);
      renderer::DrawLine(cmd, {p, 0, -extent}, {p, 0, extent}, major_color);
    }

    // Axes
    renderer::DrawLine(cmd, {-extent, 0, 0}, {extent, 0, 0}, axis_color_x);
    renderer::DrawLine(cmd, {0, 0, -extent}, {0, 0, extent}, axis_color_z);
  }

  // Draw map elements
  if (!hide_geometry)
  for (const auto &entry : map.entities)
  {
    const auto &ent = entry.entity;
    if (!ent)
      continue;

    // Try to render via the entity's render component
    const auto *rc = ent->get_component<network::render_component_t>();

    if (rc && rc->visible)
    {
      const char *mesh_path = nullptr;

      // Check if mesh_path string is set (for primitives or direct paths)
      if (rc->mesh_path.length > 0)
      {
        mesh_path = rc->mesh_path.c_str();
      }
      // Fallback to mesh_id lookup
      else if (rc->mesh_id >= 0)
      {
        mesh_path = assets::get_mesh_path(rc->mesh_id);
      }

      if (mesh_path)
      {
        // Check if it's a primitive (starts with __primitive_)
        assets::asset_handle_t<assets::mesh_asset_t> mesh_handle;
        if (std::strncmp(mesh_path, "__primitive_", 12) == 0)
        {
          // Extract primitive name (after "__primitive_")
          const char *prim_name = mesh_path + 12;
          mesh_handle = assets::get_primitive_mesh(prim_name);
        }
        else
        {
          // Regular OBJ file
          mesh_handle = assets::load_mesh(mesh_path);
        }

        if (mesh_handle.valid())
        {
          if (rc->is_wireframe)
            renderer::DrawMeshWireframe(cmd, ent->position, rc->scale,
                                        mesh_handle, 0xFFFFFFFF, ent->orientation + rc->rotation);
          else
            renderer::DrawMesh(cmd, ent->position, rc->scale,
                               mesh_handle, 0xFFFFFFFF, ent->orientation + rc->rotation);
          continue;
        }
      }
    }

    // Fallback: entity-specific primitive rendering
    if (auto *aabb = dynamic_cast<::network::AABB_Entity *>(ent.get()))
    {
      renderer::DrawAABB(cmd, aabb->position - aabb->half_extents,
                         aabb->position + aabb->half_extents, 0xFFFFFFFF,
                         /*as_wireframe=*/!draw_entities_solid,
                         /*random_color=*/draw_entities_solid,
                         /*random_seed=*/entry.uid);
    }
    else if (auto *wedge = dynamic_cast<::network::Wedge_Entity *>(ent.get()))
    {
      shared::wedge_t w;
      w.center = wedge->position;
      w.half_extents = wedge->half_extents;
      w.orientation = wedge->orientation;
      renderer::draw_wedge(cmd, w, 0xFFFFFFFF);
    }
    else if (auto *disp = dynamic_cast<::network::Displacement_Entity *>(ent.get()))
    {
      std::string disp_key =
          "__displacement_" + std::to_string(entry.uid);
      auto mesh_handle = assets::find_mesh_in_cache(disp_key.c_str());
      if (!mesh_handle.valid())
      {
        auto mesh = network::generate_displacement_mesh(*disp);
        mesh_handle =
            assets::register_dynamic_mesh(disp_key.c_str(), std::move(mesh));
      }
      if (mesh_handle.valid())
      {
        renderer::DrawMeshMaterial(cmd, disp->position, {1, 1, 1},
                                   mesh_handle, {0.6f, 0.6f, 0.6f},
                                   renderer::ShaderType::Lit,
                                   disp->orientation);
      }
    }
    else if (dynamic_cast<::network::Static_Mesh_Entity *>(ent.get()))
    {
      // No mesh in render component — draw placeholder AABB
      auto bounds = shared::compute_entity_bounds(ent.get());
      renderer::DrawAABB(cmd, bounds.min, bounds.max, 0xFF00FFFF,
                         /*as_wireframe=*/!draw_entities_solid,
                         /*random_color=*/draw_entities_solid,
                         /*random_seed=*/entry.uid);
    }
    else if (auto *player = dynamic_cast<::network::Player_Entity *>(ent.get()))
    {
      const char *mesh_path = assets::get_mesh_path(2); // pyramid
      if (mesh_path)
      {
        auto mesh_handle = assets::load_mesh(mesh_path);
        if (mesh_handle.valid())
        {
          renderer::DrawMeshWireframe(cmd, ent->position, {32, 72, 32},
                                      mesh_handle, 0xFFFFFFFF, ent->orientation);
        }
      }
    }
    else if (dynamic_cast<::network::Player_Spawn_Entity *>(ent.get()))
    {
      // Draw player hull outline + upward spike (same visual as placement ghost)
      const linalg::vec3 hull{network::player_half_width, network::player_half_height, network::player_half_width};
      renderer::DrawWireAABB(cmd, ent->position - hull, ent->position + hull, 0xFF8800FF);
      renderer::DrawLine(cmd, ent->position, ent->position + linalg::vec3{0, 48, 0}, 0xFF8800FF);
    }
  }

  // Draw navmesh triangle wireframes, colored by island ID.
  // Suppressed when the pathfinding tool is active — it draws the navmesh itself.
  if (debug_collision::debug_show_navmesh.Get() && map.navmesh.valid() && active_tool_index != 3)
  {
    const navmesh_t &nav = map.navmesh;
    constexpr float y_lift = 2.f;

    static constexpr uint32_t island_colors[] = {
      0xFFFFFF00, // ABGR: cyan
      0xFF00FFFF, // yellow
      0xFF00FF00, // green
      0xFFFF00FF, // magenta
    };

    for (const auto &poly : nav.polygons)
    {
      uint32_t color = island_colors[poly.island % 4];
      const int N = (int)poly.verts.size();
      for (int e = 0; e < N; ++e)
      {
        vec3f a = nav.vertices[poly.verts[e          ]].pos;
        vec3f b = nav.vertices[poly.verts[(e + 1) % N]].pos;
        a.y += y_lift;
        b.y += y_lift;
        renderer::DrawLine(cmd, a, b, color);
      }
    }

    // Draw each vertex as a small cross so winding/deduplication is visible.
    constexpr float r = 2.f;
    constexpr uint32_t vert_color = 0xFFFFFFFF; // white
    for (const auto &v : nav.vertices)
    {
      vec3f p = v.pos; p.y += y_lift;
      renderer::DrawLine(cmd, {p.x - r, p.y, p.z}, {p.x + r, p.y, p.z}, vert_color);
      renderer::DrawLine(cmd, {p.x, p.y, p.z - r}, {p.x, p.y, p.z + r}, vert_color);
    }
  }

  // Draw particle emitters
  for (const auto &entry : map.entities)
  {
    auto *pe = dynamic_cast<network::Particle_Emitter_Entity *>(entry.entity.get());
    if (!pe) continue;

    renderer::particle_emitter_params_t p{};
    p.entity_id = pe->entity_id;
    p.position = pe->position;
    p.delta_time = last_dt;
    p.emit_rate = pe->emit_rate;
    p.max_particles = pe->max_particles;
    p.lifetime_min = pe->lifetime_min;
    p.lifetime_max = pe->lifetime_max;
    p.velocity_min = pe->velocity_min;
    p.velocity_max = pe->velocity_max;
    p.spread = pe->spread;
    p.gravity = pe->gravity;
    p.drag = pe->drag;
    p.size_start = pe->size_start;
    p.size_end = pe->size_end;
    p.rotation_speed_min = pe->rotation_speed_min;
    p.rotation_speed_max = pe->rotation_speed_max;
    p.color_start = pe->color_start;
    p.color_end = pe->color_end;
    p.alpha_start = pe->alpha_start;
    p.alpha_end = pe->alpha_end;
    renderer::DrawParticles(cmd, p);
  }

  // Draw Tool Overlay
  VulkanOverlayRenderer overlay(cmd);
  if (active_tool_index >= 0 && active_tool_index < (int)tools.size())
  {
    tools[active_tool_index]->on_draw_overlay(context, overlay);
  }
}

void ToolEditorState::pre_render(VkCommandBuffer cmd)
{
  for (const auto &entry : map.entities)
  {
    auto *pe = dynamic_cast<network::Particle_Emitter_Entity *>(entry.entity.get());
    if (!pe) continue;

    renderer::particle_emitter_params_t p{};
    p.entity_id = pe->entity_id;
    p.position = pe->position;
    p.delta_time = last_dt;
    p.emit_rate = pe->emit_rate;
    p.max_particles = pe->max_particles;
    p.lifetime_min = pe->lifetime_min;
    p.lifetime_max = pe->lifetime_max;
    p.velocity_min = pe->velocity_min;
    p.velocity_max = pe->velocity_max;
    p.spread = pe->spread;
    p.gravity = pe->gravity;
    p.drag = pe->drag;
    p.size_start = pe->size_start;
    p.size_end = pe->size_end;
    p.rotation_speed_min = pe->rotation_speed_min;
    p.rotation_speed_max = pe->rotation_speed_max;
    p.color_start = pe->color_start;
    p.color_end = pe->color_end;
    p.alpha_start = pe->alpha_start;
    p.alpha_end = pe->alpha_end;
    renderer::UpdateParticles(cmd, p);
  }
}

void ToolEditorState::update_bvh()
{
  bvh = build_editor_bvh(map);
}

} // namespace client
