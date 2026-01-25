#pragma once

#include "game_state.hpp"
#include <memory>

namespace client {

// Global state management functions
namespace state_manager {

// Shutdown and cleanup current state
void shutdown();

// Switch to a new state. Exits current state, Enters new state.
void set_state(std::unique_ptr<IGameState> new_state);

// Get the current state (unsafe, can be null)
IGameState *get_current_state();

// Update the current state. Returns false if the game should exit.
bool update(float dt);

// Request the game to exit
void request_exit();

// Render UI for the current state
void render_ui();

// Render 3D for the current state
void render_3d(VkCommandBuffer cmd);

} // namespace state_manager

} // namespace client
