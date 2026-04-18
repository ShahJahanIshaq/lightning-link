#include "server/udp_snapshot_broadcaster.hpp"
#include "common/serialization.hpp"

#include <array>

namespace ll::srv {

std::size_t UdpSnapshotBroadcaster::broadcast(const proto::Snapshot& snap,
                                              const std::vector<net::Endpoint>& recipients) {
    std::array<std::uint8_t, proto::MAX_SNAPSHOT_BYTES> buf{};
    std::size_t n = proto::encode_snapshot(buf.data(), buf.size(), snap);
    if (n == 0) return 0;

    for (const auto& ep : recipients) {
        sock_.send_to(ep, buf.data(), n);
        bytes_sent_ += n;
    }
    ++snapshots_sent_;
    return n * recipients.size();
}

} // namespace ll::srv
