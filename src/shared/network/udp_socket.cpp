#include "udp_socket.hpp"
#include <cstdio>
#include <string>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    // Link with Ws2_32.lib
    #pragma comment(lib, "Ws2_32.lib")
    
    // Alias for compatibility
    using socklen_t = int;
#else
    #include <arpa/inet.h>
    #include <fcntl.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <unistd.h>
    
    // Windows constants aliases for POSIX
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
#endif

namespace network
{

// --- Address Implementation ---

Address::Address(uint8 a, uint8 b, uint8 c, uint8 d, uint16 p)
{
    ip_v4 = (a << 24) | (b << 16) | (c << 8) | d;
    port = p;
}

bool Address::parse(const std::string &str, Address &out_addr)
{
    struct sockaddr_in sa;
    // inet_pton works on both Windows (Vista+) and Linux
    if (inet_pton(AF_INET, str.c_str(), &(sa.sin_addr)) == 1)
    {
        out_addr.ip_v4 = ntohl(sa.sin_addr.s_addr);
        out_addr.port = 0; 
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

// --- Udp_Socket Implementation ---

Udp_Socket::Udp_Socket() : m_socket_handle(INVALID_SOCKET) {}

Udp_Socket::~Udp_Socket() { close(); }

bool Udp_Socket::open(uint16 port)
{
    close(); 

    m_socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket_handle == INVALID_SOCKET)
    {
        return false;
    }

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(m_socket_handle, (const struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR)
    {
        close();
        return false;
    }

    // --- Set Non-Blocking ---
#ifdef _WIN32
    u_long mode = 1; // 1 to enable non-blocking
    if (ioctlsocket(m_socket_handle, FIONBIO, &mode) != 0)
    {
        close();
        return false;
    }
#else
    int flags = fcntl(m_socket_handle, F_GETFL, 0);
    if (flags == -1) flags = 0;
    if (fcntl(m_socket_handle, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        close();
        return false;
    }
#endif

    return true;
}

void Udp_Socket::close()
{
    if (m_socket_handle != INVALID_SOCKET)
    {
#ifdef _WIN32
        closesocket(m_socket_handle);
#else
        ::close(m_socket_handle);
#endif
        m_socket_handle = INVALID_SOCKET;
    }
}

bool Udp_Socket::is_open() const { return m_socket_handle != INVALID_SOCKET; }

bool Udp_Socket::send(const Packet &packet, const Address &address)
{
    if (m_socket_handle == INVALID_SOCKET) return false;

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(address.ip_v4);
    addr.sin_port = htons(address.port);

    size_t header_plus_padding = sizeof(Packet_Header) + sizeof(int);
    size_t send_size = header_plus_padding + packet.header.payload_size;

    if (send_size > sizeof(Packet)) send_size = sizeof(Packet);

    int sent_bytes = sendto(m_socket_handle, (const char *)&packet, static_cast<int>(send_size), 0,
                            (struct sockaddr *)&addr, sizeof(addr));

    return sent_bytes == static_cast<int>(send_size);
}

bool Udp_Socket::receive(Packet &packet, Address &sender)
{
    if (m_socket_handle == INVALID_SOCKET) return false;

    struct sockaddr_in from;
    socklen_t from_length = sizeof(from);

    int bytes_received = recvfrom(m_socket_handle, (char *)&packet, sizeof(Packet), 0,
                                  (struct sockaddr *)&from, &from_length);

    if (bytes_received <= 0) return false;

    sender.ip_v4 = ntohl(from.sin_addr.s_addr);
    sender.port = ntohs(from.sin_port);

    if (static_cast<size_t>(bytes_received) < sizeof(Packet_Header))
        return false; 

    return true;
}

} // namespace network