#pragma once

#include "common/lagged_socket.hpp"
#include "common/protocol.hpp"
#include "common/net_compat.hpp"

#include <vector>

namespace ll::srv {

// Encodes a snapshot once and sends to all provided endpoints, returning total bytes sent.
class UdpSnapshotBroadcaster {
public:
    explicit UdpSnapshotBroadcaster(LaggedSocket& sock) : sock_(sock) {}

    std::size_t broadcast(const proto::Snapshot& snap,
                          const std::vector<net::Endpoint>& recipients);

    std::uint64_t total_bytes_sent() const { return bytes_sent_; }
    std::uint64_t total_snapshots() const  { return snapshots_sent_; }

private:
    LaggedSocket& sock_;
    std::uint64_t bytes_sent_     = 0;
    std::uint64_t snapshots_sent_ = 0;
};

} // namespace ll::srv
