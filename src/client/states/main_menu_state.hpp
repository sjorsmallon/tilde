#pragma once

#include "../game_state.hpp"
#include "../input.hpp"
#include "../state_manager.hpp"
#include "../../shared/network/network_types.hpp"
#include "../../shared/network/udp_socket.hpp"
#include "imgui.h"
#include <string>

namespace client
{

class Main_Menu_State : public Game_State
{
public:
  void update(float dt) override
  {
    if (input::is_key_pressed(input::key_t::Escape))
    {
      state_manager::request_exit();
    }
  }

  void render_ui() override
  {
    ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 250), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Main Menu", nullptr, ImGuiWindowFlags_NoCollapse))
    {
      if (ImGui::Button("Start Game", ImVec2(-1, 40)))
      {
        // Leaves requested_server_address at its loopback default.
        state_manager::switch_to(game_state::play);
      }

      ImGui::Dummy(ImVec2(0, 10));
      ImGui::SetNextItemWidth(-1);
      ImGui::InputTextWithHint("##join_address", "ip or ip:port",
                               join_address_buffer,
                               sizeof(join_address_buffer));
      if (ImGui::Button("Join Game", ImVec2(-1, 40)))
      {
        join_game();
      }
      if (!join_error.empty())
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", join_error.c_str());


      ImGui::Dummy(ImVec2(0, 10));
      if (ImGui::Button("Tool Editor", ImVec2(-1, 40)))
      {
        log_terminal("Tool Editor button clicked");
        state_manager::switch_to(game_state::tool_editor);
      }

      ImGui::Dummy(ImVec2(0, 10));
      if (ImGui::Button("Shader Editor", ImVec2(-1, 40)))
      {
        state_manager::switch_to(game_state::shader_editor);
      }

      ImGui::Dummy(ImVec2(0, 10));

      if (ImGui::Button("Quit", ImVec2(-1, 40)))
      {
        state_manager::request_exit();
      }
    }
    ImGui::End();
  }

private:
  // Free-typed endpoint for Join Game. Prefilled with loopback because that is
  // the common case while developing against a local MyGame_Server.
  char join_address_buffer[64] = "127.0.0.1:9999";

  // Non-empty while the last Join attempt failed to parse. Shown under the
  // button and cleared by the next successful parse -- a bad address must not
  // switch state and then fail silently somewhere in the handshake.
  std::string join_error;

  // Resolves the typed text and, only if it resolves, hands the endpoint to
  // Play_State through the client context. See client_context.hpp.
  void join_game()
  {
    ::network::Address server_address;
    if (!::network::Address::parse_endpoint(join_address_buffer,
                                            ::network::server_port_number,
                                            server_address, join_error))
      return;

    state_manager::get_client_context().requested_server_address =
        server_address;
    state_manager::switch_to(game_state::play);
  }
};

} // namespace client
