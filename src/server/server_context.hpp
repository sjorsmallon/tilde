#pragma once

#include "../shared/game_session.hpp"
#include "../shared/network/server_connection_state.hpp"
#include "../shared/physics.hpp"
#include <memory>

namespace server
{

// 'Context' refers to the bundle of data required for the active game session
// (entities, network state). This is distinct from 'State' (e.g. Initializing,
// Running, Shutdown) which refers to the FSM state.
struct server_context_t
{
  network::Server_Connection_State net;
  shared::game_session_t session;
  // Heap-allocated so construction is deferred past static init: physics_state_t
  // contains a JPH::TempAllocatorImpl that calls AlignedAlloc (Jolt's allocator)
  // in its constructor, which crashes if jolt_init() hasn't been called yet.
  // g_state is a file-scope global, so all value members would construct before main().
  std::unique_ptr<physics_state_t> physics;
};

} // namespace server
