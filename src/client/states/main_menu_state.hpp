#pragma once

#include "../game_state.hpp"
#include "../state_manager.hpp"
#include "imgui.h"

namespace client
{

class MainMenuState : public IGameState
{
public:
  void update(float dt) override {}

  void render_ui() override
  {
    ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 250), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Main Menu", nullptr, ImGuiWindowFlags_NoCollapse))
    {
      if (ImGui::Button("Start Game", ImVec2(-1, 40)))
      {
        state_manager::switch_to(GameStateKind::Play);
      }


      ImGui::Dummy(ImVec2(0, 10));
      if (ImGui::Button("Tool Editor", ImVec2(-1, 40)))
      {
        log_terminal("Tool Editor button clicked");
        state_manager::switch_to(GameStateKind::ToolEditor);
      }

      ImGui::Dummy(ImVec2(0, 10));

      if (ImGui::Button("Quit", ImVec2(-1, 40)))
      {
        state_manager::request_exit();
      }
    }
    ImGui::End();
  }
};

} // namespace client
