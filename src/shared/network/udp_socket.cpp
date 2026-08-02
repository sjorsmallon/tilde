#include "udp_socket.hpp"
#include "../log.hpp"
#include <cstdio>
#include <string>
#include <cerrno>
#include <cstring>

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

bool Address::parse_endpoint(const std::string &str, uint16 default_port,
                             Address &out_address, std::string &out_error)
{
    std::string host = str;
    uint16 port = default_port;

    // Split on the last ':' so the port is whatever follows it. There is exactly
    // one colon in an IPv4 endpoint; searching from the back costs nothing and
    // keeps this honest if IPv6 ever shows up.
    size_t colon = str.find_last_of(':');
    if (colon != std::string::npos)
    {
        host = str.substr(0, colon);
        std::string port_text = str.substr(colon + 1);
        if (port_text.empty())
        {
            out_error = "missing port after ':'";
            return false;
        }

        unsigned long parsed_port = 0;
        for (char c : port_text)
        {
            if (c < '0' || c > '9')
            {
                out_error = "'" + port_text + "' is not a port number";
                return false;
            }
            parsed_port = parsed_port * 10 + static_cast<unsigned long>(c - '0');
            if (parsed_port > 65535)
                break; // stop before overflowing; the range check below reports it
        }

        if (parsed_port == 0 || parsed_port > 65535)
        {
            out_error = "port " + port_text + " is out of range (1-65535)";
            return false;
        }
        port = static_cast<uint16>(parsed_port);
    }

    if (!Address::parse(host, out_address))
    {
        out_error = "'" + host + "' is not an IPv4 address";
        return false;
    }

    out_address.port = port; // parse() zeroes it; we own the endpoint's port.
    out_error.clear();
    return true;
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
    printf("[UDP] Attempting to open socket on port %d\n", port);
    close();

#ifdef _WIN32
    // Ensure Winsock is initialized once per process
    static bool winsock_initialized = false;
    if (!winsock_initialized)
    {
        printf("[UDP] Initializing Winsock...\n");
        WSADATA wsaData;
        int wsaErr = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (wsaErr != 0)
        {
            printf("[UDP] ERROR: WSAStartup failed: %d\n", wsaErr);
            log_error("WSAStartup failed: {}", wsaErr);
            return false;
        }
        printf("[UDP] Winsock initialized successfully\n");
        winsock_initialized = true;
    }
#endif

    printf("[UDP] Creating socket...\n");
    m_socket_handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket_handle == INVALID_SOCKET)
    {
#ifdef _WIN32
        int err = WSAGetLastError();
        printf("[UDP] ERROR: socket() failed with code: %d\n", err);
        log_error("socket() failed: {}", err);
#else
        printf("[UDP] ERROR: socket() failed with errno: %d (%s)\n", errno, std::strerror(errno));
        log_error("socket() failed: {} ({})", errno, std::strerror(errno));
#endif
        return false;
    }
    printf("[UDP] Socket created successfully\n");

    // Allow reusing the port if it's in TIME_WAIT state (useful for rapid restarts)
    int reuse = 1;
    printf("[UDP] Setting SO_REUSEADDR...\n");
#ifdef _WIN32
    if (setsockopt(m_socket_handle, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse)) == SOCKET_ERROR)
#else
    if (setsockopt(m_socket_handle, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
#endif
    {
#ifdef _WIN32
        int err = WSAGetLastError();
        printf("[UDP] ERROR: setsockopt(SO_REUSEADDR) failed with code: %d\n", err);
        log_error("setsockopt(SO_REUSEADDR) failed: {}", err);
#else
        printf("[UDP] ERROR: setsockopt(SO_REUSEADDR) failed with errno: %d (%s)\n", errno, std::strerror(errno));
        log_error("setsockopt(SO_REUSEADDR) failed: {} ({})", errno, std::strerror(errno));
#endif
        close();
        return false;
    }

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    printf("[UDP] Binding to port %d...\n", port);
    if (bind(m_socket_handle, (const struct sockaddr *)&address, sizeof(address)) == SOCKET_ERROR)
    {
#ifdef _WIN32
        int err = WSAGetLastError();
        printf("[UDP] ERROR: bind() failed on port %d with code: %d\n", port, err);
        log_error("bind() failed on port {}: {}", port, err);
#else
        printf("[UDP] ERROR: bind() failed on port %d with errno: %d (%s)\n", port, errno, std::strerror(errno));
        log_error("bind() failed on port {}: {} ({})", port, errno, std::strerror(errno));
#endif
        close();
        return false;
    }
    printf("[UDP] Successfully bound to port %d\n", port);

    // --- Set Non-Blocking ---
    printf("[UDP] Setting non-blocking mode...\n");
#ifdef _WIN32
    u_long mode = 1; // 1 to enable non-blocking
    if (ioctlsocket(m_socket_handle, FIONBIO, &mode) != 0)
    {
        int err = WSAGetLastError();
        printf("[UDP] ERROR: ioctlsocket(FIONBIO) failed with code: %d\n", err);
        log_error("ioctlsocket(FIONBIO) failed: {}", err);
        close();
        return false;
    }
#else
    int flags = fcntl(m_socket_handle, F_GETFL, 0);
    if (flags == -1) flags = 0;
    if (fcntl(m_socket_handle, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        printf("[UDP] ERROR: fcntl(F_SETFL) failed with errno: %d (%s)\n", errno, std::strerror(errno));
        log_error("fcntl(F_SETFL) failed: {} ({})", errno, std::strerror(errno));
        close();
        return false;
    }
#endif

    printf("[UDP] Socket successfully opened on port %d\n", port);
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

    // TODO: stamp packet.header.timestamp ("when was this sent?") HERE, at the
    // actual send, so it reflects the true moment on the wire. Right now nothing
    // in the codebase ever writes header.timestamp, yet the server reads it to
    // order incoming moves (poll_network -> TimestampedMove in
    // server_connection_state.hpp), so that ordering is currently keyed on an
    // unset (0/garbage) value. Doing it here needs a mutable packet: either drop
    // the `const` and set it before sendto, or copy-and-stamp into a local.

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