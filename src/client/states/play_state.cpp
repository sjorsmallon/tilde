#include "play_state.hpp"
#include "../console.hpp"
#include "../renderer.hpp"
#include "../shared/map.hpp"
#include "../shared/network/network_types.hpp"
#include "../state_manager.hpp"

// TODO: WORKING ON MAP LOADING!.

namespace client
{

void PlayState::on_enter()
{
  // console::log("Entered Play State");
  renderer::draw_announcement("Play State: Connecting...");

  auto &ctx = state_manager::get_client_context();
  if (!ctx.connection_state.connected)
  {
    if (!ctx.connection_state.socket.open(network::client_port_number))
    {
      // Try random port if fixed fails? Or just fail.
      // Fallback logic could go here.
      renderer::draw_announcement("Failed to open socket");
    }
    // Assuming server is localhost for now or set elsewhere?
    // Default to localhost:2020 if not set.
    if (ctx.connection_state.server_address.port == 0)
    {
      ctx.connection_state.server_address =
          network::Address(127, 0, 0, 1, network::server_port_number);
    }

    // protobuf.
    game::NetCommand cmd;
    auto *connect = cmd.mutable_connect();
    connect->set_protocol_version(1);
    connect->set_player_name("Sjors"); // TODO: Get from UI

    network::send_protobuf_message(ctx.connection_state, cmd);
  }
}

void PlayState::update(float dt)
{
  auto &ctx = state_manager::get_client_context();
  network::ClientInbox inbox;
  network::poll_client_network(ctx.connection_state, 0.005, inbox);

  for (const auto &cmd : inbox.net_commands)
  {
    if (cmd.has_accept())
    {
      ctx.connection_state.connected = true;
      renderer::draw_announcement("Connected!");

      if (ctx.session.map_name != cmd.accept().map_name())
      {
        shared::map_t temp_map;
        // Ensure path is correct, maybe server sends full path or just name?
        // Assuming relative path for now.
        std::string map_path = "levels/" + cmd.accept().map_name();
        if (shared::load_map(map_path, temp_map))
        {
          shared::init_session_from_map(ctx.session, temp_map);
          ctx.session.map_name = cmd.accept().map_name();
        }
      }
    }
    if (cmd.has_reject())
    {
      renderer::draw_announcement(
          ("Connection Rejected: " + cmd.reject().reason()).c_str());
    }
  }

  // TODO: Handle Entity Replication here
  // for(const auto& update : inbox.entity_updates) { ... }

  // Game logic here
}

void PlayState::render_ui()
{
  // Console is now global in client_impl.cpp

  ImGui::ShowDemoWindow();

  // Simple overlay to show we are in PlayState
  ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
  if (ImGui::Begin("Game State", nullptr,
                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                       ImGuiWindowFlags_AlwaysAutoResize))
  {
    ImGui::Text("Current State: PLAY");
    if (ImGui::Button("Back to Menu"))
    {
      state_manager::switch_to(GameStateKind::MainMenu);
    }
  }
  ImGui::End();
}

void PlayState::render_3d(VkCommandBuffer cmd)
{
  // 3D rendering calls would go here
}

} // namespace client
