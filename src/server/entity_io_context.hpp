#pragma once

#include "../shared/entity_uid.hpp"

#include <cstdint>

namespace server
{

struct server_context_t;

// What every action handler is given, and the counterpart of
// cvars::command_context_t: the shapes no .def declaration implies.
//
// It is hand-written and it lives on the SERVER side, because it holds a
// server_context_t& -- which is also why the generated declarations only ever
// name it through a reference. Wiring is server-only; results reach the client
// as replicated state.
struct input_context_t
{
  server_context_t& server;

  // Who CAUSED the signal that led here -- the player who walked into the
  // trigger, the attacker who fired the shot. null_entity_uid when nothing
  // did: a system emitting on its own clock has no activator.
  //
  // A handler must tolerate this naming nobody: a queued action's delay can
  // outlive the player who set it off, so the uid may resolve to nothing by
  // the time the drain reaches it.
  shared::entity_uid_t activator = shared::null_entity_uid;

  uint32_t tick = 0;
};

} // namespace server
