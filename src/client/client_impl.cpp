#include "../shared/entity_system.hpp"
#include "audio/audio_system.hpp"
#include "client_api.hpp"
#include "console.hpp"
#include "cosmetic_events.hpp"
#include "renderer.hpp"
#include "state_manager.hpp"

#include <memory>

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

static SDL_Window *g_window = nullptr;
static std::chrono::high_resolution_clock::time_point g_last_tick_time;
static bool g_tick_time_initialized = false;
static std::unique_ptr<audio_system_t> g_audio;

bool Init(cvars::cvar_state_t *cvar_state, cvars::command_table_t *command_table)
{
  timed_function();
  log_terminal("--- Initializing Client (SDL + Vulkan) ---");

  if (!cvar_state || !command_table)
  {
    log_error("client::Init: the launcher must own and pass a cvar_state_t and "
              "a command_table_t (see cvar_def.md)");
    return false;
  }

  // Before anything that could read a cvar or run a console line. The bind call
  // fills this DLL's @Client handler slots; the linker already proved every
  // symbol it references exists, so there is nothing to verify at runtime.
  state_manager::get_client_context().cvars    = cvar_state;
  state_manager::get_client_context().commands = command_table;
  cvars::bind_client_commands(*command_table);
  Console::Get().SetCVarState(cvar_state, command_table);

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
  {
    log_error("SDL_Init Error: {}", SDL_GetError());
    return false;
  }

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

  if (!renderer::Init(g_window))
  {
    log_error("Renderer Init Failed");
    return false;
  }

  // Set initial state
  state_manager::init();
  state_manager::switch_to(GameStateKind::MainMenu);

  // Register Entities (Shared Logic)
  state_manager::get_entity_system().register_all_known_entity_types();

  // Bind every cosmetic-effect handler. Each effect_type_t maps to exactly
  // one function — registration must happen before the first snapshot can
  // arrive, so it lives in client Init() rather than PlayState::on_enter.
  register_all_effect_handlers();

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
  timed_function();

  client::input::new_frame();

  SDL_Event event;
  while (SDL_PollEvent(&event))
  {
    renderer::ProcessEvent(&event);
    input::process_sdl_event(&event);

    if (event.type == SDL_QUIT)
    {
      return false;
    }
    // Resize handling is done inside renderer::BeginFrame via queries usually,
    // or we can pass it.
    // NOTE: In the previous code, resize triggered `g_swapchain_rebuild =
    // true`. In our new `renderer.cpp`, `BeginFrame` handles checking for
    // `VK_ERROR_OUT_OF_DATE`. Explicit resize event handling might be needed if
    // we want to be proactive.
    // However, `renderer.cpp` as written checks `vkAcquireNextImage` result and
    // rebuilds. But for window resize events, we might want to flag it?
    // Let's rely on Vulkan returning OutOfDate for now, or assume the user is
    // happy with the current implementation which checks `g_swapchain_rebuild`
    // inside renderer (which is currently global static in renderer.cpp, but
    // how is it set??) Ah, `renderer.cpp` globals are static. But the SDL event
    // loop here sees the event. The previous code had: if (event.type ==
    // SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED ...)
    //   g_swapchain_rebuild = true;
    //
    // I need to tell renderer to rebuild!
    // Or add `renderer::HandleResize()`.
    // Or just `renderer::ProcessEvent` should handle it?
    // YES, `renderer::ProcessEvent` should probably handle it if we move that
    // logic there.
    // OR we just rely on `BeginFrame` failing to acquire and rebuilding.
    // But SDL might not trigger "OutOfDate" immediately on all platforms?
    // Let's assume proactive is better.
    // I'll add `renderer::RequestSwapchainRebuild()` or similar?
    // Or just let `ProcessEvent` handle it.
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

  // Render
  VkCommandBuffer cmd = renderer::BeginFrame();
  if (cmd == VK_NULL_HANDLE)
  {
    return true; // Skip frame
  }

  state_manager::render_ui();

  // Global Console Overlay
  if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false))
  {
    client::Console::Get().Toggle();
  }
  client::Console::Get().Draw();

  state_manager::pre_render(cmd);
  renderer::BeginRenderPass(cmd);
  state_manager::render_3d(cmd);
  renderer::EndFrame(cmd);

  return true;
}

void Shutdown()
{
  timed_function();
  log_terminal("--- Shutting down Client ---");

  state_manager::shutdown();
  renderer::Shutdown();

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
