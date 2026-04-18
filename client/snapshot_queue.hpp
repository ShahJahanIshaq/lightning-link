#pragma once

#include "common/protocol.hpp"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>

namespace ll::cl {

// Receive-side timestamped snapshot for interpolation time-stamping.
struct StampedSnapshot {
    proto::Snapshot snap;
    std::uint64_t   recv_wall_ms = 0;
};

class SnapshotQueue {
public:
    void push(StampedSnapshot s) {
        std::lock_guard<std::mutex> g(mu_);
        q_.push_back(std::move(s));
        cv_.notify_one();
    }

    std::optional<StampedSnapshot> try_pop() {
        std::lock_guard<std::mutex> g(mu_);
        if (q_.empty()) return std::nullopt;
        StampedSnapshot s = std::move(q_.front());
        q_.pop_front();
        return s;
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> g(mu_);
        return q_.size();
    }

private:
    mutable std::mutex          mu_;
    std::condition_variable     cv_;
    std::deque<StampedSnapshot> q_;
};

} // namespace ll::cl
