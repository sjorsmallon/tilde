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

namespace state_manager
{

// shutdown and cleanup all states
void shutdown();

// Initialize all states
void init();

// Switch to a new state by Enum
void switch_to(game_state kind);

// Get the current state (unsafe, can be null)

// Update the current state. Returns false if the game should exit.
bool update(float dt);

// Request the game to exit
void request_exit();

// Render UI for the current state
void draw_imgui_panels();

// Append the current state's view passes and screen-space UI to the frame
void build_frame(float delta_seconds, std::vector<renderer::view_pass_t> &passes,
                 renderer::ui_draw_list_t &ui);

// Get access to the full client context
client_context_t &get_client_context();

} // namespace state_manager

} // namespace client
