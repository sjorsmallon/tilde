#include "../shared/replay/ghost.hpp"
#include "../shared/message_stream.hpp"
#include "../shared/network/bitstream.hpp"
#include "../shared/network/client_transport_layer.hpp"
#include "../shared/network/generated/messages_generated.hpp"
#include "../shared/network/ghost_transfer.hpp"
#include "../shared/network/packet.hpp"
#include "../shared/network/server_transport_layer.hpp"
#include "../shared/network/udp_socket.hpp"
#include <cassert>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

using namespace network;

// What a test collects off each side's poll, the shape the inboxes used to have:
// the transport delivers through a callback, and a test keeps the bytes.
struct received_payload_t
{
  int32_t            slot;
  Address            sender;
  std::vector<uint8> payload;
};

struct server_received_t
{
  std::vector<received_payload_t> message_payloads;
};

struct client_received_t
{
  std::vector<std::vector<uint8>> message_payloads;
  std::vector<std::vector<uint8>> map_transfers;
  std::vector<std::vector<uint8>> ghost_transfers;
};

static void poll_server(Server_Transport_Layer &state, Udp_Socket &socket, uint32_t tick,
                        server_received_t &received)
{
  poll_network(state, socket, server_receive_drain_cap_in_datagrams, tick,
               [&received](int32_t slot, const Address &sender, Span<const uint8> payload) {
                 received.message_payloads.push_back(
                     {slot, sender, std::vector<uint8>(payload.data, payload.data + payload.count)});
               });
}

static void poll_client(Client_Transport_Layer &state, client_received_t &received)
{
  poll_client_network(state, client_receive_drain_cap_in_datagrams,
                      [&received](Payload_Kind kind, Span<const uint8> payload) {
                        std::vector<uint8> bytes(payload.data, payload.data + payload.count);
                        switch (kind)
                        {
                        case Payload_Kind::Message:
                          received.message_payloads.push_back(std::move(bytes));
                          return;
                        case Payload_Kind::Map_Transfer:
                          received.map_transfers.push_back(std::move(bytes));
                          return;
                        case Payload_Kind::Ghost_Transfer:
                          received.ghost_transfers.push_back(std::move(bytes));
                          return;
                        case Payload_Kind::Reliable:
                        case Payload_Kind::Transfer_Receipt:
                        case Payload_Kind::Count:
                          break;
                        }
                        assert(false && "the client poll delivers no transport parcel");
                      });
}

void test_receive_and_reassembly()
{
  std::cout << "[TEST] Testing Receive and Reassembly..." << std::endl;

  Server_Transport_Layer state;
  Udp_Socket server_socket;
  Udp_Socket client_socket;

  // Open sockets
  if (!server_socket.try_open({.port_id = 9001}))
  {
    std::cerr << "Failed to open server socket on 9001" << std::endl;
    exit(1);
  }
  if (!client_socket.try_open({.port_id = 0}))
  {
    std::cerr << "Failed to open client socket" << std::endl;
    exit(1);
  }

  // Register "client" in state manually so receive_messages accepts packets
  Address client_addr(127, 0, 0, 1, 9002);
  // Actually Udp_Socket wraps a socket handle. If we bind 0, we need to ask OS.
  // For simplicity, bind client to fixed port 9002.
  client_socket.close();
  if (!client_socket.try_open({.port_id = 9002}))
  {
    std::cerr << "Failed to open client socket on 9002" << std::endl;
    exit(1);
  }
  client_addr = Address(127, 0, 0, 1, 9002);

  occupy_client_slot(state, 0, client_addr, 100);

  // Two Client_Input rows in one stream, the way the client sends its unacked
  // tail: one Message datagram whose payload the dispatch walks row by row.
  shared::message_stream_t stream;
  for (int input_number : {9, 10})
  {
    shared::Client_Input input;
    input.input_number           = input_number;
    input.input.buttons_at_start = 0xABCDull; // a payload marker, to prove the
                                              // bytes survive the round trip
    input.input.view_at_end      = {45.0f, 0.0f};
    shared::write_client_input(stream, input);
  }
  stream.finish();

  uint8 next_message_id = 0;
  auto packets = convert_to_packets(stream.writer.buffer, Payload_Kind::Message,
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
  server_received_t inbox;
  // The 50ms sleep above is what makes this deterministic: the datagrams are
  // already queued, so the drain finds them and stops. The cap is the livelock
  // guard, not a window to wait in.
  poll_server(state, server_socket, 140, inbox);

  // Verify
  if (inbox.message_payloads.empty())
  {
    std::cerr << "Failed to receive/reassemble the input datagram!" << std::endl;
    std::cerr << "Partial packets count: " << state.clients[0].partial_packets.size()
              << std::endl;
    assert(false);
  }

  // One payload, filed under the sender's slot, carrying both rows in the order
  // they were written: the transport reassembles, it does not look inside.
  assert(inbox.message_payloads.size() == 1 && inbox.message_payloads[0].slot == 0);
  const std::vector<uint8> &payload = inbox.message_payloads[0].payload;
  assert(payload == stream.writer.buffer && "the bytes survive the round trip");

  Bit_Reader reader(payload.data(), payload.size());
  assert(reader.read_bits(shared::MESSAGE_STREAM_COUNT_BITS) == 2);
  for (int input_number : {9, 10})
  {
    assert(reader.read_bits(16) == (uint32_t)shared::Message_Type::Client_Input);
    const std::optional<shared::Client_Input> input = shared::try_read_client_input(reader);
    assert(input && input->input_number == input_number);
    assert(input->input.buttons_at_start == 0xABCDull && input->input.view_at_end.yaw == 45.0f);
  }
  std::cout << "  -> Input datagram reassembled and both rows read back!" << std::endl;

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

  if (!server_socket.try_open({.port_id = 9003}))
  {
    std::cerr << "Failed to open server socket on 9003" << std::endl;
    exit(1);
  }
  if (!client_state.socket.try_open({.port_id = 9004}))
  {
    std::cerr << "Failed to open client socket on 9004" << std::endl;
    exit(1);
  }

  const Address server_address(127, 0, 0, 1, 9003);
  const Address client_address(127, 0, 0, 1, 9004);
  client_state.server_address = server_address;
  occupy_client_slot(server_state, 0, client_address, 100);

  // A Server_Text on the stream the way the server queues one: a Message
  // record holding the tick, then the finished reliable scope.
  shared::reliable_stream_t reliable;
  shared::Server_Text       text;
  (void)text.text.set("the stream carries this");
  shared::write_server_text(reliable, text);
  reliable.records.finish();

  std::vector<uint8> payload;
  shared::begin_server_message_payload(payload, 123);
  shared::append_message_stream(payload, reliable.records);
  queue_reliable_message(server_state.clients[0].reliable_stream,
                         static_cast<uint8>(Payload_Kind::Message), payload);

  send_reliable_block(server_state, server_socket, 0);
  assert(server_state.clients[0].reliable_stream.block_number == 1);
  assert(server_state.clients[0].reliable_stream.block_length != 0);

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  client_received_t inbox;
  poll_client(client_state, inbox);

  assert(inbox.message_payloads.size() == 1 &&
         "the record came out of the block and through the same deliver");
  {
    Bit_Reader reader(inbox.message_payloads[0].data(), inbox.message_payloads[0].size());
    assert(reader.read_bits(32) == 123);
    assert(reader.read_bits(shared::MESSAGE_STREAM_COUNT_BITS) == 1);
    assert(reader.read_bits(16) == (uint32_t)shared::Message_Type::Server_Text);
    const std::optional<shared::Server_Text> read_back = shared::try_read_server_text(reader);
    assert(read_back && std::string(read_back->text.c_str()) == "the stream carries this");
  }
  assert(client_state.reliable_stream.received_through == 1);
  std::cout << "  -> Block delivered and framed back into a message!" << std::endl;

  // The ack is not a message: it rides the header of whatever the client sends
  // next, which in the live client is its per-tick input. Any datagram does.
  shared::message_stream_t any_outbound;
  shared::Connect          connect;
  (void)connect.player_name.set("acker");
  shared::write_connect(any_outbound, connect);
  any_outbound.finish();
  send_message_stream_to_server(client_state, any_outbound);

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  server_received_t server_inbox;
  poll_server(server_state, server_socket, 141, server_inbox);

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

  if (!server_socket.try_open({.port_id = 9007}) || !client_state.socket.try_open({.port_id = 9008}))
  {
    std::cerr << "Failed to open the socket pair" << std::endl;
    exit(1);
  }

  const Address server_address(127, 0, 0, 1, 9007);
  const Address client_address(127, 0, 0, 1, 9008);
  client_state.server_address = server_address;
  occupy_client_slot(server_state, 0, client_address, 300);

  // The two rows, written into one frame's reliable scope and queued as one
  // record -- which is also what makes "rely on order, never on grouping"
  // checkable: they arrive in the order they were written because neither the
  // scope nor the stream can reorder them.
  shared::reliable_stream_t reliable;
  shared::Console_Line      command;
  (void)command.line.set("changelevel new_map");
  shared::write_console_line(reliable, command);

  shared::Map_Data_Request request;
  request.map_generation = 3;
  (void)request.map_name.set("new_map");
  shared::write_map_data_request(reliable, request);
  reliable.records.finish();
  queue_reliable_client_message(client_state, reliable.records.writer.buffer);

  service_client_reliable_stream(client_state);
  assert(client_state.reliable_stream.block_number == 1);
  assert(client_state.reliable_stream.block_length != 0);

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  server_received_t inbox;
  poll_server(server_state, server_socket, 301, inbox);

  assert(inbox.message_payloads.size() == 1 &&
         "the record came out of the block and through the deliver the unreliable path uses");
  assert(inbox.message_payloads[0].slot == 0);
  {
    const std::vector<uint8> &received = inbox.message_payloads[0].payload;
    Bit_Reader                reader(received.data(), received.size());
    assert(reader.read_bits(shared::MESSAGE_STREAM_COUNT_BITS) == 2);
    assert(reader.read_bits(16) == (uint32_t)shared::Message_Type::Console_Line);
    const std::optional<shared::Console_Line> line = shared::try_read_console_line(reader);
    assert(line && std::string(line->line.c_str()) == "changelevel new_map");
    assert(reader.read_bits(16) == (uint32_t)shared::Message_Type::Map_Data_Request);
    const std::optional<shared::Map_Data_Request> read_back = shared::try_read_map_data_request(reader);
    assert(read_back && read_back->map_generation == 3 && std::string(read_back->map_name.c_str()) == "new_map");
  }
  assert(server_state.clients[0].reliable_stream.received_through == 1);
  std::cout << "  -> Both records delivered, in the order they were queued!"
            << std::endl;

  // Nothing has freed the block yet: the ack lives in the header of whatever the
  // server sends next, and it has not sent anything.
  assert(client_state.reliable_stream.block_length != 0);

  // An ORDINARY S2C datagram -- not a reliable one -- sent the way the server
  // sends every snapshot. This is the property the Loading client depends on,
  // since the only S2C traffic it gets is the map package's fragments.
  {
    shared::message_stream_t stream;
    shared::write_entity_package(stream, shared::Entity_Package{});
    stream.finish();
    std::vector<uint8> payload;
    shared::begin_server_message_payload(payload, 77);
    shared::append_message_stream(payload, stream);
    for (const Packet &packet : convert_to_packets(payload, Payload_Kind::Message,
                                                   server_state.next_message_id))
      send_packet_to_client(server_state, server_socket, 0, packet);
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  client_received_t client_inbox;
  poll_client(client_state, client_inbox);

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
//
// Run at two sizes, because the bitmap's size is the transfer's: at 20 fragments
// the report is one datagram, at 10000 it is two, and the server must take both.
void test_lossy_transfer_converges(uint16_t server_port, uint16_t client_port,
                                   size_t fragment_count, size_t first_lost, size_t second_lost)
{
  std::cout << "[TEST] Testing transfer recovery over UDP, " << fragment_count
            << " fragments..." << std::endl;

  Server_Transport_Layer server_state;
  Udp_Socket server_socket;
  Client_Transport_Layer client_state;

  if (!server_socket.try_open({.port_id = server_port}) || !client_state.socket.try_open({.port_id = client_port}))
  {
    std::cerr << "Failed to open the socket pair" << std::endl;
    exit(1);
  }

  const Address server_address(127, 0, 0, 1, server_port);
  const Address client_address(127, 0, 0, 1, client_port);
  client_state.server_address = server_address;
  occupy_client_slot(server_state, 0, client_address, 200);

  std::vector<uint8> payload(fragment_count * MAX_PAYLOAD_SIZE_IN_BYTES);
  for (size_t index = 0; index < payload.size(); ++index)
    payload.data()[index] = static_cast<uint8>(index * 13 + 5);

  begin_paced_transfer(server_state, 0, payload,
                       Payload_Kind::Map_Transfer);
  const std::vector<Packet> fragments = server_state.clients[0].outbound_transfer.fragments;
  assert(fragments.size() == fragment_count);

  std::vector<uint8> reassembled;
  for (size_t index = 0; index < fragments.size(); ++index)
  {
    if (index == first_lost || index == second_lost)
      continue;
    assert(!reassemble_fragment(client_state.partial_packets, fragments[index], reassembled) &&
           "the message cannot complete while two fragments are missing");
  }

  report_transfer_progress(client_state);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  server_received_t server_inbox;
  poll_server(server_state, server_socket, 201, server_inbox);

  size_t confirmed_count = 0;
  for (bool confirmed : server_state.clients[0].outbound_transfer.confirmed)
    if (confirmed)
      ++confirmed_count;
  assert(confirmed_count == fragment_count - 2 && "the bitmap named exactly what arrived");
  assert(!server_state.clients[0].outbound_transfer.confirmed[first_lost]);
  assert(!server_state.clients[0].outbound_transfer.confirmed[second_lost]);
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
    assert(fragment.header.fragment_index == first_lost ||
           fragment.header.fragment_index == second_lost);

  // Feeding them in completes the message.
  assert(!reassemble_fragment(client_state.partial_packets, repaired[0], reassembled));
  assert(reassemble_fragment(client_state.partial_packets, repaired[1], reassembled) &&
         "the last missing fragment completes the message");
  assert(reassembled == payload && "and the bytes survived the repair");
  std::cout << "  -> Repair was proportional to the loss and rebuilt the payload!"
            << std::endl;

  // A duplicate arriving after completion must not reopen the bucket -- that is
  // what would report "1 of N" and re-stream a map we already hold.
  assert(!reassemble_fragment(client_state.partial_packets, fragments[0], reassembled) &&
         "a duplicate after completion is discarded, not re-delivered");

  // And the completing report frees the transfer, so the sender stops waiting.
  std::this_thread::sleep_for(std::chrono::milliseconds(
      static_cast<int>(transfer_receipt_interval_in_seconds * 1000) + 20));
  report_transfer_progress(client_state);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  poll_server(server_state, server_socket, 202, server_inbox);
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

  if (!server_socket.try_open({.port_id = 9011}) || !client_state.socket.try_open({.port_id = 9012}))
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

  // 1. The announce, a Ghost_Available on the S2C reliable stream.
  shared::reliable_stream_t announce;
  shared::Ghost_Available   available;
  available.party_size = 2;
  available.ghost_hash = ghost_hash;
  available.byte_count = static_cast<uint32_t>(ghost_bytes.size());
  shared::write_ghost_available(announce, available);
  announce.records.finish();
  std::vector<uint8> announce_payload;
  shared::begin_server_message_payload(announce_payload, 400);
  shared::append_message_stream(announce_payload, announce.records);
  queue_reliable_message(server_state.clients[0].reliable_stream,
                         static_cast<uint8>(Payload_Kind::Message), announce_payload);
  send_reliable_block(server_state, server_socket, 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  client_received_t client_inbox;
  poll_client(client_state, client_inbox);
  assert(client_inbox.message_payloads.size() == 1);
  Bit_Reader announce_reader(client_inbox.message_payloads[0].data(),
                             client_inbox.message_payloads[0].size());
  assert(announce_reader.read_bits(32) == 400);
  assert(announce_reader.read_bits(shared::MESSAGE_STREAM_COUNT_BITS) == 1);
  assert(announce_reader.read_bits(16) == (uint32_t)shared::Message_Type::Ghost_Available);
  const std::optional<shared::Ghost_Available> announced = shared::try_read_ghost_available(announce_reader);
  assert(announced && announced->party_size == 2 && announced->ghost_hash == ghost_hash &&
         announced->byte_count == ghost_bytes.size());
  std::cout << "  -> The announce named the party, the hash and the size!" << std::endl;

  // 2. A cold cache: a Ghost_Request on the C2S reliable stream.
  shared::reliable_stream_t request;
  shared::Ghost_Request     ghost_request;
  ghost_request.ghost_hash = announced->ghost_hash;
  shared::write_ghost_request(request, ghost_request);
  request.records.finish();
  queue_reliable_client_message(client_state, request.records.writer.buffer);
  service_client_reliable_stream(client_state);
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  server_received_t server_inbox;
  poll_server(server_state, server_socket, 401, server_inbox);
  assert(server_inbox.message_payloads.size() == 1 && server_inbox.message_payloads[0].slot == 0);
  Bit_Reader request_reader(server_inbox.message_payloads[0].payload.data(),
                            server_inbox.message_payloads[0].payload.size());
  assert(request_reader.read_bits(shared::MESSAGE_STREAM_COUNT_BITS) == 1);
  assert(request_reader.read_bits(16) == (uint32_t)shared::Message_Type::Ghost_Request);
  const std::optional<shared::Ghost_Request> requested = shared::try_read_ghost_request(request_reader);
  assert(requested && requested->ghost_hash == ghost_hash);
  std::cout << "  -> The request reached the server, naming that hash!" << std::endl;

  // 3. The file, as a paced transfer.
  Bit_Writer data_writer;
  shared::serialize_ghost_data(
      data_writer, {.party_size = 2, .ghost_hash = ghost_hash, .bytes = ghost_bytes});
  begin_paced_transfer(server_state, 0, data_writer.buffer,
                       Payload_Kind::Ghost_Transfer);
  assert(server_state.clients[0].outbound_transfer.fragments.size() > 1 &&
         "the fixture is a real multi-fragment transfer");

  client_inbox = {};
  for (int pass = 0; pass < 20 && client_inbox.ghost_transfers.empty(); ++pass)
  {
    service_paced_transfers(server_state, server_socket, 8);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    poll_client(client_state, client_inbox);
  }
  assert(client_inbox.ghost_transfers.size() == 1);

  Bit_Reader data_reader(client_inbox.ghost_transfers[0].data(),
                         client_inbox.ghost_transfers[0].size());
  const std::optional<shared::ghost_data_message_t> data =
      shared::try_deserialize_ghost_data(data_reader);
  assert(data && data->party_size == 2 && data->ghost_hash == ghost_hash);
  assert(data->bytes == ghost_bytes && "the file's bytes survived the transfer");
  assert(shared::compute_ghost_hash(Span<const uint8_t>(
             data->bytes.data(), static_cast<uint32_t>(data->bytes.size()))) == announced->ghost_hash &&
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
      abandoned, Payload_Kind::Message, stale_id);
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
      arriving, Payload_Kind::Message, wrapped_id);
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

// A Message is one payload kind for every row of messages.def, by either route:
// a datagram's payload and a reliable record's payload both reach the same
// deliver, in arrival order, for the generated dispatch to walk.
void test_message_payloads_arrive_by_both_routes()
{
  std::cout << "[TEST] Testing Message payloads by datagram and by block..." << std::endl;

  Server_Transport_Layer server_state;
  Udp_Socket             server_socket;
  Client_Transport_Layer client_state;

  if (!server_socket.try_open({.port_id = 9015}))
  {
    std::cerr << "Failed to open server socket on 9015" << std::endl;
    exit(1);
  }
  if (!client_state.socket.try_open({.port_id = 9016}))
  {
    std::cerr << "Failed to open client socket on 9016" << std::endl;
    exit(1);
  }
  client_state.server_address = Address(127, 0, 0, 1, 9015);
  occupy_client_slot(server_state, 0, Address(127, 0, 0, 1, 9016), 100);

  // The server tick, then one stream holding one footstep.
  shared::message_stream_t stream;
  shared::Footstep         step;
  step.site.attached_entity = 42;
  shared::write_footstep(stream, step);
  stream.finish();

  std::vector<uint8> payload;
  Bit_Writer         header;
  header.write_bits(777, 32);
  payload.assign(header.buffer.begin(), header.buffer.end());
  shared::append_message_stream(payload, stream);

  for (const Packet &packet :
       convert_to_packets(payload, Payload_Kind::Message, server_state.next_message_id))
    send_packet_to_client(server_state, server_socket, 0, packet);

  queue_reliable_message(server_state.clients[0].reliable_stream, static_cast<uint8>(Payload_Kind::Message), payload);
  send_reliable_block(server_state, server_socket, 0);

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  client_received_t inbox;
  poll_client(client_state, inbox);

  assert(inbox.message_payloads.size() == 2 && "one by datagram, one out of the block");
  for (const std::vector<uint8> &received : inbox.message_payloads)
  {
    assert(received == payload && "the bytes are the payload, whichever route");
    Bit_Reader reader(received.data(), received.size());
    assert(reader.read_bits(32) == 777);
    assert(reader.read_bits(shared::MESSAGE_STREAM_COUNT_BITS) == 1);
    assert(reader.read_bits(16) == (uint32_t)shared::Message_Type::Footstep);
    const std::optional<shared::Footstep> read_back = shared::try_read_footstep(reader);
    assert(read_back && read_back->site.attached_entity == 42);
    reader.align();
    assert(reader.bits_remaining() == 0);
  }
  std::cout << "  -> Both routes delivered the same Message payload!" << std::endl;

  release_client_slot(server_state, 0);
  client_state.socket.close();
  server_socket.close();
}

// A peer with no slot may say one thing, a Connect: its Message is delivered
// with no slot and its address, for on_connect to answer. Anything else from a
// stranger, and anything that needs reassembly, is dropped at the door.
void test_stranger_connect_is_filed_without_a_slot()
{
  std::cout << "[TEST] Testing a stranger's Connect..." << std::endl;

  Server_Transport_Layer server_state;
  Udp_Socket             server_socket;
  Client_Transport_Layer client_state;

  if (!server_socket.try_open({.port_id = 9017}) || !client_state.socket.try_open({.port_id = 9018}))
  {
    std::cerr << "Failed to open the socket pair" << std::endl;
    exit(1);
  }
  client_state.server_address = Address(127, 0, 0, 1, 9017);
  const Address client_address(127, 0, 0, 1, 9018);

  shared::message_stream_t stream;
  shared::Connect          connect;
  (void)connect.player_name.set("stranger");
  connect.schema_hash = 0x1234;
  shared::write_connect(stream, connect);
  stream.finish();
  send_message_stream_to_server(client_state, stream);

  // A transfer receipt from a stranger names a transfer it cannot have.
  send_raw_message(client_state, Payload_Kind::Transfer_Receipt, {1, 2, 3});

  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  server_received_t inbox;
  poll_server(server_state, server_socket, 500, inbox);

  assert(inbox.message_payloads.size() == 1 && "the Connect was delivered, the receipt was not");
  assert(inbox.message_payloads[0].slot == -1);
  assert(inbox.message_payloads[0].sender == client_address);
  {
    const std::vector<uint8> &payload = inbox.message_payloads[0].payload;
    Bit_Reader                reader(payload.data(), payload.size());
    assert(reader.read_bits(shared::MESSAGE_STREAM_COUNT_BITS) == 1);
    assert(reader.read_bits(16) == (uint32_t)shared::Message_Type::Connect);
    const std::optional<shared::Connect> read_back = shared::try_read_connect(reader);
    assert(read_back && read_back->schema_hash == 0x1234 && std::string(read_back->player_name.c_str()) == "stranger");
  }
  assert(!try_find_client_slot(server_state, client_address) && "delivering a Connect occupies nothing");
  std::cout << "  -> The stranger's Connect was delivered with no slot and its address!" << std::endl;

  client_state.socket.close();
  server_socket.close();
}

int main()
{
  test_receive_and_reassembly();
  test_message_payloads_arrive_by_both_routes();
  test_stranger_connect_is_filed_without_a_slot();
  test_reliable_stream_round_trip();
  test_reliable_stream_round_trip_c2s();
  test_lossy_transfer_converges(9005, 9006, 20, 4, 17);
  static_assert(3 + receipt_bitmap_size_in_bytes(10000) > MAX_PAYLOAD_SIZE_IN_BYTES,
                "the large case must need a receipt of more than one datagram");
  test_lossy_transfer_converges(9013, 9014, 10000, 4, 9990);
  test_a_cold_client_receives_the_announced_ghost();
  test_wrapped_message_id_takes_over_a_stale_bucket();
  std::cout << "[TEST] All tests passed." << std::endl;
  return 0;
}
