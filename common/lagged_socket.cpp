#include "common/lagged_socket.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace ll {

std::uint64_t LaggedSocket::now_us() const {
    using namespace std::chrono;
    return static_cast<std::uint64_t>(
        duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

LaggedSocket::~LaggedSocket() { close(); }

void LaggedSocket::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

bool LaggedSocket::bind(std::uint16_t port) {
    fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) return false;

    int one = 1;
    ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port);
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close();
        return false;
    }
    if (!net::set_nonblocking(fd_)) {
        close();
        return false;
    }
    return true;
}

bool LaggedSocket::bind_any() {
    return bind(0);
}

void LaggedSocket::configure(const LaggedSocketConfig& cfg) {
    std::lock_guard<std::mutex> g(mu_);
    cfg_ = cfg;
    rng_.seed(cfg.seed);
}

static int sample_jitter_ms(std::mt19937_64& rng, int jitter_ms) {
    if (jitter_ms <= 0) return 0;
    std::uniform_int_distribution<int> dist(-jitter_ms, jitter_ms);
    return dist(rng);
}

bool LaggedSocket::send_to(const net::Endpoint& ep, const void* data, std::size_t size) {
    if (fd_ < 0) return false;

    int    latency_ms = 0;
    int    jitter     = 0;
    double loss       = 0.0;
    {
        std::lock_guard<std::mutex> g(mu_);
        latency_ms = cfg_.add_latency_ms_send;
        jitter     = sample_jitter_ms(rng_, cfg_.jitter_ms);
        loss       = cfg_.loss_prob;

        if (loss > 0.0) {
            std::uniform_real_distribution<double> u(0.0, 1.0);
            if (u(rng_) < loss) {
                return true; // silently dropped
            }
        }
    }

    int total_ms = std::max(0, latency_ms + jitter);

    if (total_ms == 0) {
        sockaddr_in a = net::sockaddr_from_endpoint(ep);
        ssize_t n = ::sendto(fd_, data, size, 0, reinterpret_cast<sockaddr*>(&a), sizeof(a));
        return n >= 0;
    }

    Pending p;
    p.deliver_at_us = now_us() + static_cast<std::uint64_t>(total_ms) * 1000ULL;
    p.ep            = ep;
    p.bytes.assign(
        reinterpret_cast<const std::uint8_t*>(data),
        reinterpret_cast<const std::uint8_t*>(data) + size);
    p.outbound      = true;

    std::lock_guard<std::mutex> g(mu_);
    queue_.push(std::move(p));
    return true;
}

void LaggedSocket::flush_outbound_locked() {
    std::uint64_t now = now_us();
    while (!queue_.empty() && queue_.top().deliver_at_us <= now) {
        const Pending& p = queue_.top();
        if (p.outbound) {
            sockaddr_in a = net::sockaddr_from_endpoint(p.ep);
            ::sendto(fd_, p.bytes.data(), p.bytes.size(), 0,
                     reinterpret_cast<sockaddr*>(&a), sizeof(a));
            queue_.pop();
        } else {
            break; // inbound handled in recv_from
        }
    }
}

LaggedSocket::RecvResult LaggedSocket::recv_from(void* out, std::size_t cap) {
    RecvResult rr;
    if (fd_ < 0) return rr;

    // Drain the real socket first, pushing inbound packets into the pending queue
    // scheduled per the recv-side conditioner.
    for (;;) {
        sockaddr_in from{};
        socklen_t   flen = sizeof(from);
        std::uint8_t tmp[2048];
        ssize_t n = ::recvfrom(fd_, tmp, sizeof(tmp), 0,
                               reinterpret_cast<sockaddr*>(&from), &flen);
        if (n <= 0) break;

        int    latency_ms = 0;
        int    jitter     = 0;
        double loss       = 0.0;
        {
            std::lock_guard<std::mutex> g(mu_);
            latency_ms = cfg_.add_latency_ms_recv;
            jitter     = sample_jitter_ms(rng_, cfg_.jitter_ms);
            loss       = cfg_.loss_prob;

            if (loss > 0.0) {
                std::uniform_real_distribution<double> u(0.0, 1.0);
                if (u(rng_) < loss) continue; // dropped inbound
            }
        }

        int total_ms = std::max(0, latency_ms + jitter);

        Pending p;
        p.deliver_at_us = now_us() + static_cast<std::uint64_t>(total_ms) * 1000ULL;
        p.ep            = net::endpoint_from_sockaddr(from);
        p.bytes.assign(tmp, tmp + n);
        p.outbound      = false;

        std::lock_guard<std::mutex> g(mu_);
        queue_.push(std::move(p));
    }

    // Now look for a ready inbound delivery.
    std::lock_guard<std::mutex> g(mu_);
    flush_outbound_locked();
    std::uint64_t now = now_us();
    // Rebuild queue while looking for the earliest inbound that has matured.
    // A tiny linear scan is fine since queue_ is bounded by send rate * latency.
    std::vector<Pending> held;
    held.reserve(queue_.size());
    while (!queue_.empty()) {
        Pending top = queue_.top();
        queue_.pop();
        if (!top.outbound && top.deliver_at_us <= now) {
            // found it - deliver this one, put the rest back
            std::size_t n = std::min(cap, top.bytes.size());
            std::memcpy(out, top.bytes.data(), n);
            rr.size     = n;
            rr.from     = top.ep;
            rr.has_data = true;
            for (auto& h : held) queue_.push(std::move(h));
            return rr;
        }
        held.push_back(std::move(top));
    }
    for (auto& h : held) queue_.push(std::move(h));
    return rr;
}

void LaggedSocket::pump() {
    std::lock_guard<std::mutex> g(mu_);
    flush_outbound_locked();
}

} // namespace ll
