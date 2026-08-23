#include "../shared/asset.hpp"
#include "../shared/entity_system.hpp"
#include "audio/audio_system.hpp"
#include "client_api.hpp"
#include "console.hpp"
#include "hud/announcement.hpp"
#include "renderer.hpp"
#include "state_manager.hpp"
#include "ui/font.hpp"

#include <cmath>
#include <memory>
#include <vector>

#include <chrono>
#include <iostream>
#include <string>

#include <SDL.h>

#include "cvars/generated/cvars_generated.hpp"
#include "input.hpp"
#include "log.hpp"
#include "timed_function.hpp"

namespace client
{

static SDL_Window* g_window = nullptr;
static std::chrono::high_resolution_clock::time_point g_last_tick_time;
static bool g_tick_time_initialized = false;

static std::unique_ptr<audio_system_t> g_audio;
static std::unique_ptr<ui::ui_font_t> g_ui_font;

void set_asset_state(assets::asset_state_t *asset_state)
{
  assets::set_state(asset_state);
}

bool init(cvars::cvar_state_t *cvar_state, cvars::command_table_t *command_table,
          assets::asset_state_t *asset_state)
{
  timed_function();
  log_terminal("--- Initializing Client (SDL + Vulkan) ---");

  if (!cvar_state || !command_table || !asset_state)
  {
    log_error("client::Init: the launcher must own and pass a cvar_state_t, a "
              "command_table_t and an asset_state_t (see cvar_def.md and the "
              "ownership note in asset.hpp)");
    return false;
  }

  // Before the renderer resolves a single mesh. This DLL's copy of the asset
  // accessors' state pointer is null until now -- that is precisely the bug
  // that made every get_mesh here return an invalid handle.
  assets::set_state(asset_state);

  // Before anything that could read a cvar or run a console line. The bind call
  // fills this DLL's @Client handler slots; the linker already proved every
  // symbol it references exists, so there is nothing to verify at runtime.
  state_manager::get_client_context().cvars    = cvar_state;
  state_manager::get_client_context().commands = command_table;
  cvars::bind_client_commands(*command_table);
  console::get().set_cvar_state(cvar_state, command_table);

  // Before SDL_Init, because SDL declares process DPI awareness during video
  // init and Windows refuses the change afterwards. Without it the process is
  // DPI-UNAWARE: Windows hands us a virtualized desktop (2560x1440 on a 4K
  // panel at 150%), we render every pixel at that size, and DWM bilinearly
  // upscales the finished frame to 3840x2160. Every glyph is resampled, which
  // no amount of care in the rasterizer or the blend can undo.
  SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
  {
    log_error("SDL_Init Error: {}", SDL_GetError());
    return false;
  }

  // The raw-input thread owns its own message-only window, so it needs no
  // handle from ours and can come up before the window does. Failure is not
  // fatal -- see input::init.
  input::init();

  g_window =
      SDL_CreateWindow("MyGame Client", SDL_WINDOWPOS_CENTERED,
                       SDL_WINDOWPOS_CENTERED, 1280, 720,
                       SDL_WINDOW_VULKAN | SDL_WINDOW_SHOWN |
                           SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);

  if (!g_window)
  {
    log_error("SDL_CreateWindow Error: {}", SDL_GetError());
    return false;
  }

  if (!renderer::init(g_window))
  {
    log_error("Renderer Init Failed");
    return false;
  }

  // load a font.
  {
    // BAKED at the real pixel height, never scaled at draw time: a raster atlas
    // resampled to fit a denser display is the same blur the DPI fix removed.
    // The three logical sizes stay the design; the scale is applied here.
    const float scale = renderer::display_scale();

    const std::optional<ui::font_atlas_t> atlas = ui::try_bake_font(
        assets::read_asset_bytes(ui::DEFAULT_FONT_PATH),
        {std::floor(18.f * scale), std::floor(28.f * scale), std::floor(48.f * scale)});
    if (!atlas)
      fatal_error("client::init: could not bake the UI font from '{}'", ui::DEFAULT_FONT_PATH);

    g_ui_font = std::make_unique<ui::ui_font_t>();
    g_ui_font->atlas = renderer::register_texture(atlas->image, /*srgb*/ false);
    g_ui_font->sizes = atlas->sizes;
    state_manager::get_client_context().font = g_ui_font.get();
  }

  // Set initial state
  state_manager::init();
  state_manager::switch_to(game_state::main_menu);

  // Bring up audio and lend the shared context a borrowed pointer. A failed
  // audio init is non-fatal — the engine becomes inert and play_* no-op — so
  // a machine with no sound device still runs.
  g_audio = std::make_unique<audio_system_t>();
  g_audio->init(*cvar_state);
  state_manager::get_client_context().audio = g_audio.get();

  return true;
}

bool Tick()
{
  

  client::input::new_frame();

  SDL_Event event;
  while (SDL_PollEvent(&event))
  {
    renderer::process_event(&event);
    input::process_sdl_event(&event);

    if (event.type == SDL_QUIT)
    {
      return false;
    }
    // Resize needs no handling here: new_frame() rebuilds the swapchain when
    // vkAcquireNextImageKHR reports it out of date, and skips that frame.
  }

  // Compute real dt
  auto now = std::chrono::high_resolution_clock::now();
  if (!g_tick_time_initialized)
  {
    g_last_tick_time = now;
    g_tick_time_initialized = true;
  }
  float dt = static_cast<float>(
      std::chrono::duration<double>(now - g_last_tick_time).count());
  g_last_tick_time = now;
  if (dt > 0.25f)
    dt = 0.25f;
  if (dt < 0.0001f)
    dt = 0.0001f;

  // Apply time scale. Same cvar_state_t the integrated launcher scales the
  // SERVER accumulator from, so slow-mo now slows simulation and rendering
  // together instead of only the half that happened to link this DLL's copy.
  float timescale = state_manager::get_client_context().cvars->cl_timescale;
  if (timescale < 0.01f)
    timescale = 0.01f;
  dt *= timescale;

  // Update state
  if (!state_manager::update(dt))
  {
    return false;
  }

  // Render. The two-phase shape is ImGui's doing and nothing else's: the UI is
  // built imperatively between the calls. Everything the 3D scene needs is a
  // value collected in build_frame, and render_frame runs EXACTLY ONCE.
  if (!renderer::new_frame())
  {
    return true; // minimized, or the swapchain is being rebuilt
  }

  // Ordered the way the frame composites, bottom to top: the UI list first, the
  // ImGui panels over it.
  //
  // Retained across frames so the per-frame pass list and UI vertices cost no
  // allocation after the first few frames.
  static std::vector<renderer::view_pass_t> frame_passes;
  static renderer::ui_draw_list_t           frame_ui;
  frame_passes.clear();
  frame_ui.clear();
  state_manager::build_frame(dt, frame_passes, frame_ui);

  // Appended after the state's own UI so a banner draws over it, and appended
  // HERE rather than by each state because set_announcement() is fire-and-forget
  // from thirty call sites that share no state.
  hud::announcement_t &announcement = hud::current_announcement();
  hud::advance_announcement(announcement, dt);
  hud::draw_announcement(frame_ui, *g_ui_font, renderer::screen_size(),
                         renderer::display_scale(), announcement);

  state_manager::draw_imgui_panels();

  // Global console Overlay
  if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false))
  {
    client::console::get().toggle();
  }
  client::console::get().draw();

  renderer::render_frame(frame_passes, frame_ui);

  // WHAT WAS ON SCREEN, recorded at the moment it becomes true. A shot is
  // judged against the world the shooter was looking at, and this is the only
  // point in the program that knows a frame was actually handed over -- the
  // early return above means a minimized or rebuilding client presents nothing
  // and correctly records nothing.
  //
  // The stamp is the same clock the input edges carry, so the lookup that pairs
  // them needs no calibration between two clocks read at different points in
  // the frame. It is PRESENT time, not photon time: queued frames, the
  // compositor and the panel all sit past this line, and cl_display_latency_ms
  // is what the reader subtracts to cross them.
  {
    client_context_t &frame_context = state_manager::get_client_context();
    client::push_drawn_frame(frame_context.replication.drawn_history,
                             {input::arrival_clock_now(),
                              frame_context.replication.interpolation_cursor.tick,
                              frame_context.replication.interpolation_cursor.newest_received_tick});
  }

  return true;
}

void shutdown()
{
  timed_function();
  log_terminal("--- Shutting down Client ---");

  state_manager::shutdown();

  input::shutdown();

  // Before renderer::shutdown, which frees the atlas texture the font points at.
  state_manager::get_client_context().font = nullptr;
  g_ui_font.reset();

  renderer::shutdown();

  if (g_audio)
  {
    state_manager::get_client_context().audio = nullptr;
    g_audio->shutdown();
    g_audio.reset();
  }

  if (g_window)
  {
    SDL_DestroyWindow(g_window);
  }
  SDL_Quit();
}

void set_integrated_server_session(const shared::game_session_t *session)
{
  state_manager::get_client_context().server_session = session;
}

static server_map_reload_hook_t g_server_map_reload_hook = nullptr;

void set_server_map_reload_hook(server_map_reload_hook_t hook)
{
  g_server_map_reload_hook = hook;
}

// Internal accessor for tool_editor_state.cpp / play_state.cpp.
bool invoke_server_map_reload_hook(const std::string &map_path)
{
  if (!g_server_map_reload_hook)
    return false;
  return g_server_map_reload_hook(map_path.c_str());
}

} // namespace client
