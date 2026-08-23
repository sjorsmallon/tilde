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

  // A batch of two, because the batch is the only input message on the wire and
  // unpacking it into one inbox entry per input is the part worth exercising.
  game::C2S_ClientInputBatch batch;
  for (int input_number : {9, 10})
  {
    game::C2S_ClientInput *input = batch.add_inputs();
    input->set_input_number(input_number);
    input->set_buttons_bitfield(0xABCDull); // a payload marker, to prove the
                                            // bytes survive the round trip
    auto *va = input->mutable_viewangles();
    va->set_yaw(45.0f);
    va->set_pitch(0.0f);
  }

  std::vector<uint8> serialized_data(batch.ByteSizeLong());
  batch.SerializeToArray(serialized_data.data(), serialized_data.size());

  uint8 next_message_id = 0;
  auto packets = convert_to_packets(
      serialized_data, static_cast<uint8>(Message_Type::C2S_ClientInputBatch),
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
  // The 50ms sleep above is what makes this deterministic: the datagrams are
  // already queued, so the drain finds them and stops. The cap is the livelock
  // guard, not a window to wait in.
  poll_network(state, server_socket, server_receive_drain_cap_in_datagrams, 140,
               inbox);

  // Verify
  if (inbox.inputs.empty())
  {
    std::cerr << "Failed to receive/reassemble moves!" << std::endl;
    // Debug
    std::cerr << "Partial packets count: " << state.partial_packets[0].size()
              << std::endl;
    assert(false);
  }

  // Both inputs of the batch, as separate inbox entries and in the order they
  // were packed: the receive side unpacks, it does not deduplicate or reorder.
  std::vector<const game::C2S_ClientInput *> inputs_for_player_0;
  for (const auto &[pidx, input] : inbox.inputs)
  {
    if (pidx == 0)
      inputs_for_player_0.push_back(&input);
  }

  assert(inputs_for_player_0.size() == 2 &&
         "a batch of two must land as two inbox inputs");
  assert(inputs_for_player_0[0]->input_number() == 9);
  assert(inputs_for_player_0[1]->input_number() == 10);
  assert(inputs_for_player_0[0]->buttons_bitfield() == 0xABCDull);
  assert(inputs_for_player_0[1]->buttons_bitfield() == 0xABCDull);
  std::cout << "  -> Input batch reassembled and unpacked correctly!" << std::endl;

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
