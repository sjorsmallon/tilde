#include "../shared/network/packet.hpp"
#include "../shared/network/server_connection_state.hpp"
#include "../shared/network/udp_socket.hpp"
#include "game.pb.h"
#include <cassert>
#include <iostream>
#include <thread>

using namespace network;

void test_receive_and_reassembly()
{
  std::cout << "[TEST] Testing Receive and Reassembly..." << std::endl;

  Server_Connection_State state;
  Udp_Socket server_socket;
  Udp_Socket client_socket;

  // Open sockets
  if (!server_socket.open(9001))
  {
    std::cerr << "Failed to open server socket on 9001" << std::endl;
    exit(1);
  }
  if (!client_socket.open(0))
  {
    std::cerr << "Failed to open client socket" << std::endl;
    exit(1);
  }

  // Register "client" in state manually so receive_messages accepts packets
  Address client_addr(127, 0, 0, 1, 9002);
  // Actually Udp_Socket wraps a socket handle. If we bind 0, we need to ask OS.
  // For simplicity, bind client to fixed port 9002.
  client_socket.close();
  if (!client_socket.open(9002))
  {
    std::cerr << "Failed to open client socket on 9002" << std::endl;
    exit(1);
  }
  client_addr = Address(127, 0, 0, 1, 9002);

  state.player_slots[0] = true;
  state.player_ips[0] = client_addr;

  // Create a Move Command
  game::CmdMove move;
  move.set_entity_id(10);
  move.mutable_target_position()->set_x(100.0f);
  move.mutable_target_position()->set_y(200.0f);
  move.mutable_target_position()->set_z(300.0f);

  std::vector<uint8> serialized_data(move.ByteSizeLong());
  move.SerializeToArray(serialized_data.data(), serialized_data.size());

  // Convert to packets (Message Type C2S_PlayerMoveCommand = 0 in enum but
  // let's check packet.hpp) packet.hpp: C2S_PlayerMoveCommand is first, so 0.
  // But let's use the enum cast.
  auto packets = convert_to_packets(
      serialized_data, static_cast<uint8>(Message_Type::C2S_PlayerMoveCommand));

  // Set sequencing manually if convert_to_packets doesn't do it fully?
  // convert_to_packets sets sequence_count, sequence_idx. sequence_id is 0.
  // That's fine for first test.

  // Send packets
  Address server_addr(127, 0, 0, 1, 9001);
  for (const auto &p : packets)
  {
    client_socket.send(p, server_addr);
    std::this_thread::sleep_for(std::chrono::milliseconds(
        2)); // Small delay to ensure order? or not. UDP unordered.
  }

  // Allow time for OS to deliver
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  // Server Receive
  ServerInbox inbox;
  poll_network(state, server_socket, 0.1, inbox); // 100ms window

  // Verify
  if (inbox.moves.empty())
  {
    std::cerr << "Failed to receive/reassemble moves!" << std::endl;
    // Debug
    std::cerr << "Partial packets count: " << state.partial_packets[0].size()
              << std::endl;
    assert(false);
  }

  // Find move for player 0
  const TimestampedMove *received_move_ptr = nullptr;
  for (const auto &[pidx, tm] : inbox.moves)
  {
    if (pidx == 0)
    {
      received_move_ptr = &tm;
      break;
    }
  }

  assert(received_move_ptr && "No move found for player 0");
  const auto &received_move = *received_move_ptr;

  // Timestamp might be 0 as packet helper doesn't set it (comment says
  // "Timestamp should be set by sender") We didn't set it in loop. So it's 0.

  assert(received_move.move.entity_id() == 10);
  assert(received_move.move.target_position().x() == 100.0f);
  std::cout << "  -> Move Reassembled Correctly!" << std::endl;
}

int main()
{
  test_receive_and_reassembly();
  std::cout << "[TEST] All tests passed." << std::endl;
  return 0;
}
