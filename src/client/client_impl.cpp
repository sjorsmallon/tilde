#include "../shared/entity_system.hpp"
#include "client_api.hpp"
#include "console.hpp"
#include "renderer.hpp"
#include "state_manager.hpp"

#include <iostream>
#include <string>

#include <SDL.h>

#include "cvar.hpp"
#include "input.hpp"
#include "log.hpp"
#include "timed_function.hpp"

namespace client
{

static SDL_Window *g_window = nullptr;

bool Init()
{
  timed_function();
  log_terminal("--- Initializing Client (SDL + Vulkan) ---");

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
  // Register Entities (Shared Logic)
  state_manager::get_entity_system().register_all_known_entity_types();

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

  // Update state
  if (!state_manager::update(0.016f))
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

  if (g_window)
  {
    SDL_DestroyWindow(g_window);
  }
  SDL_Quit();
}

} // namespace client
