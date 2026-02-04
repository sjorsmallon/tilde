#pragma once

#include "../shared/game_session.hpp"
#include "../shared/network/client_connection_state.hpp"

namespace client
{

// 'Context' refers to the bundle of data required for the active game session
// (entities, network state). This is distinct from 'State' (e.g. MainMenu,
// Play, Editor) which refers to the FSM state of the application.
struct client_context_t
{
  shared::game_session_t session;
  network::Client_Connection_State connection_state;
};

} // namespace client