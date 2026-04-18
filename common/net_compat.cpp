#include "common/net_compat.hpp"

#include <sstream>

namespace ll::net {

std::string endpoint_to_string(Endpoint e) {
    in_addr a{};
    a.s_addr = e.ip;
    char buf[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &a, buf, sizeof(buf));
    std::ostringstream oss;
    oss << buf << ':' << ntohs(e.port);
    return oss.str();
}

bool resolve_ipv4(const std::string& host, std::uint16_t port, sockaddr_in& out) {
    addrinfo hints{};
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    addrinfo* result = nullptr;
    std::string port_str = std::to_string(port);
    int rc = getaddrinfo(host.c_str(), port_str.c_str(), &hints, &result);
    if (rc != 0 || !result) {
        return false;
    }
    std::memcpy(&out, result->ai_addr, sizeof(sockaddr_in));
    freeaddrinfo(result);
    return true;
}

bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

} // namespace ll::net
