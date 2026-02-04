#pragma once

#include "../shared/game_session.hpp"
#include "../shared/network/server_connection_state.hpp"

namespace server
{

// 'Context' refers to the bundle of data required for the active game session
// (entities, network state). This is distinct from 'State' (e.g. Initializing,
// Running, Shutdown) which refers to the FSM state.
struct server_context_t
{
  network::Server_Connection_State net;
  shared::game_session_t session;
};

} // namespace server
