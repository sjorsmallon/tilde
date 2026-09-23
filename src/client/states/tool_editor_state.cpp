#include "../../shared/entities/entity_reflection.hpp"
#include "tool_editor_state.hpp"

#include "../particle_emitter_parameters.hpp"
#include "../../shared/asset.hpp"
#include "../../shared/debug_collision.hpp"
#include "../../shared/map_baker.hpp"
#include "../editor/editor_bvh.hpp"
#include "../editor/entity_editor_traits.hpp"
#include "../editor/connection_lines.hpp"
#include "../editor/entity_icons.hpp"
#include "../editor/entity_outliner.hpp"
#include "../editor/geometry_editor.hpp"
#include "../editor/map_cvars_panel.hpp"
#include "../editor/tools/animation_tool.hpp"
#include "../editor/tools/brush_tool.hpp"
#include "../editor/tools/lightmap_tool.hpp"
#include "../editor/tools/path_tool.hpp"
#include "../editor/path_editing.hpp"
#include "../editor/tools/pathfinding_test_tool.hpp"
#include "../editor/tools/placement_tool.hpp"
#include "../editor/tools/sculpting_tool.hpp"
#include "../editor/tools/particle_editor_tool.hpp"
#include "../editor/tools/selection_tool.hpp"
#include "../console.hpp"
#include "../entity_hitbox_overlay.hpp"
#include "../shadow_debug_draw.hpp"
#include "../hud/announcement.hpp"
#include "../fly_camera.hpp"
#include "../input.hpp"
#include "../renderer.hpp"
#include "../shared/linalg.hpp"
#include "../shared/math.hpp"
#include "../state_manager.hpp"
#include "imgui.h"
#include <SDL_filesystem.h>
#include <SDL_stdinc.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include "../../shared/cvars/generated/cvars_generated.hpp"

namespace client
{

// Internal client hook (set in client_impl.cpp). Invokes the integrated
// launcher's server::change_map_to. Returns false both when no hook is
// installed (a networked client build) and when the server refused the map,
// which is why the installed test is a separate question.
bool server_map_reload_hook_is_installed();
bool invoke_server_map_reload_hook(const std::string &map_path);

struct toolbox_row_t
{
  editor_tool_t tool;
  const char   *label;
};

constexpr Enum_Array<editor_tool_t, toolbox_row_t> TOOLBOX_ROWS = {{
    {editor_tool_t::selection,   "Select"},
    {editor_tool_t::placement,   "Place"},
    {editor_tool_t::sculpting,   "Sculpt"},
    {editor_tool_t::pathfinding, "Pathfinding"},
    {editor_tool_t::particles,   "Particles"},
    {editor_tool_t::animation,   "Animation"},
    {editor_tool_t::brush,       "Brush"},
    {editor_tool_t::lightmap,    "Lightmap"},
    {editor_tool_t::path,        "Path"},
}};
static_assert(rows_in_enum_order<&toolbox_row_t::tool>(TOOLBOX_ROWS));

// Ctrl+1..7 -> a toolbox row. The top-row digits are contiguous, so the digit
// IS the row index; anything else is not a toolbox shortcut and says so.
[[nodiscard]] static std::optional<editor_tool_t> try_toolbox_tool_for_key(input::key_t key)
{
  const int row = static_cast<int>(key) - static_cast<int>(input::key_t::Num_1);
  if (row < 0 || row >= static_cast<int>(EDITOR_TOOL_COUNT))
    return std::nullopt;
  return static_cast<editor_tool_t>(row);
}

// Returns the absolute path to the maps/ directory, creating it if needed.
// Resolved relative to the executable: <exe_dir>/../../maps/ which puts it at
// the project root when running from a cmake_build/bin/ layout.
// What the OPEN MAP says its sky is, out of its own cvars block.
//
// split_cvar_line is the one split the file writer, the file reader and the Map
// Cvars panel all go through, so this cannot disagree with any of them about
// where the name ends. Last line wins, matching apply_map_cvars_that_were_supplied_from_the_editor: the map's list
// is executed in order, so a repeated name ends on its final value.
static std::string skybox_name_of(const shared::map_t &map)
{
  std::string name;
  for (const std::string &line : map.attached_cvars)
  {
    const shared::cvar_line_t split = shared::split_cvar_line(line);
    if (split.name == "sv_skybox")
      name = split.value;
  }
  return name;
}

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
    if (entry.is_regular_file() && entry.path().extension() == ".source")
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

// What a commit did. `server_refused` is a file on disk the server would not
// run -- an ill-typed connection row, which the panel shows red and the load
// check refuses whole -- and the editor has to say so, because the server kept
// the map it had and a play trip would otherwise join the wrong one.
enum class commit_result_t
{
  save_failed,
  server_refused,
  committed,
};

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
static commit_result_t commit_map_to_disk(const shared::map_t &map,
                                          const std::string &full_path)
{
  rotate_backup_file(full_path);

  if (!shared::save_map(full_path, map))
  {
    log_error("save_map failed for '{}'", full_path);
    return commit_result_t::save_failed;
  }

  std::ofstream last_map_f("last_map.txt");
  if (last_map_f.is_open())
    last_map_f << full_path;
  else
    log_error("Could not write last_map.txt");

  // A networked client has no in-process server to reload: the save itself
  // stands and the server it connects to streams whatever it runs.
  if (!server_map_reload_hook_is_installed())
  {
    log_terminal("Server map-reload hook not installed; server session may be stale.");
    return commit_result_t::committed;
  }

  if (!invoke_server_map_reload_hook(full_path))
  {
    log_error("The server refused '{}' and kept the map it was running; see the lines above",
              full_path);
    return commit_result_t::server_refused;
  }

  return commit_result_t::committed;
}

static const char* announcement_for(commit_result_t result)
{
  switch (result)
  {
    case commit_result_t::save_failed:    return "Save failed!";
    case commit_result_t::server_refused: return "Saved, but the server refused the map (see log)";
    case commit_result_t::committed:      return "Saved!";
  }
  return "";
}

// On-load backup: snapshot the file as it was when it was opened so the
// editor can roll back if the user trashes the in-memory map and saves over
// it (which would otherwise leave only the previous-save .bak from
// rotate_backup_file). Safe to call on any path; no-op if missing.
static void snapshot_on_load(const std::string &full_path)
{
  rotate_backup_file(full_path);
}

// A new map isn't empty — it gets a floor to stand on, or the first thing you
// place has nothing to land against.
static void add_default_floor(shared::map_t &map)
{
  map.add_geometry(shared::make_box_brush(
      {0, editor::DEFAULT_FLOOR_Y, 0},
      {editor::DEFAULT_FLOOR_HALF_W, editor::DEFAULT_FLOOR_HALF_H,
       editor::DEFAULT_FLOOR_HALF_W}));
}

static void add_default_spawner(shared::map_t &map)
{
  auto entity = entities::Player_Spawn_Entity{};
  entity.position = {0, editor::DEFAULT_FLOOR_Y, 0};
  map.add_entity(std::make_shared<entities::Player_Spawn_Entity>(entity));
}
static void add_default_warp_trigger(shared::map_t &map)
{
  auto entity = entities::Trigger_Volume_Entity{};
  entity.position = {0, editor::DEFAULT_FLOOR_Y -128.0, 0};
  entity.volume.half_extents = {editor::DEFAULT_FLOOR_HALF_W,editor::DEFAULT_FLOOR_HALF_W,editor::DEFAULT_FLOOR_HALF_W};
  map.add_entity(std::make_shared<entities::Trigger_Volume_Entity>(entity));
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
      std::optional<shared::map_t> loaded = shared::try_load_map(line);
      map_loaded = loaded.has_value();
      if (!map_loaded)
        log_terminal("Failed to load map");
      else
      {
        map = std::move(*loaded);
        entity_visibility.show_all();
        snapshot_on_load(line);
      }
    }

    if (!map_loaded)
    {
      map.name = "untitled.source";
      add_default_floor(map);
      hud::set_announcement("Welcome to the Tool Editor!");
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
  if (!tools[editor_tool_t::selection])
  {
    tools[editor_tool_t::selection]   = std::make_unique<Selection_Tool>();
    tools[editor_tool_t::placement]   = std::make_unique<Placement_Tool>();
    tools[editor_tool_t::sculpting]   = std::make_unique<Sculpting_Tool>();
    tools[editor_tool_t::pathfinding] = std::make_unique<Pathfinding_Test_Tool>();
    tools[editor_tool_t::particles]   = std::make_unique<Particle_Editor_Tool>();
    tools[editor_tool_t::animation]   = std::make_unique<Animation_Tool>();
    tools[editor_tool_t::brush]       = std::make_unique<Brush_Tool>();
    tools[editor_tool_t::lightmap]    = std::make_unique<Lightmap_Tool>();
    tools[editor_tool_t::path]        = std::make_unique<Path_Tool>();
  }

  // Enable first tool
  switch_tool(editor_tool_t::selection);

  update_bvh();
}

void Tool_Editor_State::on_exit()
{
  if (active_tool)
  {
    tools[*active_tool]->on_disable(context);
  }
}

void Tool_Editor_State::snap_to_axis_view(ViewMode mode)
{
  // Pitch stops short of straight down. At exactly +-90 the forward vector is
  // parallel to world up, the right vector collapses, and get_orientation_vectors
  // falls back to an arbitrary one -- so the view spins depending on nothing.
  constexpr float VERTICAL_PITCH = 89.0f;

  float       yaw          = 0.0f;
  float       pitch        = 0.0f;
  const char *announcement = "";

  switch (mode)
  {
    case ViewMode::Front:   yaw = 0.0f;   pitch = 0.0f;             announcement = "Front (+X)";   break;
    case ViewMode::Back:    yaw = 180.0f; pitch = 0.0f;             announcement = "Back (-X)";    break;
    case ViewMode::Side:    yaw = 90.0f;  pitch = 0.0f;             announcement = "Right (+Z)";   break;
    case ViewMode::Left:    yaw = 270.0f; pitch = 0.0f;             announcement = "Left (-Z)";    break;
    case ViewMode::TopDown: yaw = 0.0f;   pitch = -VERTICAL_PITCH;  announcement = "Top (-Y)";     break;
    case ViewMode::Bottom:  yaw = 0.0f;   pitch = VERTICAL_PITCH;   announcement = "Bottom (+Y)";  break;

    // Not an axis view; nothing to snap to. Reached only if a caller passes it,
    // which no keypad binding does.
    case ViewMode::FreeCam:
      return;
  }

  // The active tool decides what "the thing" is. No tool with an opinion means
  // the world origin at map scale, which is the old behaviour.
  view_focus_t focus;
  if (active_tool)
  {
    if (std::optional<view_focus_t> tool_focus = tools[*active_tool]->view_focus())
      focus = *tool_focus;
  }

  camera.orthographic = true;
  camera.yaw          = yaw;
  camera.pitch        = pitch;
  view_mode           = mode;

  // Orthographic, so the pull-back does not affect how big the subject looks --
  // only whether it is inside the depth range. Framing is ortho_height's job.
  const camera_basis_t basis = get_orientation_vectors(camera);
  const float          pull_back = std::max(focus.radius * 4.0f, 1024.0f);
  camera.position = focus.center - basis.forward * pull_back;

  // Diameter plus a margin, so the subject fills the viewport instead of
  // sitting in the middle of it.
  camera.ortho_height  = focus.radius * 2.5f;
  camera.orbit_target  = focus.center;

  hud::set_announcement(announcement);
}

constexpr float ORBIT_DEGREES_PER_PIXEL = 0.25f;

linalg::vec3f Tool_Editor_State::pick_orbit_pivot()
{
  const viewport_state_t view = transform_viewport_state();
  ray_hit_result_t       hit{};
  if (!editor_bvh.bvh.nodes.empty() &&
      bvh_intersect_ray(editor_bvh.bvh, view.mouse_ray.origin, view.mouse_ray.direction, hit))
    return view.mouse_ray.origin + view.mouse_ray.direction * hit.t;

  if (active_tool)
  {
    if (std::optional<view_focus_t> tool_focus = tools[*active_tool]->view_focus())
      return tool_focus->center;
  }

  return {0.0f, 0.0f, 0.0f};
}

void Tool_Editor_State::orbit_camera_around_pivot(float yaw_delta_degrees, float pitch_delta_degrees)
{
  linalg::vec3f offset = camera.position - orbit_pivot;

  offset = linalg::rotate(linalg::from_axis_angle({0.0f, 1.0f, 0.0f}, -yaw_delta_degrees), offset);
  camera.yaw += yaw_delta_degrees;

  const float          new_pitch = std::clamp(camera.pitch + pitch_delta_degrees, -89.0f, 89.0f);
  const camera_basis_t basis     = get_orientation_vectors(camera);
  offset = linalg::rotate(linalg::from_axis_angle(basis.right, new_pitch - camera.pitch), offset);
  camera.pitch = new_pitch;

  camera.position = orbit_pivot + offset;
}

void Tool_Editor_State::switch_tool(editor_tool_t tool)
{
  if (active_tool == tool)
    return;

  context.selection_handed_over.clear();
  if (active_tool)
  {
    const Span<const shared::entity_uid_t> leaving = tools[*active_tool]->selected_objects();
    context.selection_handed_over.assign(leaving.begin(), leaving.end());
    tools[*active_tool]->on_disable(context);
  }

  active_tool = tool;

  // Update context
  context.map = &map;
  context.map_path = get_maps_dir() + map.name;
  context.bvh = &editor_bvh.bvh;
  context.objects_without_collision = editor_bvh.objects_without_collision;
  context.geometry_updated_so_bvh_rebuild_is_needed = &geometry_updated_flag;
  context.lightmap_updated_so_atlas_upload_is_needed = &lightmap_updated_flag;
  context.grid = &grid_settings;
  context.entity_draw_settings = {
      .gravity = state_manager::get_client_context().cvars->g_gravity};
  context.tickrate = state_manager::get_client_context().cvars->sv_tickrate;
  entity_visibility.refresh(map);
  context.hidden_objects = entity_visibility.hidden_this_frame;
  // context.time is NOT reset here -- it is seconds since the editor opened,
  // advanced in update(), and a tool switch is not a new clock. Resetting it
  // made every selection pulse restart mid-fade.

  tools[*active_tool]->on_enable(context);
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

  // The editor's own clock, in seconds since it opened. Every animated overlay
  // reads it -- selection pulses, and the hitbox highlight in the Animation
  // tool. It was pinned at 0 with a TODO, which is why the Selection tool's
  // "pulse" has always been a flat colour.
  context.time += dt;

  // Re-read every frame so `r_fov` from the console takes effect immediately,
  // and so picking and rendering can never be a frame apart on it.
  camera.fov_degrees = state_manager::get_client_context().cvars->r_fov;

  if (input::is_key_pressed(input::key_t::Escape))
  {
    state_manager::switch_to(game_state::main_menu);
    return;
  }

  if (input::is_key_pressed(input::key_t::F1))
    play_was_requested_by_key = true;
  if (input::is_key_pressed(input::key_t::F2))
    play_at_spawn_was_requested_by_key = true;

  // Update Camera
  if (!input::imgui_wants_mouse())
  {
    input::modifiers_t mods = input::current_modifiers();

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
        hud::set_announcement("Top Down (-Y)");
        break;
      case ViewMode::TopDown:
        view_mode = ViewMode::Front;
        camera.orthographic = true;
        camera.yaw = 0.0f;
        camera.pitch = 0.0f;
        hud::set_announcement("Front (+X)");
        break;
      case ViewMode::Front:
        view_mode = ViewMode::Side;
        camera.orthographic = true;
        camera.yaw = 90.0f;
        camera.pitch = 0.0f;
        hud::set_announcement("Side (+Z)");
        break;
      case ViewMode::Side:
      // The keypad-only views are not in the cycle -- Shift+Space would
      // otherwise need seven steps to get back where it started. They drop
      // straight out to Free Cam.
      case ViewMode::Bottom:
      case ViewMode::Back:
      case ViewMode::Left:
        view_mode = ViewMode::FreeCam;
        camera.orthographic = false;
        hud::set_announcement("Free Cam");
        break;
      }
    }

    // Keypad axis views, Blender-style: 1 front, 3 right, 7 top, Ctrl for the
    // opposite side. Unlike Shift+Space these centre on what the active tool
    // says it is looking at, which is the point of them.
    if (input::is_key_pressed(input::key_t::Keypad_1))
      snap_to_axis_view(mods.ctrl ? ViewMode::Back : ViewMode::Front);
    if (input::is_key_pressed(input::key_t::Keypad_3))
      snap_to_axis_view(mods.ctrl ? ViewMode::Left : ViewMode::Side);
    if (input::is_key_pressed(input::key_t::Keypad_7))
      snap_to_axis_view(mods.ctrl ? ViewMode::Bottom : ViewMode::TopDown);

    step_fly_speed_from_keypad(state_manager::get_client_context().cvars->editor_speed);

    // Keypad 5 is Blender's ortho/perspective toggle, and having snapped to an
    // axis you immediately want it. Same effect as O, on the key the muscle
    // memory reaches for.
    if (input::is_key_pressed(input::key_t::Keypad_5))
    {
      camera.orthographic = !camera.orthographic;
      if (!camera.orthographic)
        view_mode = ViewMode::FreeCam;
      hud::set_announcement(camera.orthographic ? "Orthographic" : "Perspective");
    }

    if (camera.orthographic && view_mode == ViewMode::FreeCam)
    {
      if (input::is_key_pressed(input::key_t::Arrow_Right))
        camera.yaw = fmodf(camera.yaw + 90.0f, 360.0f);
      if (input::is_key_pressed(input::key_t::Arrow_Left))
        camera.yaw = fmodf(camera.yaw - 90.0f + 360.0f, 360.0f);
    }

    const auto announce_work_plane = [&]()
    {
      char buffer[64];
      snprintf(buffer, sizeof(buffer), "Work plane: y = %.0f", context.work_plane_height);
      hud::set_announcement(buffer);
    };

    if (input::is_key_pressed(input::key_t::Right_Bracket))
    {
      if (mods.shift)
      {
        context.work_plane_height += grid_settings.step();
        announce_work_plane();
      }
      else
      {
        grid_settings.increase();
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "Grid: %.0f", grid_settings.step());
        hud::set_announcement(buffer);
      }
    }
    if (input::is_key_pressed(input::key_t::Left_Bracket))
    {
      if (mods.shift)
      {
        context.work_plane_height -= grid_settings.step();
        announce_work_plane();
      }
      else
      {
        grid_settings.decrease();
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "Grid: %.0f", grid_settings.step());
        hud::set_announcement(buffer);
      }
    }

    if (input::is_key_pressed(input::key_t::Home))
    {
      if (mods.shift)
      {
        context.work_plane_height = 0.0f;
        announce_work_plane();
      }
      else
      {
        context.bvh  = &editor_bvh.bvh;
        context.grid = &grid_settings;
        if (const std::optional<linalg::vec3> surface =
                try_pick_surface_point(context, transform_viewport_state()))
        {
          context.work_plane_height = surface->y;
          announce_work_plane();
        }
        else
        {
          hud::set_announcement("Work plane: no surface under the cursor");
        }
      }
    }

    const bool console_open = console::get().is_open();
    const bool middle_down  = input::is_mouse_down(input::mouse_button_t::Middle) && !console_open;
    if (!middle_down)
      orbiting = false;

    linalg::vec2i look_delta = {0, 0};
    if (middle_down)
    {
      if (!orbiting)
      {
        orbit_pivot = pick_orbit_pivot();
        orbiting    = true;
        view_mode   = ViewMode::FreeCam;
      }
      input::set_relative_mouse_mode(true);
      const linalg::vec2i delta = input::mouse_delta();
      orbit_camera_around_pivot(delta.x * ORBIT_DEGREES_PER_PIXEL, -delta.y * ORBIT_DEGREES_PER_PIXEL);
    }
    else if (input::is_mouse_down(input::mouse_button_t::Right) && view_mode == ViewMode::FreeCam && !console_open)
    {
      input::set_relative_mouse_mode(true);
      look_delta = input::mouse_delta();
    }
    else
    {
      input::set_relative_mouse_mode(false);
    }

    const bool tool_captures_kb = active_tool && tools[*active_tool]->capture_keyboard();
    fly_camera_input_t fly_input = read_fly_camera_keys();
    // Shift+Space cycles the axis views above; it is not "up".
    if (mods.shift)
      fly_input.up = false;
    if (!tool_captures_kb && input::is_key_down(input::key_t::Q))
      fly_input.down = true;

    fly_camera_settings_t fly_settings;
    fly_settings.units_per_second = state_manager::get_client_context().cvars->editor_speed;

    if (!camera.orthographic)
    {
      fly_input.look_delta = look_delta;
      fly_camera(camera, fly_input, fly_settings, dt);
    }
    else
    {
      float speed = fly_settings.units_per_second * dt;
      if (fly_input.fast)
        speed *= fly_settings.fast_multiplier;
      const camera_basis_t basis = get_orientation_vectors(camera);

      if (fly_input.forward)
        camera.position = camera.position + basis.up * speed;
      if (fly_input.backward)
        camera.position = camera.position - basis.up * speed;
      if (fly_input.right)
      {
        camera.position.x += basis.right.x * speed;
        camera.position.z += basis.right.z * speed;
      }
      if (fly_input.left)
      {
        camera.position.x -= basis.right.x * speed;
        camera.position.z -= basis.right.z * speed;
      }
      if (fly_input.up)
        camera.ortho_height += speed;
      if (input::is_key_down(input::key_t::C))
        camera.ortho_height = std::max(camera.ortho_height - speed, 1.0f);
    }
  }

  if (geometry_updated_flag)
  {
    update_bvh();
    geometry_updated_flag = false;
  }

  if (lightmap_updated_flag || map.lightmap.geometry_id != uploaded_lightmap_geometry_id)
  {
    lightmap_updated_flag         = false;
    uploaded_lightmap_geometry_id = map.lightmap.geometry_id;

    if (!map.lightmap.irradiance_pages.empty())
    {
      if (scene.lightmap.valid())
        renderer::update_lightmap(scene.lightmap, map.lightmap);
      else
        scene.lightmap = renderer::register_lightmap(map.lightmap);
    }
  }

  // Update Viewport
  context.map = &map;
  context.map_path = get_maps_dir() + map.name;
  context.bvh = &editor_bvh.bvh;
  context.objects_without_collision = editor_bvh.objects_without_collision;
  context.geometry_updated_so_bvh_rebuild_is_needed = &geometry_updated_flag;
  context.lightmap_updated_so_atlas_upload_is_needed = &lightmap_updated_flag;
  context.grid = &grid_settings;
  context.placement_prefers_surface = input::current_modifiers().shift;
  context.entity_draw_settings = {
      .gravity = state_manager::get_client_context().cvars->g_gravity};
  context.tickrate = state_manager::get_client_context().cvars->sv_tickrate;

  // Flattened out of the one pass that knows both the per-entity set and the
  // per-type mask, the way objects_without_collision is.
  entity_visibility.refresh(map);
  context.hidden_objects = entity_visibility.hidden_this_frame;

  context.time += dt;
  viewport = transform_viewport_state();

  static bool was_lmb_down = false;
  static bool tool_processing_mouse = false;

  if (input::imgui_wants_mouse() && !tool_processing_mouse)
  {
    // Use a ray that won't hit anything to prevent hovering
    // Origin far away, direction pointing away
    viewport.mouse_ray.origin = {0, 1e20f, 0};
    viewport.mouse_ray.direction = {0, 1.0f, 0};
  }

  // A prefab picked in the Placement tool is a PASTE, and the Selection tool
  // owns that gesture -- the same hand-off the entity outliner makes when it
  // asks for a selection.
  if (context.requested_paste && active_tool != editor_tool_t::selection)
    switch_tool(editor_tool_t::selection);

  if (active_tool)
  {
    tools[*active_tool]->on_update(context, viewport, dt);

    input::mouse_event_t mouse_e;
    mouse_e.button = input::mouse_button_t::Left;
    mouse_e.position = input::mouse_position();
    mouse_e.delta = input::mouse_delta();
    mouse_e.mods = input::current_modifiers();

    bool is_lmb_down = input::is_mouse_down(input::mouse_button_t::Left);

    if (is_lmb_down && !was_lmb_down)
    {
      if (!input::imgui_wants_mouse())
      {
        tool_processing_mouse = true;
        tools[*active_tool]->on_mouse_down(context, mouse_e);
      }
    }
    else if (is_lmb_down && was_lmb_down)
    {
      if (tool_processing_mouse)
      {
        tools[*active_tool]->on_mouse_drag(context, mouse_e);
      }
    }
    else if (!is_lmb_down && was_lmb_down)
    {
      if (tool_processing_mouse)
      {
        tools[*active_tool]->on_mouse_up(context, mouse_e);
        tool_processing_mouse = false;
      }
    }
    was_lmb_down = is_lmb_down;
  }

  // This frame's key-down events. The input system collects them from the SDL
  // event pump, so no scancode polling. Ctrl+<digit> is resolved here rather
  // than in a tool, because switching tools has to work from every tool --
  // including from none, which is why this sits outside the block above.
  if (!input::imgui_wants_text_input())
  {
    for (const input::key_event_t &key_event : input::frame_key_events())
    {
      if (key_event.mods.ctrl)
      {
        if (std::optional<editor_tool_t> tool = try_toolbox_tool_for_key(key_event.key))
        {
          switch_tool(*tool);
          hud::set_announcement(TOOLBOX_ROWS[*tool].label);
          continue;
        }
      }

      if (active_tool)
        tools[*active_tool]->on_key_down(context, key_event);
    }
  }
}

void Tool_Editor_State::draw_imgui_panels()
{
  // Ctrl+S / Cmd+S — save current map to disk. Goes through commit_map_to_disk
  // so the running server's session is reloaded too.
  {
    input::modifiers_t mods = input::current_modifiers();
    if (input::is_key_pressed(input::key_t::S) && (mods.ctrl || mods.gui))
    {
      std::string full_path = get_maps_dir() + map.name;
      hud::set_announcement(announcement_for(commit_map_to_disk(map, full_path)));
    }
  }

  ImGui::Begin("Map Info", nullptr, ImGuiWindowFlags_NoNav);
  ImGui::Text("Map: %s", map.name.c_str());

  bool should_open_popup = false;
  bool should_open_load_popup = false;
  bool should_open_new_map_popup = false;

  if (ImGui::Button("Save Map As..."))
  {
    hud::set_announcement("is the gerg ever open?");
    // Popup for Save Map
    should_open_popup = true;
  }

  if (ImGui::Button("Load Map..."))
    should_open_load_popup = true;

  if (ImGui::Button("New Map"))
    should_open_new_map_popup = true;

  ImGui::Checkbox("Solid Entities", &draw_entities_solid);
  ImGui::Checkbox("Hide Geometry", &hide_geometry);
  ImGui::Checkbox("Entity Icons", &show_entity_icons);
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
      hud::set_announcement("Navmesh baked!");
    else
      hud::set_announcement("Navmesh bake failed (save map first?)");
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

  // Collapsed by default: most maps carry no cvars at all, and the section is
  // tall when they do. The header IS the checkbox -- a checkbox beside it would
  // be a second control saying the same thing.
  ImGui::Separator();
  if (ImGui::CollapsingHeader("Map Cvars"))
    draw_map_cvars_section(map, *state_manager::get_client_context().cvars,
                           transaction_system);

  // Run ONCE per frame, shared by the list and the viewport lines: two runs are
  // two answers free to disagree about which row is red.
  const std::vector<shared::connection_refusal_t> connection_refusals =
      shared::validate_map_connections(map);

  // Collapsed by default, for Map Cvars' reason: the wiring is not what you are
  // looking at most of the time, and the list is tall when a map has one.
  ImGui::Separator();
  hovered_connection = SIZE_MAX;
  if (ImGui::CollapsingHeader("Connections"))
  {
    const std::optional<shared::entity_uid_t> clicked_sender =
        draw_connection_overview(map, connection_refusals, connection_lines,
                                 hovered_connection);

    // The row's editable panel is the SENDER's, and that panel is the Selection
    // tool's -- so a click has to land there whatever tool was active.
    if (clicked_sender)
    {
      switch_tool(editor_tool_t::selection);
      context.requested_selection = clicked_sender;
    }
  }

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

  ImGui::SetNextWindowSize(ImVec2(360, 420), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(ImVec2(280, 200), ImVec2(FLT_MAX, FLT_MAX));
  if (ImGui::BeginPopupModal("Load Map", nullptr, ImGuiWindowFlags_None))
  {
    static std::vector<std::string> map_files;
    static int selected_idx = -1;

    if (ImGui::IsWindowAppearing())
    {
      map_files = list_map_files();
      selected_idx = -1;
    }

    ImGui::Text("maps/");
    if (ImGui::BeginChild("##maplist", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), true))
    {
      if (map_files.empty())
      {
        ImGui::TextDisabled("(no .source files in maps/)");
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
      if (std::optional<shared::map_t> new_map = shared::try_load_map(full_path))
      {
        map = std::move(*new_map);
        entity_visibility.show_all();
        transaction_system = Transaction_System{};
        geometry_updated_flag = true;

        std::ofstream last_map_f("last_map.txt");
        if (last_map_f.is_open())
          last_map_f << full_path;

        // Snapshot the file as it was on disk, so the user can roll back even
        // if their first action is to delete everything and Ctrl+S.
        snapshot_on_load(full_path);

        hud::set_announcement("Map loaded!");
      }
      else
      {
        hud::set_announcement("Failed to load map!");
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
      hud::set_announcement(announcement_for(commit_map_to_disk(map, full_path)));
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
      entity_visibility.show_all();
      map.name = "new_map.source";
      add_default_floor(map);
      add_default_spawner(map);
      add_default_warp_trigger(map);

      transaction_system = Transaction_System{};
      geometry_updated_flag = true;

      // Clear last_map.txt so the editor doesn't reload the old map on restart
      std::ofstream("last_map.txt");

      hud::set_announcement("New map created!");
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0)))
      ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
  }

  const bool toolbar_is_open = ImGui::BeginMainMenuBar();
  bool       play_was_clicked = play_was_requested_by_key || play_at_spawn_was_requested_by_key;
  const bool spawn_at_camera  = play_was_requested_by_key;
  play_was_requested_by_key          = false;
  play_at_spawn_was_requested_by_key = false;
  bool       back_to_menu_was_clicked = false;
  if (toolbar_is_open)
  {
    for (uint32_t row = 0; row < EDITOR_TOOL_COUNT; ++row)
    {
      const toolbox_row_t& entry = TOOLBOX_ROWS.values[row];
      const bool           is_active = active_tool && *active_tool == entry.tool;
      if (is_active)
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
      if (ImGui::Button(entry.label))
        switch_tool(entry.tool);
      if (is_active)
        ImGui::PopStyleColor();
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Ctrl+%u", row + 1);
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float right_side_width = ImGui::CalcTextSize("play").x + ImGui::CalcTextSize("Back to Menu").x +
                                   style.FramePadding.x * 4.0f + style.ItemSpacing.x + style.WindowPadding.x;
    const float right_side_start = ImGui::GetWindowWidth() - right_side_width;
    if (right_side_start > ImGui::GetCursorPosX())
      ImGui::SetCursorPosX(right_side_start);

    play_was_clicked |= ImGui::Button("play");
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("F1 at the camera, F2 at the spawn");
    back_to_menu_was_clicked = ImGui::Button("Back to Menu");
    ImGui::EndMainMenuBar();
  }

  if (play_was_clicked)
  {
    // Commit current edits to disk before switching. The same funnel as
    // Ctrl+S, so Play_State's last_map.txt reload and the server's
    // change_map_to both see exactly what is in the editor right now. Only a
    // full commit proceeds: a refused map means the server is still running
    // the previous one, and joining it is the "spawned at the origin in an
    // empty level" trip this used to make.
    std::string full_path = get_maps_dir() + map.name;
    const commit_result_t result = commit_map_to_disk(map, full_path);
    if (result == commit_result_t::save_failed)
    {
      hud::set_announcement("Save before play failed!");
    }
    else if (result == commit_result_t::server_refused)
    {
      hud::set_announcement("Server refused the map: fix the red connection rows (see log)");
    }
    else
    {
      // Clicking play in the editor means play, so the trip carries the
      // `join_game` a spectating connection would otherwise wait for you to
      // type. Play_State sends it once it is connected.
      client_context_t& client_context = state_manager::get_client_context();
      client_context.requested_match_join = true;
      if (spawn_at_camera)
        client_context.requested_spawn_view = camera;
      state_manager::switch_to(game_state::play);
    }
  }

  if (back_to_menu_was_clicked)
  {
    state_manager::switch_to(game_state::main_menu);
  }

  // Its own window rather than a Map Info section, unlike Connections: an
  // outliner is exactly the thing you keep open while working. "Map" is not
  // decoration -- a Player_Entity is @runtime_only and never sits in a map, so
  // the word says which of the two kinds of entity this lists, and it reads as
  // one family with Map Info and Map Cvars.
  if (ImGui::Begin("Map Entities", nullptr, ImGuiWindowFlags_NoNav))
  {
    const outliner_result_t outliner = draw_entity_outliner(
        map, entity_visibility,
        active_tool ? tools[*active_tool]->selected_objects() : Span<const shared::entity_uid_t>{});

    if (outliner.clicked_object)
    {
      switch_tool(editor_tool_t::selection);
      context.requested_selection = outliner.clicked_object;
    }
    if (outliner.clicked_group)
    {
      switch_tool(editor_tool_t::selection);
      context.requested_group_selection = outliner.clicked_group;
    }
    if (outliner.group_selection)
    {
      switch_tool(editor_tool_t::selection);
      context.requested_group_of_selection = true;
    }
    if (outliner.ungroup)
    {
      switch_tool(editor_tool_t::selection);
      context.requested_ungroup = outliner.ungroup;
    }
  }
  ImGui::End();

  // Icons before the tool's own overlay, and both under the panels: the tool is
  // drawing what the CURSOR is about to do, which has to sit on top of a layer
  // that is drawing where everything is.
  if (show_entity_icons && !hide_geometry)
    draw_entity_icons(map, transform_viewport_state(), entity_visibility.hidden_this_frame);

  draw_connection_lines(map, transform_viewport_state(), connection_lines, active_tool ? tools[*active_tool]->selected_objects() : Span<const shared::entity_uid_t>{},
                        connection_refusals, entity_visibility.hidden_this_frame,
                        hovered_connection, context.time);

  // Draw Tool UI (e.g. selection rectangle)
  if (active_tool)
  {
    tools[*active_tool]->on_draw_ui(context);
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

void Tool_Editor_State::build_frame(float delta_seconds,
                                    std::vector<renderer::view_pass_t> &passes,
                                    renderer::ui_draw_list_t &ui)
{
  scene.begin_frame(delta_seconds);
  scene.view.viewport = {{0, 0}, {1, 1}};
  scene.view.camera   = camera;
  scene.debug_channel = state_manager::get_client_context().cvars->r_debug_channel;
  if (state_manager::get_client_context().cvars->r_shadow_freeze)
  {
    draw_shadow_cascades(scene.debug, renderer::sun_shadow_cascades());
    draw_point_shadow_faces(scene.debug, renderer::point_shadow_faces());
  }

  // Sized from the bake's resolve table before any light is placed: the array's
  // head is indexed by baked slot, so it has to exist before the entity walk
  // below can write into it.
  shared::begin_frame_lights(scene.lights, map.lightmap);

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
      default: return {{-ext, context.work_plane_height, p}, {ext, context.work_plane_height, p}};
      }
    };
    auto make_line_b = [&](float p, float ext, int plane) -> std::pair<linalg::vec3, linalg::vec3> {
      switch (plane) {
      case 1: return {{p, -ext, 0}, {p, ext, 0}};  // XY: vertical lines (along Y, stepping X)
      case 2: return {{0, p, -ext}, {0, p, ext}};   // YZ: lines along Z, stepping Y
      default: return {{p, context.work_plane_height, -ext}, {p, context.work_plane_height, ext}};
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
        scene.debug.line(s1, e1, minor_color);
        scene.debug.line(s2, e2, minor_color);
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
      scene.debug.line(s1, e1, major_color);
      scene.debug.line(s2, e2, major_color);
    }

    // Axes - always draw all relevant axis lines
    const float axis_height = grid_plane == 0 ? context.work_plane_height : 0.0f;
    scene.debug.line({-extent, axis_height, 0}, {extent, axis_height, 0}, axis_color_x);
    scene.debug.line({0, axis_height, -extent}, {0, axis_height, extent}, axis_color_z);
    if (grid_plane != 0) // Also draw Y axis for non-XZ planes
      scene.debug.line({0, -extent, 0}, {0, extent, 0}, axis_color_y);
  }

  // Draw map elements: geometry through geometry_editor, entities through
  // entity_editor_traits.
  if (!hide_geometry)
  {
    for (const shared::map_geometry_t &entry : map.geometry)
    {
      const size_t first_mesh = scene.meshes.size();
      draw_geometry_in_editor(entry.value, scene, entry.uid, draw_entities_solid,
                              map.materials, map.lightmap,
                              context.object_collides(entry.uid));
      scene.record_object_meshes(entry.uid, first_mesh);
    }

    const bool show_hitboxes =
        state_manager::get_client_context().cvars->debug_show_hitboxes;

    for (const auto &entry : map.entities)
    {
      if (!entry.entity)
        continue;

      if (!context.object_is_visible(entry.uid))
        continue;

      // The editor lays the frame's lights out exactly as the game does, which
      // is what makes a bake previewed here the bake that ships.
      shared::add_frame_light(scene.lights, map.lightmap, entry.uid, *entry.entity);

      const size_t first_mesh = scene.meshes.size();
      draw_entity_in_editor(entry.entity.get(), scene, context.entity_draw_settings);
      scene.record_object_meshes(entry.uid, first_mesh);
      // ALONGSIDE the model, never instead of it: a hit volume lives inside the
      // model it belongs to.
      if (show_hitboxes)
        draw_entity_hitbox_overlay(entry.entity.get(), scene);
    }

    draw_path_links(map, context, scene);
  }

  // Draw navmesh triangle wireframes, colored by island ID.
  // Suppressed when the pathfinding tool is active — it draws the navmesh itself.
  if (state_manager::get_client_context().cvars->debug_show_navmesh &&
      map.navmesh.valid() && active_tool != editor_tool_t::pathfinding)
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
        scene.debug.line(a, b, color);
      }
    }

    // Draw each vertex as a small cross so winding/deduplication is visible.
    constexpr float right = 2.f;
    constexpr color_t vert_color = colors::white;
    for (const auto &v : nav.vertices)
    {
      vec3f p = v.position; p.y += y_lift;
      scene.debug.line({p.x - right, p.y, p.z}, {p.x + right, p.y, p.z}, vert_color);
      scene.debug.line({p.x, p.y, p.z - right}, {p.x, p.y, p.z + right}, vert_color);
    }
  }

  // Particle emitters. Filled ONCE: the renderer sequences the compute dispatch
  // before the render pass itself, because that ordering is a Vulkan fact rather
  // than something a caller should have to remember.
  for (auto [uid, pe] : map.entities_of_type<entities::Particle_Emitter_Entity>())
    scene.particles.push_back(emitter_parameters(*pe, last_dt));

  // Draw Tool Overlay
  if (active_tool)
  {
    tools[*active_tool]->on_draw_overlay(context, scene);
  }

  scene.sky = skybox.resolve(skybox_name_of(map));

  passes.push_back(scene.to_pass());
}

void Tool_Editor_State::update_bvh()
{
  editor_bvh = build_editor_bvh(map);
}

} // namespace client
