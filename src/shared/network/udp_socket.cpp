#include "udp_socket.hpp"
#include <arpa/inet.h>
#include <cstdio>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace network
{

// Address Implementation

Address::Address(uint8 a, uint8 b, uint8 c, uint8 d, uint16 p)
{
  ip_v4 = (a << 24) | (b << 16) | (c << 8) | d;
  port = p;
}

bool Address::parse(const std::string &str, Address &out_addr)
{
  // Simple parsing, assuming IP:Port format or just IP
  // But standard `inet_addr` or `inet_pton` is better.
  // Let's assume standard "d.d.d.d" for now. Port is separate in our
  // constructor usually. wait, argument is just string.

  // Split string?
  // Doing strict "x.x.x.x" for now, port needs to be set separately if we just
  // use inet_addr But let's assume this helper is mainly for IP.

  // Using inet_pton
  struct sockaddr_in sa;
  if (inet_pton(AF_INET, str.c_str(), &(sa.sin_addr)) == 1)
  {
    // Network byte order to Host
    out_addr.ip_v4 = ntohl(sa.sin_addr.s_addr);
    out_addr.port = 0; // Port not parsed
    return true;
  }
  return false;
}

std::string Address::to_string() const
{
  char buffer[128];
  uint8 a = (ip_v4 >> 24) & 0xFF;
  uint8 b = (ip_v4 >> 16) & 0xFF;
  uint8 c = (ip_v4 >> 8) & 0xFF;
  uint8 d = ip_v4 & 0xFF;
  std::snprintf(buffer, sizeof(buffer), "%d.%d.%d.%d:%d", a, b, c, d, port);
  return std::string(buffer);
}

// Udp_Socket Implementation

Udp_Socket::Udp_Socket() : m_socket_handle(-1) {}

Udp_Socket::~Udp_Socket() { close(); }

bool Udp_Socket::open(uint16 port)
{
  close(); // Close if existing

  m_socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (m_socket_handle <= 0)
  {
    m_socket_handle = -1;
    return false;
  }

  // Bind to port
  struct sockaddr_in address;
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port);

  if (bind(m_socket_handle, (const struct sockaddr *)&address,
           sizeof(struct sockaddr_in)) < 0)
  {
    close();
    return false;
  }

  // Set non-blocking
  int flags = fcntl(m_socket_handle, F_GETFL, 0);
  if (flags == -1)
    flags = 0;
  fcntl(m_socket_handle, F_SETFL, flags | O_NONBLOCK);

  return true;
}

void Udp_Socket::close()
{
  if (m_socket_handle != -1)
  {
    ::close(m_socket_handle);
    m_socket_handle = -1;
  }
}

bool Udp_Socket::is_open() const { return m_socket_handle != -1; }

bool Udp_Socket::send(const Packet &packet, const Address &address)
{
  if (m_socket_handle == -1)
    return false;

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(address.ip_v4);
  addr.sin_port = htons(address.port);

  // Calculate total size to send
  // Header + Padding + Payload
  // However, packet.header.payload_size indicates actual payload length.
  // Padding is just explicit bytes in struct?
  // User struct definition:
  // Packet_Header header;
  // int padding_for_alignment;
  // uint8 buffer[MAX_BUFFER_SIZE_IN_BYTES];
  //
  // The struct Packet is a fixed size container in memory.
  // BUT over the wire, we should probably only send what is needed?
  // OR do we send the whole struct for simplicity?
  //
  // "constexpr size_t MAX_BUFFER_SIZE_IN_BYTES = 1452 - sizeof(Packet_Header);"
  // This implies the packet is MTU sized.
  // If we send the WHOLE Packet struct, it's ~1452 bytes.
  // That's fine for MTU.
  //
  // However, if payload_size is small, we waste bandwidth sendsing full MTU.
  // Usually we send: Header + Payload.
  // BUT, the user struct has `padding_for_alignment` right in the middle.
  // If receiver just casts `(Packet*)buffer`, they expect that padding.
  //
  // Let's send the WHOLE struct up to payload_size?
  // Offset of buffer is: sizeof(Packet_Header) + sizeof(int).
  // Send Size = Offset + packet.header.payload_size.

  size_t header_plus_padding = sizeof(Packet_Header) + sizeof(int);
  size_t send_size = header_plus_padding + packet.header.payload_size;

  // Safety check
  if (send_size > sizeof(Packet))
    send_size = sizeof(Packet);

  int sent_bytes = sendto(m_socket_handle, (const char *)&packet, send_size, 0,
                          (struct sockaddr *)&addr, sizeof(struct sockaddr_in));

  return sent_bytes == static_cast<int>(send_size);
}

bool Udp_Socket::receive(Packet &packet, Address &sender)
{
  if (m_socket_handle == -1)
    return false;

  struct sockaddr_in from;
  socklen_t from_length = sizeof(from);

  // We can receive into the packet struct directly
  // But we need to use recvfrom return value to set payload_size?
  // Actually header usually comes with it.

  int bytes_received =
      recvfrom(m_socket_handle, (char *)&packet, sizeof(Packet), 0,
               (struct sockaddr *)&from, &from_length);

  if (bytes_received <= 0)
  {
    // 0 or -1 (error/wouldblock)
    return false;
  }

  // Set Sender
  sender.ip_v4 = ntohl(from.sin_addr.s_addr);
  sender.port = ntohs(from.sin_port);

  // Verify size?
  // Ideally we check if bytes_received >= sizeof(Packet_Header)
  if (static_cast<size_t>(bytes_received) < sizeof(Packet_Header))
    return false; // Trash packet

  return true;
}

} // namespace network
