#pragma once

#include "common/net_compat.hpp"

#include <cstdint>
#include <mutex>
#include <queue>
#include <random>
#include <string>
#include <vector>

namespace ll {

// In-app network conditioner wrapping a UDP socket. Latency/jitter/loss are applied on
// both send and receive (symmetric), so the --add-latency-ms argument approximates half
// of the simulated RTT. This keeps the conditioner reproducible (single seeded PRNG) and
// cross-platform on desktop POSIX without pfctl/tc.
struct LaggedSocketConfig {
    int    add_latency_ms_send = 0;
    int    add_latency_ms_recv = 0;
    int    jitter_ms           = 0;
    double loss_prob           = 0.0;    // 0.0 - 1.0
    std::uint64_t seed         = 0xC0FFEEULL;
};

class LaggedSocket {
public:
    // Listen/bind a UDP socket on all interfaces at `port`. Returns true on success.
    bool bind(std::uint16_t port);
    // Bind to an ephemeral port (used by clients).
    bool bind_any();
    // Explicit close (also invoked by destructor).
    void close();

    ~LaggedSocket();

    void configure(const LaggedSocketConfig& cfg);

    // Non-blocking send. Data may be delayed or dropped per the conditioner.
    bool send_to(const net::Endpoint& ep, const void* data, std::size_t size);

    struct RecvResult {
        std::size_t        size = 0;
        net::Endpoint      from{};
        bool               has_data = false;
    };
    // Non-blocking receive. Returns has_data=false if nothing is ready. Data may be
    // buffered internally by the conditioner and delivered on a later call.
    RecvResult recv_from(void* out, std::size_t cap);

    // Advance the conditioner clock; releases pending scheduled deliveries.
    // Must be called periodically by the owner (e.g. each game tick).
    void pump();

    // Raw fd, used only for poll/select shutdown.
    int raw_fd() const { return fd_; }

private:
    struct Pending {
        std::uint64_t                deliver_at_us = 0;
        net::Endpoint                ep{};
        std::vector<std::uint8_t>    bytes;
        bool                         outbound = false; // true = deliver via sendto, false = deliver to recv
    };

    struct Cmp { bool operator()(const Pending& a, const Pending& b) const {
        return a.deliver_at_us > b.deliver_at_us;
    }};

    int                                                fd_ = -1;
    LaggedSocketConfig                                 cfg_{};
    std::mt19937_64                                    rng_{0xC0FFEEULL};
    std::mutex                                         mu_;
    std::priority_queue<Pending, std::vector<Pending>, Cmp> queue_;

    std::uint64_t                                      now_us() const;
    void                                               flush_outbound_locked();
};

} // namespace ll
