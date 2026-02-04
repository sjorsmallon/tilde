#pragma once

#include "client_context.hpp"
#include "game_state.hpp"
#include <memory>

namespace shared
{
struct Entity_System;
}

namespace client
{

// Global state management functions
namespace state_manager
{

// Shutdown and cleanup all states
void shutdown();

// Initialize all states
void init();

// Switch to a new state by Enum
void switch_to(GameStateKind kind);

// Get the current state (unsafe, can be null)
IGameState *get_current_state();

// Update the current state. Returns false if the game should exit.
bool update(float dt);

// Request the game to exit
void request_exit();

// Render UI for the current state
void render_ui();

// Render 3D for the current state
// Render 3D for the current state
void render_3d(VkCommandBuffer cmd);

// Get access to the shared entity system
// Get access to the shared entity system (convenience wrapper around context)
struct shared::Entity_System &get_entity_system();

// Get access to the full client context
client_context_t &get_client_context();

} // namespace state_manager

} // namespace client
