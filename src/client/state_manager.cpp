#include "state_manager.hpp"
#include "log.hpp"
#include <utility>

namespace client {
namespace state_manager {

static std::unique_ptr<IGameState> g_current_state = nullptr;
static bool g_should_exit = false;

void shutdown() {
  if (g_current_state) {
    g_current_state->on_exit();
    g_current_state = nullptr;
  }
}

void set_state(std::unique_ptr<IGameState> new_state) {
  // We defer state changes to the beginning of update usually,
  // but for simplicity let's do it immediately if safe,
  // or handle "pending" if we are in the middle of a frame.
  // For now: Immediate switching (simplest).
  // Warning: Don't change state inside a state's render function if that causes
  // issues.

  if (g_current_state) {
    g_current_state->on_exit();
  }

  g_current_state = std::move(new_state);

  if (g_current_state) {
    g_current_state->on_enter();
  }
}

IGameState *get_current_state() { return g_current_state.get(); }

bool update(float dt) {
  if (g_should_exit) {
    return false;
  }
  if (g_current_state) {
    g_current_state->update(dt);
  }
  return !g_should_exit;
}

void request_exit() { g_should_exit = true; }

void render_ui() {
  if (g_current_state) {
    g_current_state->render_ui();
  }
}

void render_3d(VkCommandBuffer cmd) {
  if (g_current_state) {
    g_current_state->render_3d(cmd);
  }
}

} // namespace state_manager
} // namespace client
