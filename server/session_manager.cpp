#include "server/session_manager.hpp"

namespace ll::srv {

using clock_t_ = std::chrono::steady_clock;

std::uint16_t SessionManager::next_free_id() const {
    for (std::uint16_t id = 1; id <= MAX_PLAYERS; ++id) {
        if (id_to_endpoint_.find(id) == id_to_endpoint_.end()) return id;
    }
    return 0;
}

std::optional<std::uint16_t> SessionManager::register_or_get(const net::Endpoint& ep) {
    auto it = sessions_.find(ep);
    if (it != sessions_.end()) {
        it->second.last_input_time = clock_t_::now();
        return it->second.player_id;
    }
    std::uint16_t id = next_free_id();
    if (id == 0) return std::nullopt;

    Session s{};
    s.player_id       = id;
    s.endpoint        = ep;
    s.last_input_time = clock_t_::now();
    sessions_.emplace(ep, s);
    id_to_endpoint_[id] = ep;
    return id;
}

Session* SessionManager::by_endpoint(const net::Endpoint& ep) {
    auto it = sessions_.find(ep);
    return it == sessions_.end() ? nullptr : &it->second;
}

Session* SessionManager::by_player_id(std::uint16_t id) {
    auto it = id_to_endpoint_.find(id);
    if (it == id_to_endpoint_.end()) return nullptr;
    return by_endpoint(it->second);
}

bool SessionManager::verify_input(const net::Endpoint& ep, std::uint16_t claimed_id) {
    auto* s = by_endpoint(ep);
    return s && s->player_id == claimed_id;
}

void SessionManager::touch(const net::Endpoint& ep) {
    if (auto* s = by_endpoint(ep)) {
        s->last_input_time = clock_t_::now();
    }
}

std::vector<std::uint16_t> SessionManager::reap_timeouts() {
    using std::chrono::duration_cast;
    using std::chrono::milliseconds;

    const auto now     = clock_t_::now();
    const auto timeout = std::chrono::duration_cast<clock_t_::duration>(
        std::chrono::duration<double>(CLIENT_TIMEOUT_SEC));

    std::vector<std::uint16_t> removed;
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (now - it->second.last_input_time > timeout) {
            removed.push_back(it->second.player_id);
            id_to_endpoint_.erase(it->second.player_id);
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
    return removed;
}

bool SessionManager::remove_by_endpoint(const net::Endpoint& ep) {
    auto it = sessions_.find(ep);
    if (it == sessions_.end()) return false;
    id_to_endpoint_.erase(it->second.player_id);
    sessions_.erase(it);
    return true;
}

std::vector<net::Endpoint> SessionManager::all_endpoints() const {
    std::vector<net::Endpoint> out;
    out.reserve(sessions_.size());
    for (const auto& [ep, s] : sessions_) out.push_back(ep);
    return out;
}

} // namespace ll::srv
