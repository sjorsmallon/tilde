#include "../shared/network/packet.hpp"
#include "../shared/network/udp_socket.hpp"
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

using namespace network;

int main()
{
  std::cout << "[TEST] Starting UDP Socket Test..." << std::endl;

  // 1. Test Address Helper
  {
    Address addr;
    bool res = Address::parse("127.0.0.1", addr);
    assert(res && "Failed to parse 127.0.0.1");
    assert(addr.ip_v4 == 0x7F000001 && "Parsed IP incorrect (Host Order)");

    addr.port = 8080;
    std::cout << "  -> Parsed Address: " << addr.to_string() << std::endl;
    assert(addr.to_string() == "127.0.0.1:8080");
  }

  // 2. Test Packet Chunking
  {
    std::cout << "  -> Testing packet chunking..." << std::endl;
    std::vector<uint8> big_data;
    big_data.resize(3000); // Should be 3 packets (max payload ~1400)
    for (size_t i = 0; i < big_data.size(); ++i)
      big_data[i] = (uint8)(i % 255);

    auto packets = convert_to_packets(big_data, 10);

    // Expected packets: ceil(3000 / MAX_PAYLOAD_SIZE)
    // MAX_PAYLOAD_SIZE = 1452 - sizeof(Packet_Header) - 4
    // Header = 8+1+1+1+1+2 = 14 bytes. Padding = 4. Total overhead = 18.
    // Payload = 1434.
    // 3000 / 1434 = 2.09 -> 3 packets.

    assert(packets.size() == 3);
    assert(packets[0].header.sequence_idx == 0);
    assert(packets[0].header.sequence_count == 3);
    assert(packets[0].header.message_type == 10);
    assert(packets[0].header.payload_size == MAX_PAYLOAD_SIZE_IN_BYTES);

    assert(packets[2].header.sequence_idx == 2);
    assert(packets[2].header.payload_size ==
           (3000 - MAX_PAYLOAD_SIZE_IN_BYTES * 2));

    std::cout << "  -> Chunking Success! Created " << packets.size()
              << " packets." << std::endl;
  }

  // 3. Test Loopback Communication
  {
    std::cout << "  -> Testing Loopback Communication..." << std::endl;

    Udp_Socket receiver;
    Udp_Socket sender;

    // Open Receiver on port 9000
    if (!receiver.open(9000))
    {
      std::cerr << "Failed to open receiver socket on port 9000" << std::endl;
      return 1;
    }

    // Open Sender on any port
    if (!sender.open(0))
    {
      std::cerr << "Failed to open sender socket" << std::endl;
      return 1;
    }

    // Create a packet
    Packet p = {};
    p.header.message_type = 42;
    p.header.payload_size = 5;
    std::memcpy(p.buffer, "HELLO", 5);

    // Send to 127.0.0.1:9000
    Address dest(127, 0, 0, 1, 9000);

    // Send
    bool sent = sender.send(p, dest);
    assert(sent && "Failed to send packet");

    // Wait a bit for OS to deliver
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // Receive
    Packet received_p = {};
    Address sender_addr;
    bool got_packet = receiver.receive(received_p, sender_addr);

    if (got_packet)
    {
      std::cout << "  -> Received Packet from " << sender_addr.to_string()
                << std::endl;
      assert(received_p.header.message_type == 42);
      assert(received_p.header.payload_size == 5);
      assert(std::memcmp(received_p.buffer, "HELLO", 5) == 0);

      // Note: sender_addr port will be random ephemeral port
      assert(sender_addr.ip_v4 == 0x7F000001);
    }
    else
    {
      std::cerr << "Failed to receive packet!" << std::endl;
      assert(false);
    }

    // Two-way
    // Reply
    Packet reply = {};
    std::memcpy(reply.buffer, "WORLD", 5);
    reply.header.payload_size = 5;
    receiver.send(reply, sender_addr);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    got_packet = sender.receive(received_p, sender_addr);
    assert(got_packet);
    assert(sender_addr.port == 9000);
    assert(std::memcmp(received_p.buffer, "WORLD", 5) == 0);

    std::cout << "  -> Loopback Success!" << std::endl;
  }

  std::cout << "[TEST] All Tests Passed." << std::endl;
  return 0;
}
