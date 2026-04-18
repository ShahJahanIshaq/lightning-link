#pragma once

#include "common/constants.hpp"
#include "common/types.hpp"

#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

namespace ll::cl {

// Per-remote-entity history of (position, recv_time). We use wall-clock recv time
// on the receive thread so that the render-time offset applies to true wire time.
struct InterpSample {
    Vec2          pos{};
    Vec2          vel{};
    std::uint64_t recv_wall_ms = 0;
};

class Interpolator {
public:
    // Called from the main thread once per frame after draining the queue.
    // Only ids OTHER than `local_id` should be passed in via `sample` to keep the
    // interpolator's responsibilities narrow.
    void ingest(std::uint16_t remote_id, const InterpSample& sample);

    // Compute the interpolated render position for a given remote id at wall-clock
    // time `now_ms`. Returns false if the entity has insufficient data (and should
    // not be rendered) or is stale past ENTITY_STALE_SEC.
    bool render_position(std::uint16_t remote_id, std::uint64_t now_ms, Vec2& out_pos) const;

    // Drop entries that are stale for longer than ENTITY_STALE_SEC.
    void prune(std::uint64_t now_ms);

    std::size_t buffer_depth(std::uint16_t remote_id) const;

private:
    std::unordered_map<std::uint16_t, std::deque<InterpSample>> histories_;
};

} // namespace ll::cl
