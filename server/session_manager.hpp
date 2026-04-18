#pragma once

#include "common/net_compat.hpp"
#include "common/constants.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ll::srv {

struct Session {
    std::uint16_t       player_id = 0;
    net::Endpoint       endpoint{};
    std::chrono::steady_clock::time_point last_input_time;
};

class SessionManager {
public:
    // If endpoint already has a session, returns the existing player id.
    // Otherwise allocates a new id (1..MAX_PLAYERS). Returns nullopt if full.
    std::optional<std::uint16_t> register_or_get(const net::Endpoint& ep);

    // Lookup / mutation.
    Session* by_endpoint(const net::Endpoint& ep);
    Session* by_player_id(std::uint16_t id);

    bool verify_input(const net::Endpoint& ep, std::uint16_t claimed_id);

    void touch(const net::Endpoint& ep);

    // Collect and remove sessions whose last_input_time exceeds the timeout.
    std::vector<std::uint16_t> reap_timeouts();

    // Explicit disconnect (used on DISCONNECT_NOTICE).
    bool remove_by_endpoint(const net::Endpoint& ep);

    std::vector<net::Endpoint> all_endpoints() const;
    std::size_t size() const { return sessions_.size(); }

private:
    std::unordered_map<net::Endpoint, Session, net::EndpointHash> sessions_;
    std::unordered_map<std::uint16_t, net::Endpoint>              id_to_endpoint_;

    std::uint16_t next_free_id() const;
};

} // namespace ll::srv
