#include "play_state.hpp"
#include "../console.hpp"
#include "../renderer.hpp"
#include "../shared/map.hpp"
#include "../state_manager.hpp"

// TODO: WORKING ON MAP LOADING!.

namespace client
{

void PlayState::on_enter()
{
  // console::log("Entered Play State");
  renderer::draw_announcement("Play State");
  shared::map_t map;
  //   if (shared::load_map("levels/start.map", map)) { ... }
}

void PlayState::update(float dt)
{
  // Game logic here
}

void PlayState::render_ui()
{
  // Console is now global in client_impl.cpp

  ImGui::ShowDemoWindow();

  // Simple overlay to show we are in PlayState
  ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Game State", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::Text("Current State: PLAY");
    if (ImGui::Button("Back to Menu"))
    {
      state_manager::switch_to(GameStateKind::MainMenu);
    }
  }
  ImGui::End();
}

void PlayState::render_3d(VkCommandBuffer cmd)
{
  // 3D rendering calls would go here
}

} // namespace client
