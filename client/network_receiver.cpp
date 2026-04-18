#include "client/network_receiver.hpp"

#include "common/logging.hpp"
#include "common/serialization.hpp"

#include <chrono>
#include <thread>

namespace ll::cl {

NetworkReceiver::NetworkReceiver(LaggedSocket& sock, SnapshotQueue& queue)
    : sock_(sock), queue_(queue) {}

NetworkReceiver::~NetworkReceiver() { stop(); }

void NetworkReceiver::start() {
    running_.store(true);
    thread_ = std::thread([this]{ run(); });
}

void NetworkReceiver::stop() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
}

void NetworkReceiver::run() {
    std::uint8_t buf[MAX_PACKET_BYTES];
    while (running_.load()) {
        sock_.pump();
        auto rr = sock_.recv_from(buf, sizeof(buf));
        if (!rr.has_data) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        auto t = proto::peek_type(buf, rr.size);
        if (!t) continue;

        if (*t == proto::PacketType::WORLD_SNAPSHOT) {
            StampedSnapshot ss{};
            if (proto::decode_snapshot(buf, rr.size, ss.snap)) {
                ss.recv_wall_ms = wall_time_ms();
                bytes_.fetch_add(rr.size);
                queue_.push(std::move(ss));
            }
        }
        // Other packet types are handled synchronously on the main thread in client.cpp.
    }
}

} // namespace ll::cl
