#include "server/world_state.hpp"
#include "common/math_utils.hpp"

namespace ll::srv {

bool WorldState::add_player(std::uint16_t id) {
    if (players_.find(id) != players_.end()) return false;
    ServerPlayer p{};
    p.state.id  = id;
    p.state.pos = {ARENA_WIDTH * 0.5f, ARENA_HEIGHT * 0.5f};
    players_.emplace(id, p);
    return true;
}

bool WorldState::remove_player(std::uint16_t id) {
    return players_.erase(id) > 0;
}

bool WorldState::has_player(std::uint16_t id) const {
    return players_.find(id) != players_.end();
}

ServerPlayer* WorldState::get(std::uint16_t id) {
    auto it = players_.find(id);
    return it == players_.end() ? nullptr : &it->second;
}

const ServerPlayer* WorldState::get(std::uint16_t id) const {
    auto it = players_.find(id);
    return it == players_.end() ? nullptr : &it->second;
}

bool WorldState::apply_input(std::uint16_t id, std::uint32_t seq,
                             std::int8_t mx, std::int8_t my) {
    auto* p = get(id);
    if (!p) return false;
    // Reject strictly older seq. Equal seq is idempotent (same frame input repeats).
    if (seq < p->latest_seen_seq) return false;
    p->latest_seen_seq = seq;
    // Clamp to {-1, 0, 1} defensively; attackers could send arbitrary i8.
    p->pending_mx = std::max<std::int8_t>(-1, std::min<std::int8_t>(1, mx));
    p->pending_my = std::max<std::int8_t>(-1, std::min<std::int8_t>(1, my));
    p->has_pending = true;
    p->state.last_processed_input_seq = seq;
    return true;
}

void WorldState::step_tick() {
    const float dt = static_cast<float>(DT_SECONDS);
    for (auto& [id, sp] : players_) {
        Vec2 dir = normalize_dir(sp.pending_mx, sp.pending_my);
        sp.state.vel = dir * PLAYER_SPEED;
        sp.state.pos = sp.state.pos + sp.state.vel * dt;
        sp.state.pos = clamp_to_arena(sp.state.pos, ARENA_WIDTH, ARENA_HEIGHT);
    }
    ++tick_;
}

std::vector<PlayerState> WorldState::collect_player_states() const {
    std::vector<PlayerState> out;
    out.reserve(players_.size());
    for (const auto& [id, sp] : players_) {
        out.push_back(sp.state);
    }
    return out;
}

} // namespace ll::srv
