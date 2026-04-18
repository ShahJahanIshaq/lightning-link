#pragma once

// Thin wrapper to keep the rest of the code free of platform #ifdefs.
// Lightning Link targets desktop POSIX (macOS/Linux) per the spec.

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <netdb.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace ll::net {

// Normalized comparable endpoint key for session lookup.
struct Endpoint {
    std::uint32_t ip   = 0; // network order
    std::uint16_t port = 0; // network order

    bool operator==(const Endpoint& o) const { return ip == o.ip && port == o.port; }
};

struct EndpointHash {
    std::size_t operator()(const Endpoint& e) const noexcept {
        return (static_cast<std::size_t>(e.ip) << 16) ^ e.port;
    }
};

inline Endpoint endpoint_from_sockaddr(const sockaddr_in& a) {
    return Endpoint{a.sin_addr.s_addr, a.sin_port};
}

inline sockaddr_in sockaddr_from_endpoint(Endpoint e) {
    sockaddr_in out{};
#ifdef __APPLE__
    out.sin_len         = sizeof(sockaddr_in);
#endif
    out.sin_family      = AF_INET;
    out.sin_addr.s_addr = e.ip;
    out.sin_port        = e.port;
    return out;
}

std::string endpoint_to_string(Endpoint e);

// Resolve "host:port" or "host" + port into an IPv4 sockaddr_in. Returns true on success.
bool resolve_ipv4(const std::string& host, std::uint16_t port, sockaddr_in& out);

// Set socket to non-blocking mode.
bool set_nonblocking(int fd);

} // namespace ll::net
