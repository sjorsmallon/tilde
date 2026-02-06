#include "state_manager.hpp"
#include "../shared/entity_system.hpp"
#include "log.hpp"
#include "states/editor_state.hpp"
#include "states/main_menu_state.hpp"
#include "states/play_state.hpp"
#include "states/tool_editor_state.hpp"
#include <unordered_map>
#include <utility>

namespace client
{
namespace state_manager
{

static IGameState *g_active_state = nullptr;
static bool g_should_exit = false;

static std::unordered_map<GameStateKind, std::unique_ptr<IGameState>> g_states;
static client_context_t g_client_context;

void shutdown()
{
  if (g_active_state)
  {
    g_active_state->on_exit();
    g_active_state = nullptr;
  }
  g_states.clear();
  g_client_context.session.entity_system.reset();
}

void init()
{
  g_states[GameStateKind::MainMenu] = std::make_unique<MainMenuState>();
  g_states[GameStateKind::Play] = std::make_unique<PlayState>();
  g_states[GameStateKind::Editor] = std::make_unique<EditorState>();
  g_states[GameStateKind::ToolEditor] = std::make_unique<ToolEditorState>();
}

void switch_to(GameStateKind kind)
{
  log_terminal(std::format("Switching to state: {}", (int)kind));
  IGameState *next_state = g_states[kind].get();
  if (g_active_state)
  {
    g_active_state->on_exit();
  }

  g_active_state = next_state;

  if (g_active_state)
  {
    g_active_state->on_enter();
  }
}

// set_state removed.

IGameState *get_current_state() { return g_active_state; }

bool update(float dt)
{
  if (g_should_exit)
  {
    return false;
  }
  if (g_active_state)
  {
    g_active_state->update(dt);
  }
  return !g_should_exit;
}

void request_exit() { g_should_exit = true; }

void render_ui()
{
  if (g_active_state)
  {
    g_active_state->render_ui();
  }
}

void render_3d(VkCommandBuffer cmd)
{
  if (g_active_state)
  {
    g_active_state->render_3d(cmd);
  }
}

shared::Entity_System &get_entity_system()
{
  return g_client_context.session.entity_system;
}

client_context_t &get_client_context() { return g_client_context; }

} // namespace state_manager
} // namespace client
