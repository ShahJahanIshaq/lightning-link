#pragma once

#include "common/constants.hpp"
#include "common/types.hpp"

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ll::srv {

// Authoritative per-player state plus bookkeeping used only server-side.
struct ServerPlayer {
    PlayerState   state{};
    std::int8_t   pending_mx = 0;
    std::int8_t   pending_my = 0;
    bool          has_pending = false;
    std::uint32_t latest_seen_seq = 0;       // drops stale UDP inputs
};

class WorldState {
public:
    // Tries to add a player with the given id. Returns false if id already exists.
    bool add_player(std::uint16_t id);
    bool remove_player(std::uint16_t id);
    bool has_player(std::uint16_t id) const;

    ServerPlayer*       get(std::uint16_t id);
    const ServerPlayer* get(std::uint16_t id) const;

    // Apply an input: updates pending direction and last_processed_input_seq when seq > latest.
    // Returns true if accepted, false if stale.
    bool apply_input(std::uint16_t id, std::uint32_t seq, std::int8_t mx, std::int8_t my);

    // One authoritative simulation tick.
    void step_tick();

    std::size_t player_count() const { return players_.size(); }
    std::vector<PlayerState> collect_player_states() const;

    std::uint32_t tick() const { return tick_; }

private:
    std::unordered_map<std::uint16_t, ServerPlayer> players_;
    std::uint32_t tick_ = 0;
};

} // namespace ll::srv
