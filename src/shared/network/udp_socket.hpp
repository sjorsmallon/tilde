#pragma once

#include "packet.hpp"
#include <string>

namespace network
{

struct Address
{
  uint32 ip_v4; // Host byte order usually? Or Network? Let's assume Host for
                // convenience wrappers, convert internall.
  uint16 port;  // Host byte

  Address() : ip_v4(0), port(0) {}
  Address(uint32 ip, uint16 p) : ip_v4(ip), port(p) {}
  Address(uint8 a, uint8 b, uint8 c, uint8 d, uint16 p);

  // Parses "127.0.0.1" string, returns true if success
  static bool parse(const std::string &str, Address &out_addr);

  std::string to_string() const;

  bool operator==(const Address &other) const
  {
    return ip_v4 == other.ip_v4 && port == other.port;
  }
  bool operator!=(const Address &other) const { return !(*this == other); }
};

class Udp_Socket
{
public:
  Udp_Socket();
  ~Udp_Socket();

  // Open a socket on a specific port.
  // If port is 0, the OS will choose a free port.
  bool open(uint16 port);

  void close();

  bool is_open() const;

  // Send a packet to a destination
  bool send(const Packet &packet, const Address &address);

  // Receive a packet. Returns true if a packet was read.
  // Non-blocking if the socket is non-blocking (we will set it to non-blocking
  // by default).
  bool receive(Packet &packet, Address &sender);

private:
  int m_socket_handle;
};

} // namespace network
