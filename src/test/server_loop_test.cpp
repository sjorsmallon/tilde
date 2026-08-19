#include "../shared/network/packet.hpp"
#include "../shared/network/server_transport_layer.hpp"
#include "../shared/network/udp_socket.hpp"
#include "game.pb.h"
#include <cassert>
#include <iostream>
#include <thread>

using namespace network;

void test_receive_and_reassembly()
{
  std::cout << "[TEST] Testing Receive and Reassembly..." << std::endl;

  Server_Transport_Layer state;
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

  occupy_client_slot(state, 0, client_addr, 100);

  // A batch of two, because the batch is the only move message on the wire and
  // unpacking it into one inbox entry per move is the part worth exercising.
  game::C2S_PlayerMoveBatch batch;
  for (int command_number : {9, 10})
  {
    game::C2S_PlayerMoveCommand *move = batch.add_moves();
    move->set_command_number(command_number);
    move->set_forwardmove(127.0f);
    move->set_sidemove(0.0f);
    auto *va = move->mutable_viewangles();
    va->set_yaw(45.0f);
    va->set_pitch(0.0f);
  }

  std::vector<uint8> serialized_data(batch.ByteSizeLong());
  batch.SerializeToArray(serialized_data.data(), serialized_data.size());

  uint8 next_message_id = 0;
  auto packets = convert_to_packets(
      serialized_data, static_cast<uint8>(Message_Type::C2S_PlayerMoveBatch),
      next_message_id);

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
  poll_network(state, server_socket, 0.1, 140, inbox); // 100ms window

  // Verify
  if (inbox.moves.empty())
  {
    std::cerr << "Failed to receive/reassemble moves!" << std::endl;
    // Debug
    std::cerr << "Partial packets count: " << state.partial_packets[0].size()
              << std::endl;
    assert(false);
  }

  // Both moves of the batch, as separate inbox entries and in the order they
  // were packed: the receive side unpacks, it does not deduplicate or reorder.
  std::vector<const game::C2S_PlayerMoveCommand *> moves_for_player_0;
  for (const auto &[pidx, move] : inbox.moves)
  {
    if (pidx == 0)
      moves_for_player_0.push_back(&move);
  }

  assert(moves_for_player_0.size() == 2 &&
         "a batch of two must land as two inbox moves");
  assert(moves_for_player_0[0]->command_number() == 9);
  assert(moves_for_player_0[1]->command_number() == 10);
  assert(moves_for_player_0[0]->forwardmove() == 127.0f);
  assert(moves_for_player_0[1]->forwardmove() == 127.0f);
  std::cout << "  -> Move batch reassembled and unpacked correctly!" << std::endl;

  // Arrival stamps the slot: this is what sv_timeout measures silence against.
  assert(state.latest_packet_tick[0] == 140);

  release_client_slot(state, 0);
  assert(!state.slot_occupied[0]);
  assert(state.latest_packet_tick[0] == 0);
  std::cout << "  -> Slot liveness stamped and released!" << std::endl;
}

int main()
{
  test_receive_and_reassembly();
  std::cout << "[TEST] All tests passed." << std::endl;
  return 0;
}
