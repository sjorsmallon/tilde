#pragma once

#include "../game_state.hpp"
#include "../state_manager.hpp"
#include "editor_state.hpp"
#include "imgui.h"
#include "play_state.hpp"

namespace client {

class MainMenuState : public IGameState {
public:
  void update(float dt) override {}

  void render_ui() override {
    ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 250), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Main Menu", nullptr, ImGuiWindowFlags_NoCollapse)) {
      if (ImGui::Button("Start Game", ImVec2(-1, 40))) {
        state_manager::set_state(std::make_unique<PlayState>());
      }

      ImGui::Dummy(ImVec2(0, 10));

      if (ImGui::Button("Editor Mode", ImVec2(-1, 40))) {
        state_manager::set_state(std::make_unique<EditorState>());
      }

      ImGui::Dummy(ImVec2(0, 10));

      if (ImGui::Button("Quit", ImVec2(-1, 40))) {
        state_manager::request_exit();
      }
    }
    ImGui::End();
  }
};

} // namespace client
