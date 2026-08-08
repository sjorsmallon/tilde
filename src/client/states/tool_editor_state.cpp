#include "../../shared/entities/entity_reflection.hpp"
#include "tool_editor_state.hpp"
#include "../../shared/asset.hpp"
#include "../../shared/debug_collision.hpp"
#include "../../shared/map_baker.hpp"
#include "../editor/editor_bvh.hpp"
#include "../editor/entity_editor_traits.hpp"
#include "../editor/geometry_editor.hpp"
#include "../editor/tools/pathfinding_test_tool.hpp"
#include "../editor/tools/placement_tool.hpp"
#include "../editor/tools/sculpting_tool.hpp"
#include "../editor/tools/particle_editor_tool.hpp"
#include "../editor/tools/displacement_tool.hpp"
#include "../editor/tools/selection_tool.hpp"
#include "../console.hpp"
#include "../input.hpp"
#include "../renderer.hpp"
#include "../shared/linalg.hpp"
#include "../shared/math.hpp"
#include "../state_manager.hpp"
#include "imgui.h"
#include <SDL_filesystem.h>
#include <SDL_stdinc.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include "../../shared/cvars/generated/cvars_generated.hpp"

namespace client
{

// Internal client hook (defined in client_impl.cpp). Invokes the integrated
// launcher's server::reload_map. Returns false if no hook is installed (e.g.
// in a hypothetical dedicated/networked client build) — caller should treat
// that as "server-side reload not available" but otherwise continue.
bool invoke_server_map_reload_hook(const std::string &map_path);

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

// Copy `path` to `path + ".bak"`, overwriting any existing .bak. No-op if
// `path` doesn't exist. Errors are logged but non-fatal — a missed backup is
// strictly less bad than blocking a save the user asked for.
static void rotate_backup_file(const std::string &path)
{
  std::error_code ec;
  if (!std::filesystem::exists(path, ec))
    return;
  std::filesystem::copy_file(
      path, path + ".bak",
      std::filesystem::copy_options::overwrite_existing, ec);
  if (ec)
    log_error("Backup failed for '{}': {}", path, ec.message());
}

// Single funnel for getting an editor map onto disk and into the running
// server's session. Used by Ctrl+S, "Save Map As...", and the play button so
// all three paths produce the same on-disk + in-memory state.
//
// Steps:
//   1. Rotate <full_path> to <full_path>.bak (if file exists).
//   2. Write `map` to <full_path>.
//   3. Update last_map.txt so the next boot picks up the same file.
//   4. Ask the integrated server to reload from <full_path> so its session
//      matches what we just saved (fixes the long-standing "save then play
//      runs the old map" bug).
//
// Returns true on success. On failure the on-disk state is left untouched
// (well, .bak may have been refreshed — but that's by design).
static bool commit_map_to_disk(const shared::map_t &map,
                                const std::string &full_path)
{
  rotate_backup_file(full_path);

  if (!shared::save_map(full_path, map))
  {
    log_error("save_map failed for '{}'", full_path);
    return false;
  }

  std::ofstream last_map_f("last_map.txt");
  if (last_map_f.is_open())
    last_map_f << full_path;
  else
    log_error("Could not write last_map.txt");

  // Server reload is best-effort: if the hook isn't installed (non-integrated
  // build) we still want the save itself to succeed.
  if (!invoke_server_map_reload_hook(full_path))
    log_terminal("Server map-reload hook not installed; server session may be stale.");

  return true;
}

// On-load backup: snapshot the file as it was when it was opened so the
// editor can roll back if the user trashes the in-memory map and saves over
// it (which would otherwise leave only the previous-save .bak from
// rotate_backup_file). Safe to call on any path; no-op if missing.
static void snapshot_on_load(const std::string &full_path)
{
  rotate_backup_file(full_path);
}

// Concrete renderer adapter
struct VulkanOverlayRenderer : public overlay_renderer_t
{
  VkCommandBuffer cmd;

  VulkanOverlayRenderer(VkCommandBuffer c) : cmd(c) {}

  VkCommandBuffer get_command_buffer() override { return cmd; }

  void draw_line(const linalg::vec3 &start, const linalg::vec3 &end,
                 color_t color) override
  {
    renderer::draw_line(cmd, start, end, color);
  }

  void draw_wire_box(const linalg::vec3 &center,
                     const linalg::vec3 &half_extents, color_t color) override
  {
    linalg::vec3 min = center - half_extents;
    linalg::vec3 max = center + half_extents;
    renderer::draw__wire_AABB(cmd, min, max, color);
  }

  void draw_solid_box(const linalg::vec3 &center,
                      const linalg::vec3 &half_extents, color_t color) override
  {
    linalg::vec3 min = center - half_extents;
    linalg::vec3 max = center + half_extents;
    renderer::draw_AABB(cmd, min, max, color);
  }

  void draw_circle(const linalg::vec3 &center, float radius,
                   const linalg::vec3 &normal, color_t color) override
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
                 color_t color) override
  {
    // Not supported in immediate 3d cmd buffer easily without font texture
    // binding Could use ImGui::GetBackgroundDrawList()->AddText but that
    // requires projection
  }
};

// A new map isn't empty — it gets a floor to stand on, or the first thing you
// place has nothing to land against.
static void add_default_floor(shared::map_t &map)
{
  shared::box_geometry_t floor;
  floor.position = {0, editor::DEFAULT_FLOOR_Y, 0};
  floor.half_extents = {editor::DEFAULT_FLOOR_HALF_W, editor::DEFAULT_FLOOR_HALF_H,
                        editor::DEFAULT_FLOOR_HALF_W};
  map.add_geometry(floor);
}

void Tool_Editor_State::on_enter()
{
  log_terminal("Entered Tool_Editor_State");

  // Only load from disk on first entry. When returning from play mode the
  // in-memory map is already correct; reloading would discard unsaved edits
  // and could pick up the wrong file if last_map.txt is stale.
  if (map.object_count() == 0)
  {
    bool map_loaded = false;
    std::ifstream forward("last_map.txt");
    if (forward.is_open())
    {
      log_terminal("Loading map from last_map.txt");
      std::string line;
      std::getline(forward, line);
      log_terminal(line);
      map_loaded = load_map(line, map);
      if (!map_loaded)
        log_terminal("Failed to load map");
      else
        snapshot_on_load(line);
    }

    if (!map_loaded)
    {
      map.name = "Tool Editor Map";
      add_default_floor(map);
      renderer::draw_announcement("Welcome to the Tool Editor!");
    }
  }

  // Initialize Camera
  camera.position.x = 0;
  camera.position.y = 1024.f;
  camera.position.z = 10;
  camera.pitch = -30.0f;
  camera.yaw = 0.0f;
  camera.fov_degrees = state_manager::get_client_context().cvars->r_fov;
  aspect = 1.77f; // Will update
  z_near = 0.1f;
  z_far = 16000.0f;

  // Initialize Tools
  if (tools.empty())
  {
    tools.push_back(std::make_unique<Selection_Tool>());
    tools.push_back(std::make_unique<Placement_Tool>());
    tools.push_back(std::make_unique<Sculpting_Tool>());
    tools.push_back(std::make_unique<Pathfinding_Test_Tool>());
    tools.push_back(std::make_unique<Particle_Editor_Tool>());
    tools.push_back(std::make_unique<Displacement_Tool>());
  }

  // Enable first tool
  switch_tool(0);

  update_bvh();
}

void Tool_Editor_State::on_exit()
{
  if (active_tool_index >= 0 && active_tool_index < (int)tools.size())
  {
    tools[active_tool_index]->on_disable(context);
  }
}

void Tool_Editor_State::switch_tool(int index)
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
  context.geometry_updated_so_bvh_rebuild_is_needed = &geometry_updated_flag;
  context.grid = &grid_settings;
  context.time = 0; // TODO: Get real time

  if (active_tool_index >= 0 && active_tool_index < (int)tools.size())
  {
    tools[active_tool_index]->on_enable(context);
  }
}

viewport_state_t Tool_Editor_State::transform_viewport_state()
{
  viewport_state_t view;
  view.camera = camera;
  view.camera.orthographic = camera.orthographic; // Redundant if simple copy

  linalg::vec2i mouse = input::mouse_position();

  ImGuiIO &io = ImGui::GetIO();

  float width = io.DisplaySize.x;
  float height = io.DisplaySize.y;

  if (width == 0 || height == 0)
  {
    width = 1280;
    height = 720;
  }

  float x_ndc = (2.0f * mouse.x) / width - 1.0f;
  float y_ndc = 1.0f - (2.0f * mouse.y) / height;

  view.mouse_ray = client::get_pick_ray(camera, x_ndc, y_ndc, width / height);

  view.display_size = {width, height};
  view.aspect_ratio = width / height;

  return view;
}

void Tool_Editor_State::update(float dt)
{
  last_dt = dt;

  // Re-read every frame so `r_fov` from the console takes effect immediately,
  // and so picking and rendering can never be a frame apart on it.
  camera.fov_degrees = state_manager::get_client_context().cvars->r_fov;

  if (input::is_key_pressed(input::key_t::Escape))
  {
    state_manager::switch_to(game_state::main_menu);
    return;
  }

  // Update Camera
  if (!input::ui_wants_mouse())
  {
    input::modifiers_t mods = input::current_modifiers();

    float speed = state_manager::get_client_context().cvars->editor_speed * dt;
    if (mods.shift)
      speed *= 2.0f;

    auto vectors = client::get_orientation_vectors(camera);
    linalg::vec3 forward = vectors.forward;
    linalg::vec3 right = vectors.right;
    linalg::vec3 up = vectors.up;

    if (input::is_key_pressed(input::key_t::Z))
    {
      if (mods.ctrl)
      {
        if (mods.shift)
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

    if (input::is_key_pressed(input::key_t::Y))
    {
      if (mods.ctrl)
      {
        if (transaction_system.can_redo())
        {
          transaction_system.redo(map);
          geometry_updated_flag = true;
        }
      }
    }

    if (input::is_key_pressed(input::key_t::O))
    {
      camera.orthographic = !camera.orthographic;
      if (camera.orthographic)
      {
        camera.yaw = iso_yaw;
        camera.pitch = iso_pitch;
      }
      else
      {
        view_mode = ViewMode::FreeCam;
      }
    }

    // Shift+Space: cycle through axis-aligned views
    if (input::is_key_pressed(input::key_t::Space) && mods.shift)
    {
      switch (view_mode)
      {
      case ViewMode::FreeCam:
        view_mode = ViewMode::TopDown;
        camera.orthographic = true;
        camera.yaw = 0.0f;
        camera.pitch = -89.0f;
        camera.position.y = 1500.f;
        renderer::draw_announcement("Top Down (-Y)");
        break;
      case ViewMode::TopDown:
        view_mode = ViewMode::Front;
        camera.orthographic = true;
        camera.yaw = 0.0f;
        camera.pitch = 0.0f;
        renderer::draw_announcement("Front (+X)");
        break;
      case ViewMode::Front:
        view_mode = ViewMode::Side;
        camera.orthographic = true;
        camera.yaw = 90.0f;
        camera.pitch = 0.0f;
        renderer::draw_announcement("Side (+Z)");
        break;
      case ViewMode::Side:
        view_mode = ViewMode::FreeCam;
        camera.orthographic = false;
        renderer::draw_announcement("Free Cam");
        break;
      }
    }

    if (camera.orthographic && view_mode == ViewMode::FreeCam)
    {
      if (input::is_key_pressed(input::key_t::Arrow_Right))
        camera.yaw = fmodf(camera.yaw + 90.0f, 360.0f);
      if (input::is_key_pressed(input::key_t::Arrow_Left))
        camera.yaw = fmodf(camera.yaw - 90.0f + 360.0f, 360.0f);
    }

    if (input::is_key_pressed(input::key_t::Right_Bracket))
    {
      grid_settings.increase();
      char buffer[64];
      snprintf(buffer, sizeof(buffer), "Grid: %.0f", grid_settings.step());
      renderer::draw_announcement(buffer);
    }
    if (input::is_key_pressed(input::key_t::Left_Bracket))
    {
      grid_settings.decrease();
      char buffer[64];
      snprintf(buffer, sizeof(buffer), "Grid: %.0f", grid_settings.step());
      renderer::draw_announcement(buffer);
    }

    if (input::is_key_down(input::key_t::W))
    {
      if (camera.orthographic)
      {
        camera.position.x += up.x * speed;
        camera.position.y += up.y * speed;
        camera.position.z += up.z * speed;
      }
      else
      {
        camera.position.x += forward.x * speed;
        camera.position.y += forward.y * speed;
        camera.position.z += forward.z * speed;
      }
    }
    if (input::is_key_down(input::key_t::S))
    {
      if (camera.orthographic)
      {
        camera.position.x -= up.x * speed;
        camera.position.y -= up.y * speed;
        camera.position.z -= up.z * speed;
      }
      else
      {
        camera.position.x -= forward.x * speed;
        camera.position.y -= forward.y * speed;
        camera.position.z -= forward.z * speed;
      }
    }
    if (input::is_key_down(input::key_t::D))
    {
      camera.position.x += right.x * speed;
      camera.position.z += right.z * speed;
    }
    if (input::is_key_down(input::key_t::A))
    {
      camera.position.x -= right.x * speed;
      camera.position.z -= right.z * speed;
    }
    if (input::is_key_down(input::key_t::Space) && !mods.shift)
    {
      if (camera.orthographic)
        camera.ortho_height += speed;
      else
        camera.position.y += speed;
    }
    if (input::is_key_down(input::key_t::C))
    {
      if (camera.orthographic)
      {
        camera.ortho_height -= speed;
        if (camera.ortho_height < 1.0f)
          camera.ortho_height = 1.0f;
      }
      else
      {
        camera.position.y -= speed;
      }
    }
    bool tool_captures_kb = active_tool_index >= 0 &&
                            active_tool_index < (int)tools.size() &&
                            tools[active_tool_index]->capture_keyboard();
    if (!tool_captures_kb && input::is_key_down(input::key_t::Q))
    {
      if (!camera.orthographic)
        camera.position.y -= speed;
    }

    const bool console_open = console::get().is_open();
    if (input::is_mouse_down(input::mouse_button_t::Right) && view_mode == ViewMode::FreeCam && !console_open)
    {
      input::set_relative_mouse_mode(true);
      linalg::vec2i delta = input::mouse_delta();
      camera.yaw += delta.x * 0.1f;
      camera.pitch -= delta.y * 0.1f;
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
  context.geometry_updated_so_bvh_rebuild_is_needed = &geometry_updated_flag;
  context.transaction_system = &transaction_system;
  context.grid = &grid_settings;
  context.time += dt;
  viewport = transform_viewport_state();

  static bool was_lmb_down = false;
  static bool tool_processing_mouse = false;

  if (input::ui_wants_mouse() && !tool_processing_mouse)
  {
    // Use a ray that won't hit anything to prevent hovering
    // Origin far away, direction pointing away
    viewport.mouse_ray.origin = {0, 1e20f, 0};
    viewport.mouse_ray.direction = {0, 1.0f, 0};
  }

  if (active_tool_index >= 0 && active_tool_index < (int)tools.size())
  {
    tools[active_tool_index]->on_update(context, viewport, dt);

    input::mouse_event_t mouse_e;
    mouse_e.button = input::mouse_button_t::Left;
    mouse_e.position = input::mouse_position();
    mouse_e.delta = input::mouse_delta();
    mouse_e.mods = input::current_modifiers();

    bool is_lmb_down = input::is_mouse_down(input::mouse_button_t::Left);

    if (is_lmb_down && !was_lmb_down)
    {
      if (!input::ui_wants_mouse())
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

    // Forward this frame's key-down events to the active tool. The input
    // system collects these from the SDL event pump, so no scancode polling.
    if (!input::ui_wants_keyboard())
    {
      for (const input::key_event_t &key_event : input::frame_key_events())
      {
        tools[active_tool_index]->on_key_down(context, key_event);
      }
    }
  }
}

// Returns a new map with all BOX geometry split so no two boxes overlap.
// Everything else — other geometry kinds, and every entity — is copied unchanged.
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

  // CSG is box-only on both the input and the output side, and stays that way:
  // the output is a box brush, so subtracting a displacement or a trigger volume
  // through here would silently drop its payload (heightmap, action_name, ...).
  for (const auto &entry : src.entities)
  {
    if (entry.entity)
      result.add_entity(entry.entity);
  }

  struct input_box_t
  {
    shared::aabb_t shape;
    shared::geometry_surface_t surface;
  };
  std::vector<input_box_t> inputs;

  for (const shared::map_geometry_t &entry : src.geometry)
  {
    const auto *box = std::get_if<shared::box_geometry_t>(&entry.value);
    if (!box)
    {
      result.add_geometry(entry.value);
      continue;
    }

    shared::aabb_t shape;
    shape.center = box->position;
    shape.half_extents = box->half_extents;
    inputs.push_back({shape, box->surface});
  }

  // CSG union: each new AABB is clipped against all already-placed ones
  // so the final set has zero overlaps. Earlier AABBs win where they overlap.
  struct baked_piece_t
  {
    shared::aabb_t shape;
    shared::geometry_surface_t surface;
  };
  std::vector<baked_piece_t> baked;

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
      baked.push_back({piece, inp.surface});
  }

  // Emit one box brush per piece
  for (const auto &piece : baked)
  {
    shared::box_geometry_t box;
    box.position = piece.shape.center;
    box.half_extents = piece.shape.half_extents;
    box.surface = piece.surface;
    result.add_geometry(std::move(box));
  }

  return result;
}

void Tool_Editor_State::render_ui()
{
  // Ctrl+S / Cmd+S — save current map to disk (no implicit CSG bake; use the
  // "Bake CSG" button for that). Goes through commit_map_to_disk so the
  // running server's session is reloaded too.
  {
    input::modifiers_t mods = input::current_modifiers();
    if (input::is_key_pressed(input::key_t::S) && (mods.ctrl || mods.gui))
    {
      std::string full_path = get_maps_dir() + map.name;
      if (commit_map_to_disk(map, full_path))
        renderer::draw_announcement("Saved!");
      else
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

  // Bake CSG: subtracts overlapping AABBs against each other and replaces the
  // in-memory map with the simplified result. Save is no longer destructive,
  // so this is the only path that mutates geometry — explicit and reversible
  // via undo.
  if (ImGui::Button("Bake CSG"))
  {
    auto simplified = bake_map_csg(map);
    simplified.name = map.name;
    simplified.navmesh = map.navmesh;
    map = std::move(simplified);
    geometry_updated_flag = true;
    renderer::draw_announcement("Geometry simplified (not saved)");
  }

  ImGui::Checkbox("Solid Entities", &draw_entities_solid);
  ImGui::Checkbox("Hide Geometry", &hide_geometry);
  ImGui::Checkbox("Show Grid", &show_grid);
  ImGui::SliderFloat("Camera Speed", &state_manager::get_client_context().cvars->editor_speed,
                     100.0f, 5000.0f, "%.0f");

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

  ImGui::Checkbox("Show Navmesh",
                  &state_manager::get_client_context().cvars->debug_show_navmesh);

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

        // Snapshot the file as it was on disk, so the user can roll back even
        // if their first action is to delete everything and Ctrl+S.
        snapshot_on_load(full_path);

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
      map.name = filename_buf;
      if (commit_map_to_disk(map, full_path))
        renderer::draw_announcement("Saved!");
      else
        renderer::draw_announcement("Save failed!");
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
      add_default_floor(map);

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
  if (ImGui::Button("play"))
  {
    // Commit current edits to disk before switching. This is the same code
    // path as Ctrl+S, so Play_State's last_map.txt reload and the server's
    // reload_map both see exactly what's in the editor right now — no more
    // "I clicked save AND play and it still ran the old map" surprises.
    std::string full_path = get_maps_dir() + map.name;
    if (!commit_map_to_disk(map, full_path))
    {
      renderer::draw_announcement("Save before play failed!");
    }
    else
    {
      state_manager::switch_to(game_state::play);
    }
  }

  if (ImGui::Button("Back to Menu"))
  {
    state_manager::switch_to(game_state::main_menu);
  }

  ImGui::End();

  // Draw Tool UI (e.g. selection rectangle)
  if (active_tool_index >= 0 && active_tool_index < (int)tools.size())
  {
    tools[active_tool_index]->on_draw_ui(context);
  }

  // Camera position overlay (bottom-right)
  {
    ImGuiIO &io = ImGui::GetIO();
    float padding = 8.0f;
    ImVec2 window_pos = ImVec2(io.DisplaySize.x - padding, io.DisplaySize.y - padding);
    ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.5f);
    if (ImGui::Begin("##camera_pos", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing))
    {
      ImGui::Text("%.0f, %.0f, %.0f", camera.position.x, camera.position.y, camera.position.z);
    }
    ImGui::End();
  }
}

void Tool_Editor_State::render_3d(VkCommandBuffer cmd)
{
  renderer::render_view_t view_def;
  view_def.viewport = {{0, 0}, {1, 1}};
  view_def.camera = camera;

  // Applies the viewport and the view-projection every draw_* below reads.
  renderer::set_view(cmd, view_def);

  // Draw Grid
  if (show_grid)
  {
    constexpr int count = editor::MAJOR_GRID_COUNT;
    constexpr float major = editor::MAJOR_GRID_STEP;
    float minor = grid_settings.step();
    float extent = count * major;

    color_t major_color = with_alpha(colors::white, 0x44); // Faint white
    color_t minor_color = with_alpha(colors::white, 0x22); // Fainter
    color_t axis_color_x = colors::red;   // X
    color_t axis_color_y = colors::green; // Y
    color_t axis_color_z = colors::blue;  // Z

    // Helper lambdas to make grid line endpoints based on plane orientation
    // plane 0 = XZ (Y=0, default), plane 1 = XY (Z=0, side view), plane 2 = YZ (X=0, front view)
    auto make_line_a = [&](float p, float ext, int plane) -> std::pair<linalg::vec3, linalg::vec3> {
      switch (plane) {
      case 1: return {{-ext, p, 0}, {ext, p, 0}};  // XY: horizontal lines (along X, stepping Y)
      case 2: return {{0, -ext, p}, {0, ext, p}};   // YZ: lines along Y, stepping Z
      default: return {{-ext, 0, p}, {ext, 0, p}};  // XZ: lines along X, stepping Z
      }
    };
    auto make_line_b = [&](float p, float ext, int plane) -> std::pair<linalg::vec3, linalg::vec3> {
      switch (plane) {
      case 1: return {{p, -ext, 0}, {p, ext, 0}};  // XY: vertical lines (along Y, stepping X)
      case 2: return {{0, p, -ext}, {0, p, ext}};   // YZ: lines along Z, stepping Y
      default: return {{p, 0, -ext}, {p, 0, ext}};  // XZ: lines along Z, stepping X
      }
    };

    int grid_plane = 0; // XZ by default
    if (view_mode == ViewMode::Side) grid_plane = 1;       // XY plane
    else if (view_mode == ViewMode::Front) grid_plane = 2; // YZ plane

    // Subdivision lines (only if grid step < major grid)
    if (minor < major)
    {
      int total = (int)(extent / minor);
      for (int i = -total; i <= total; ++i)
      {
        float p = (float)i * minor;
        if (std::fmod(std::abs(p), major) < 0.01f)
          continue;
        auto [s1, e1] = make_line_a(p, extent, grid_plane);
        auto [s2, e2] = make_line_b(p, extent, grid_plane);
        renderer::draw_line(cmd, s1, e1, minor_color);
        renderer::draw_line(cmd, s2, e2, minor_color);
      }
    }

    // Major grid lines
    for (int i = -count; i <= count; ++i)
    {
      if (i == 0)
        continue;
      float p = (float)i * major;
      auto [s1, e1] = make_line_a(p, extent, grid_plane);
      auto [s2, e2] = make_line_b(p, extent, grid_plane);
      renderer::draw_line(cmd, s1, e1, major_color);
      renderer::draw_line(cmd, s2, e2, major_color);
    }

    // Axes - always draw all relevant axis lines
    renderer::draw_line(cmd, {-extent, 0, 0}, {extent, 0, 0}, axis_color_x);
    renderer::draw_line(cmd, {0, 0, -extent}, {0, 0, extent}, axis_color_z);
    if (grid_plane != 0) // Also draw Y axis for non-XZ planes
      renderer::draw_line(cmd, {0, -extent, 0}, {0, extent, 0}, axis_color_y);
  }

  // Draw map elements: geometry through geometry_editor, entities through
  // entity_editor_traits.
  VulkanOverlayRenderer overlay(cmd);
  if (!hide_geometry)
  {
    for (const shared::map_geometry_t &entry : map.geometry)
      draw_geometry_in_editor(entry.value, overlay, entry.uid, draw_entities_solid);

    for (const auto &entry : map.entities)
    {
      if (!entry.entity)
        continue;
      draw_entity_in_editor(entry.entity.get(), overlay, entry.uid,
                            draw_entities_solid);
    }
  }

  // Draw navmesh triangle wireframes, colored by island ID.
  // Suppressed when the pathfinding tool is active — it draws the navmesh itself.
  if (state_manager::get_client_context().cvars->debug_show_navmesh &&
      map.navmesh.valid() && active_tool_index != 3)
  {
    const navmesh_t &nav = map.navmesh;
    constexpr float y_lift = 2.f;

    static constexpr color_t island_colors[] = {
      colors::cyan,
      colors::yellow,
      colors::green,
      colors::magenta,
    };

    for (const auto &poly : nav.polygons)
    {
      color_t color = island_colors[poly.island % 4];
      const int N = (int)poly.vertices.size();
      for (int e = 0; e < N; ++e)
      {
        vec3f a = nav.vertices[poly.vertices[e          ]].position;
        vec3f b = nav.vertices[poly.vertices[(e + 1) % N]].position;
        a.y += y_lift;
        b.y += y_lift;
        renderer::draw_line(cmd, a, b, color);
      }
    }

    // Draw each vertex as a small cross so winding/deduplication is visible.
    constexpr float right = 2.f;
    constexpr color_t vert_color = colors::white;
    for (const auto &v : nav.vertices)
    {
      vec3f p = v.position; p.y += y_lift;
      renderer::draw_line(cmd, {p.x - right, p.y, p.z}, {p.x + right, p.y, p.z}, vert_color);
      renderer::draw_line(cmd, {p.x, p.y, p.z - right}, {p.x, p.y, p.z + right}, vert_color);
    }
  }

  // Draw particle emitters
  for (auto [uid, pe] : map.entities_of_type<entities::Particle_Emitter_Entity>())
  {
    renderer::particle_emitter_parameters_t p{};
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
    renderer::draw_particles(cmd, p);
  }

  // Draw Tool Overlay
  if (active_tool_index >= 0 && active_tool_index < (int)tools.size())
  {
    tools[active_tool_index]->on_draw_overlay(context, overlay);
  }
}

void Tool_Editor_State::pre_render(VkCommandBuffer cmd)
{
  for (auto [uid, pe] : map.entities_of_type<entities::Particle_Emitter_Entity>())
  {
    renderer::particle_emitter_parameters_t p{};
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
    renderer::update_particles(cmd, p);
  }
}

void Tool_Editor_State::update_bvh()
{
  bvh = build_editor_bvh(map);
}

} // namespace client
