#pragma once

#include "../console.hpp"
#include "../game_state.hpp"
#include "../state_manager.hpp" // For changing state back if needed
#include "imgui.h"

namespace client {

class PlayState : public IGameState {
public:
  void on_enter() override;
  void update(float dt) override;
  void render_ui() override;
  void render_3d(VkCommandBuffer cmd) override;
};

} // namespace client
