#include "../shared/ghost.hpp"
#include "../shared/network/client_transport_layer.hpp"
#include "../shared/network/ghost_transfer.hpp"
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
  Server_Inbox inbox;
  // The 50ms sleep above is what makes this deterministic: the datagrams are
  // already queued, so the drain finds them and stops. The cap is the livelock
  // guard, not a window to wait in.
  poll_network(state, server_socket, server_receive_drain_cap_in_datagrams, 140,
               inbox);

  // Verify
  if (inbox.client_inputs.empty())
  {
    std::cerr << "Failed to receive/reassemble moves!" << std::endl;
    // Debug
    std::cerr << "Partial packets count: " << state.clients[0].partial_packets.size()
              << std::endl;
    assert(false);
  }

  // Both inputs of the batch, as separate inbox entries and in the order they
  // were packed: the receive side unpacks, it does not deduplicate or reorder.
  std::vector<const game::C2S_ClientInput *> inputs_for_player_0;
  for (const auto &[pidx, input] : inbox.client_inputs)
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
  assert(state.clients[0].latest_packet_tick == 140);

  release_client_slot(state, 0);
  assert(!state.clients[0].occupied);
  assert(state.clients[0].latest_packet_tick == 0);
  std::cout << "  -> Slot liveness stamped and released!" << std::endl;
}

// The reliable stream over a REAL socket pair, which is the half
// reliable_stream_test deliberately cannot reach: that one drives both sides by
// hand, so it says nothing about whether the two Packet_Header fields survive
// the wire, whether the client intercepts a block before reassembly, or whether
// the ack actually rides an ordinary outbound datagram.
void test_reliable_stream_round_trip()
{
  std::cout << "[TEST] Testing the reliable stream over UDP..." << std::endl;

  Server_Transport_Layer server_state;
  Udp_Socket server_socket;
  Client_Transport_Layer client_state;

  if (!server_socket.open(9003))
  {
    std::cerr << "Failed to open server socket on 9003" << std::endl;
    exit(1);
  }
  if (!client_state.socket.open(9004))
  {
    std::cerr << "Failed to open client socket on 9004" << std::endl;
    exit(1);
  }

  const Address server_address(127, 0, 0, 1, 9003);
  const Address client_address(127, 0, 0, 1, 9004);
  client_state.server_address = server_address;
  occupy_client_slot(server_state, 0, client_address, 100);

  // A message with no protobuf behind it: the record's type is opaque to the
  // stream, and CmdChangeMap is one the client files as a raw payload.
  game::S2C_ServerMessage message;
  message.set_message("the stream carries this");
  std::vector<uint8> payload(message.ByteSizeLong());
  message.SerializeToArray(payload.data(), static_cast<int>(payload.size()));

  queue_reliable_message(server_state.clients[0].reliable_stream,
                         static_cast<uint8>(Message_Type::S2C_ServerMessage),
                         payload);

  send_reliable_block(server_state, server_socket, 0);
  assert(server_state.clients[0].reliable_stream.block_number == 1);
  assert(server_state.clients[0].reliable_stream.block_length != 0);

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client_Inbox inbox;
  poll_client_network(client_state, client_receive_drain_cap_in_datagrams, inbox);

  assert(inbox.server_text_messages.size() == 1 &&
         "the record came out of the block and through the handler table");
  assert(inbox.server_text_messages[0].message() == "the stream carries this");
  assert(client_state.reliable_stream.received_through == 1);
  std::cout << "  -> Block delivered and framed back into a message!" << std::endl;

  // The ack is not a message: it rides the header of whatever the client sends
  // next, which in the live client is its per-tick input.
  game::C2S_Command any_outbound;
  any_outbound.set_line("noclip 1");
  send_protobuf_message(client_state, any_outbound);

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Server_Inbox server_inbox;
  poll_network(server_state, server_socket, server_receive_drain_cap_in_datagrams,
               141, server_inbox);

  assert(server_state.clients[0].reliable_stream.block_length == 0 &&
         "an ordinary datagram's header freed the block");
  assert(server_state.clients[0].reliable_stream.outbound.empty() &&
         "and the drained stream reclaimed its buffer");
  std::cout << "  -> Ack rode an ordinary datagram and freed the block!" << std::endl;

  release_client_slot(server_state, 0);
  client_state.socket.close();
  server_socket.close();
}

// The C2S half of the same stream, which is the direction that closes the map
// handshake: a lost C2S_RequestMapData used to leave a client waiting forever
// for a transfer the server never started.
//
// Two things are worth the socket here, and neither is reachable by driving the
// halves by hand. The record has to come out of the block and through the SAME
// delivery the unreliable path uses -- reliability is the transport's business,
// so nothing above it may be able to tell which route a message took. And the
// server's ack has to ride an ordinary S2C datagram: the client sends nothing
// while it is Loading, so if the ack did not ride the snapshot, the map package's
// own fragments or the server's blocks, this stream would stall exactly when it
// is carrying the request.
void test_reliable_stream_round_trip_c2s()
{
  std::cout << "[TEST] Testing the C2S reliable stream over UDP..." << std::endl;

  Server_Transport_Layer server_state;
  Udp_Socket server_socket;
  Client_Transport_Layer client_state;

  if (!server_socket.open(9007) || !client_state.socket.open(9008))
  {
    std::cerr << "Failed to open the socket pair" << std::endl;
    exit(1);
  }

  const Address server_address(127, 0, 0, 1, 9007);
  const Address client_address(127, 0, 0, 1, 9008);
  client_state.server_address = server_address;
  occupy_client_slot(server_state, 0, client_address, 300);

  // The two riders, queued in one frame so they share a block -- which is also
  // what makes "rely on order, never on grouping" checkable: they arrive in the
  // order they were queued because the stream cannot reorder them.
  game::C2S_Command command;
  command.set_line("changelevel new_map");
  queue_reliable_protobuf_message(client_state, command);

  const std::vector<uint8> request_payload = {0x11, 0x22, 0x33};
  queue_reliable_client_message(
      client_state, static_cast<uint8>(Message_Type::C2S_RequestMapData),
      request_payload);

  service_client_reliable_stream(client_state);
  assert(client_state.reliable_stream.block_number == 1);
  assert(client_state.reliable_stream.block_length != 0);

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Server_Inbox inbox;
  poll_network(server_state, server_socket, server_receive_drain_cap_in_datagrams,
               301, inbox);

  assert(inbox.developer_console_entries.size() == 1 &&
         "the console line came out of the block and into the inbox the "
         "unreliable path files into");
  assert(inbox.developer_console_entries[0].first == 0);
  assert(inbox.developer_console_entries[0].second == "changelevel new_map");
  assert(inbox.map_data_requests.size() == 1 &&
         "and so did the bitstream-native map request, still raw");
  assert(inbox.map_data_requests[0].second == request_payload);
  assert(server_state.clients[0].reliable_stream.received_through == 1);
  std::cout << "  -> Both records delivered, in the order they were queued!"
            << std::endl;

  // Nothing has freed the block yet: the ack lives in the header of whatever the
  // server sends next, and it has not sent anything.
  assert(client_state.reliable_stream.block_length != 0);

  // An ORDINARY S2C datagram -- not a reliable one. This is the property the
  // Loading client depends on, since the only S2C traffic it gets is the map
  // package's fragments.
  Packet snapshot = {};
  snapshot.header.message_type =
      static_cast<uint8>(Message_Type::S2C_EntityPackage);
  snapshot.header.message_id     = 77;
  snapshot.header.fragment_count = 1;
  snapshot.header.fragment_index = 0;
  snapshot.header.payload_size   = 0;
  send_packet_to_client(server_state, server_socket, 0, snapshot);

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client_Inbox client_inbox;
  poll_client_network(client_state, client_receive_drain_cap_in_datagrams,
                      client_inbox);

  assert(client_state.reliable_stream.block_length == 0 &&
         "an ordinary S2C datagram's header freed our block");
  assert(client_state.reliable_stream.outbound.empty() &&
         "and the drained stream reclaimed its buffer");
  std::cout << "  -> Server ack rode an ordinary datagram and freed the block!"
            << std::endl;

  release_client_slot(server_state, 0);
  client_state.socket.close();
  server_socket.close();
}

// A lossy download, all the way round: the receiver reports a bitmap over the
// real socket, the sender applies it and re-sends exactly the gaps, and the
// completing report frees the transfer.
//
// The S2C fragments are handed to the reassembler directly rather than pushed
// through the socket, which is the one shortcut here -- it is how the test
// chooses WHICH fragments are lost, and paced_transfer_test already covers those
// same fragments going over the wire. Everything in the C2S direction is real.
void test_lossy_transfer_converges()
{
  std::cout << "[TEST] Testing transfer recovery over UDP..." << std::endl;

  Server_Transport_Layer server_state;
  Udp_Socket server_socket;
  Client_Transport_Layer client_state;

  if (!server_socket.open(9005) || !client_state.socket.open(9006))
  {
    std::cerr << "Failed to open the socket pair" << std::endl;
    exit(1);
  }

  const Address server_address(127, 0, 0, 1, 9005);
  const Address client_address(127, 0, 0, 1, 9006);
  client_state.server_address = server_address;
  occupy_client_slot(server_state, 0, client_address, 200);

  std::vector<uint8> payload(20 * MAX_PAYLOAD_SIZE_IN_BYTES);
  for (size_t index = 0; index < payload.size(); ++index)
    payload[index] = static_cast<uint8>(index * 13 + 5);

  begin_paced_transfer(server_state, 0, payload,
                       static_cast<uint8>(Message_Type::S2C_MapData));
  const std::vector<Packet> fragments = server_state.clients[0].outbound_transfer.fragments;
  assert(fragments.size() == 20);

  // Fragments 4 and 17 are lost.
  std::vector<uint8> reassembled;
  for (size_t index = 0; index < fragments.size(); ++index)
  {
    if (index == 4 || index == 17)
      continue;
    assert(!reassemble_fragment(client_state.partial_packets, fragments[index], reassembled) &&
           "the message cannot complete while two fragments are missing");
  }

  report_transfer_progress(client_state);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Server_Inbox server_inbox;
  poll_network(server_state, server_socket, server_receive_drain_cap_in_datagrams,
               201, server_inbox);

  size_t confirmed_count = 0;
  for (bool confirmed : server_state.clients[0].outbound_transfer.confirmed)
    if (confirmed)
      ++confirmed_count;
  assert(confirmed_count == 18 && "the bitmap named exactly what arrived");
  assert(!server_state.clients[0].outbound_transfer.confirmed[4]);
  assert(!server_state.clients[0].outbound_transfer.confirmed[17]);
  std::cout << "  -> Bitmap crossed the wire and named the two gaps!" << std::endl;

  // The repair pass, which must be two fragments and not a restart.
  service_paced_transfers(server_state, server_socket, 8);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  std::vector<Packet> repaired;
  Packet packet;
  Address from;
  while (client_state.socket.receive(packet, from))
    repaired.push_back(packet);

  assert(repaired.size() == 2 && "a two-fragment gap costs two fragments");
  for (const Packet &fragment : repaired)
    assert(fragment.header.fragment_index == 4 || fragment.header.fragment_index == 17);

  // Feeding them in completes the message.
  assert(!reassemble_fragment(client_state.partial_packets, repaired[0], reassembled));
  assert(reassemble_fragment(client_state.partial_packets, repaired[1], reassembled) &&
         "the last missing fragment completes the message");
  assert(reassembled == payload && "and the bytes survived the repair");
  std::cout << "  -> Repair was proportional to the loss and rebuilt the payload!"
            << std::endl;

  // A duplicate arriving after completion must not reopen the bucket -- that is
  // what would report "1 of 20" and re-stream a map we already hold.
  assert(!reassemble_fragment(client_state.partial_packets, fragments[0], reassembled) &&
         "a duplicate after completion is discarded, not re-delivered");

  // And the completing report frees the transfer, so the sender stops waiting.
  std::this_thread::sleep_for(std::chrono::milliseconds(
      static_cast<int>(transfer_receipt_interval_in_seconds * 1000) + 20));
  report_transfer_progress(client_state);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  poll_network(server_state, server_socket, server_receive_drain_cap_in_datagrams,
               202, server_inbox);
  assert(server_state.clients[0].outbound_transfer.fragments.empty() &&
         "a completed bucket keeps confirming, which is what ends the transfer");
  std::cout << "  -> The completing report freed the transfer!" << std::endl;

  release_client_slot(server_state, 0);
  client_state.socket.close();
  server_socket.close();
}


// A ghost travels the way the map does, and this is all three legs over the real
// socket: the announce and the request as records on the two reliable streams,
// the file as a paced transfer. What the game layer adds on top is a hash
// compare on each end, which is what the last assert stands in for.
void test_a_cold_client_receives_the_announced_ghost()
{
  std::cout << "[TEST] Testing the ghost announce, request and transfer over UDP..." << std::endl;

  Server_Transport_Layer server_state;
  Udp_Socket server_socket;
  Client_Transport_Layer client_state;

  if (!server_socket.open(9011) || !client_state.socket.open(9012))
  {
    std::cerr << "Failed to open the socket pair" << std::endl;
    exit(1);
  }

  const Address server_address(127, 0, 0, 1, 9011);
  const Address client_address(127, 0, 0, 1, 9012);
  client_state.server_address = server_address;
  occupy_client_slot(server_state, 0, client_address, 400);

  // A two-runner ghost long enough to need several fragments.
  shared::ghost_t ghost;
  ghost.tickrate_hz      = 60;
  ghost.map_content_hash = 0x1234;
  ghost.run_ticks        = 299;
  for (const entities::Team_Allegiance team :
       {entities::Team_Allegiance::Red, entities::Team_Allegiance::Blu})
  {
    shared::ghost_track_t &track = ghost.tracks.emplace_back();
    track.team = team;
    track.name = team == entities::Team_Allegiance::Red ? "red" : "blu";
    for (uint32_t tick = 0; tick <= ghost.run_ticks; ++tick)
      track.poses.push_back({.position = {static_cast<float>(tick), 0.f, 0.f}, .alive = true});
  }
  const std::vector<uint8> ghost_bytes = shared::serialize_ghost(ghost);
  const uint32_t ghost_hash = shared::compute_ghost_hash(
      Span<const uint8_t>(ghost_bytes.data(), static_cast<uint32_t>(ghost_bytes.size())));

  // 1. The announce, on the S2C reliable stream.
  Bit_Writer announce_writer;
  shared::serialize_ghost_available(
      announce_writer, {.party_size = 2,
                        .ghost_hash = ghost_hash,
                        .byte_count = static_cast<uint32_t>(ghost_bytes.size())});
  queue_reliable_message(server_state.clients[0].reliable_stream,
                         static_cast<uint8>(Message_Type::S2C_GhostAvailable),
                         announce_writer.buffer);
  send_reliable_block(server_state, server_socket, 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Client_Inbox client_inbox;
  poll_client_network(client_state, client_receive_drain_cap_in_datagrams, client_inbox);
  assert(client_inbox.ghost_available_messages.size() == 1);
  Bit_Reader announce_reader(client_inbox.ghost_available_messages[0].data(),
                             client_inbox.ghost_available_messages[0].size());
  const shared::ghost_available_message_t announced =
      shared::deserialize_ghost_available(announce_reader);
  assert(announced.party_size == 2 && announced.ghost_hash == ghost_hash &&
         announced.byte_count == ghost_bytes.size());
  std::cout << "  -> The announce named the party, the hash and the size!" << std::endl;

  // 2. A cold cache: the request, on the C2S reliable stream.
  Bit_Writer request_writer;
  shared::serialize_request_ghost(request_writer, {.ghost_hash = announced.ghost_hash});
  queue_reliable_client_message(client_state, static_cast<uint8>(Message_Type::C2S_RequestGhost),
                                request_writer.buffer);
  service_client_reliable_stream(client_state);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  Server_Inbox server_inbox;
  poll_network(server_state, server_socket, server_receive_drain_cap_in_datagrams, 401,
               server_inbox);
  assert(server_inbox.ghost_requests.size() == 1 && server_inbox.ghost_requests[0].first == 0);
  Bit_Reader request_reader(server_inbox.ghost_requests[0].second.data(),
                            server_inbox.ghost_requests[0].second.size());
  assert(shared::deserialize_request_ghost(request_reader).ghost_hash == ghost_hash);
  std::cout << "  -> The request reached the server's inbox, naming that hash!" << std::endl;

  // 3. The file, as a paced transfer.
  Bit_Writer data_writer;
  shared::serialize_ghost_data(
      data_writer, {.party_size = 2, .ghost_hash = ghost_hash, .bytes = ghost_bytes});
  begin_paced_transfer(server_state, 0, data_writer.buffer,
                       static_cast<uint8>(Message_Type::S2C_GhostData));
  assert(server_state.clients[0].outbound_transfer.fragments.size() > 1 &&
         "the fixture is a real multi-fragment transfer");

  client_inbox = {};
  for (int pass = 0; pass < 20 && client_inbox.ghost_data_messages.empty(); ++pass)
  {
    service_paced_transfers(server_state, server_socket, 8);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    poll_client_network(client_state, client_receive_drain_cap_in_datagrams, client_inbox);
  }
  assert(client_inbox.ghost_data_messages.size() == 1);

  Bit_Reader data_reader(client_inbox.ghost_data_messages[0].data(),
                         client_inbox.ghost_data_messages[0].size());
  const std::optional<shared::ghost_data_message_t> data =
      shared::try_deserialize_ghost_data(data_reader);
  assert(data && data->party_size == 2 && data->ghost_hash == ghost_hash);
  assert(data->bytes == ghost_bytes && "the file's bytes survived the transfer");
  assert(shared::compute_ghost_hash(Span<const uint8_t>(
             data->bytes.data(), static_cast<uint32_t>(data->bytes.size()))) == announced.ghost_hash &&
         "and hash to what was announced, which is the client's whole check");

  const std::optional<shared::ghost_t> received = shared::try_parse_ghost(
      Span<const uint8_t>(data->bytes.data(), static_cast<uint32_t>(data->bytes.size())),
      "received");
  assert(received && received->tracks.size() == 2 && received->run_ticks == 299);
  std::cout << "  -> The ghost arrived whole, hashed right and parsed as two tracks!" << std::endl;

  release_client_slot(server_state, 0);
  client_state.socket.close();
  server_socket.close();
}

// The failure the shared reassemble_fragment exists to make unrepresentable on
// BOTH sides. message_id is a uint8 incremented once per message sent, so a
// client sending one input batch per tick wraps the whole space every
// 256/sv_tickrate seconds -- 4.3s at 60Hz, against a 5s bucket timeout. The wrap
// outlives the expiry, so a new message routinely draws an id whose bucket is
// still held by an older one that lost a fragment and will never complete.
//
// The server open-coded its own reassembly for a while and resized only when the
// bucket was empty, so it wrote the new message's fragments into the stale
// message's vector and judged completion against the stale count. It now calls
// the same function the client does, and this is what that buys.
void test_wrapped_message_id_takes_over_a_stale_bucket()
{
  std::cout << "[TEST] Testing a wrapped message_id over a stale bucket..."
            << std::endl;

  std::map<uint8, Partial_Message> partial_packets;
  std::vector<uint8> reassembled;

  // A four-fragment message that loses fragment 2 and can never complete.
  uint8 stale_id = 7;
  const std::vector<uint8> abandoned(4 * MAX_PAYLOAD_SIZE_IN_BYTES, 0xAB);
  const std::vector<Packet> stale = convert_to_packets(
      abandoned, static_cast<uint8>(Message_Type::C2S_ClientInputBatch), stale_id);
  assert(stale.size() == 4);

  for (size_t index = 0; index < stale.size(); ++index)
    if (index != 2)
      assert(!reassemble_fragment(partial_packets, stale[index], reassembled) &&
             "a message missing a fragment never completes");

  // 256 sends later the counter comes back around to the same id, carrying a
  // message of a DIFFERENT length. The bucket is still there.
  assert(partial_packets.count(stale[0].header.message_id) == 1 &&
         "the stale bucket outlives the wrap -- that is the whole problem");

  uint8 wrapped_id = stale[0].header.message_id;
  std::vector<uint8> arriving(2 * MAX_PAYLOAD_SIZE_IN_BYTES);
  for (size_t index = 0; index < arriving.size(); ++index)
    arriving[index] = static_cast<uint8>(index * 7 + 3);

  const std::vector<Packet> fresh = convert_to_packets(
      arriving, static_cast<uint8>(Message_Type::C2S_ClientInputBatch), wrapped_id);
  assert(fresh.size() == 2);
  assert(fresh[0].header.message_id == stale[0].header.message_id &&
         "the two messages collide on one id, which is what the wrap does");

  assert(!reassemble_fragment(partial_packets, fresh[0], reassembled) &&
         "one of two fragments is not a whole message");
  assert(reassemble_fragment(partial_packets, fresh[1], reassembled) &&
         "the differing fragment_count takes the bucket over rather than "
         "completing against the stale one");
  assert(reassembled == arriving &&
         "and the bytes are the NEW message's, with none of the abandoned one's");

  std::cout << "  -> The colliding message took the bucket over intact!"
            << std::endl;
}

int main()
{
  test_receive_and_reassembly();
  test_reliable_stream_round_trip();
  test_reliable_stream_round_trip_c2s();
  test_lossy_transfer_converges();
  test_a_cold_client_receives_the_announced_ghost();
  test_wrapped_message_id_takes_over_a_stale_bucket();
  std::cout << "[TEST] All tests passed." << std::endl;
  return 0;
}
