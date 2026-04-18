#pragma once

#include "client/snapshot_queue.hpp"
#include "common/lagged_socket.hpp"

#include <atomic>
#include <cstdint>
#include <thread>

namespace ll::cl {

// Background receive thread for optimized (UDP) mode.
class NetworkReceiver {
public:
    NetworkReceiver(LaggedSocket& sock, SnapshotQueue& queue);
    ~NetworkReceiver();

    void start();
    void stop();

    std::uint64_t bytes_received_total() const { return bytes_.load(); }

private:
    void run();

    LaggedSocket&        sock_;
    SnapshotQueue&       queue_;
    std::atomic<bool>    running_{false};
    std::atomic<std::uint64_t> bytes_{0};
    std::thread          thread_;
};

} // namespace ll::cl
