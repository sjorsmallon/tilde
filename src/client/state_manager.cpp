#include "state_manager.hpp"
#include "../shared/entity_system.hpp"
#include "log.hpp"
#include "states/main_menu_state.hpp"
#include "states/play_state.hpp"
#include "states/tool_editor_state.hpp"
#include "states/shader_editor_state.hpp"
#include <unordered_map>
#include <utility>

namespace client
{
namespace state_manager
{

static Game_State *g_active_state = nullptr;
static bool g_should_exit = false;

static std::unordered_map<game_state, std::unique_ptr<Game_State>> g_states;
static client_context_t g_client_context;

void shutdown()
{
  if (g_active_state)
  {
    g_active_state->on_exit();
    g_active_state = nullptr;
  }
  g_states.clear();
  g_client_context.world.session.entity_system.reset();
}

void init()
{
  g_states[game_state::main_menu] = std::make_unique<Main_Menu_State>();
  g_states[game_state::play] = std::make_unique<Play_State>();
  g_states[game_state::tool_editor] = std::make_unique<Tool_Editor_State>();
  g_states[game_state::shader_editor] = std::make_unique<Shader_Editor_State>();
}

void switch_to(game_state kind)
{
  log_terminal("Switching to state: {}", to_string(kind));
  Game_State *next_state = g_states[kind].get();
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

Game_State *get_current_state() { return g_active_state; }

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

void draw_imgui_panels()
{
  if (g_active_state)
  {
    g_active_state->draw_imgui_panels();
  }
}

void build_frame(float delta_seconds, std::vector<renderer::view_pass_t> &passes,
                 renderer::ui_draw_list_t &ui)
{
  if (g_active_state)
  {
    g_active_state->build_frame(delta_seconds, passes, ui);
  }
}

shared::Entity_System &get_entity_system()
{
  return g_client_context.world.session.entity_system;
}

client_context_t &get_client_context() { return g_client_context; }

} // namespace state_manager
} // namespace client
