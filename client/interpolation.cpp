#include "client/interpolation.hpp"
#include "common/math_utils.hpp"

namespace ll::cl {

void Interpolator::ingest(std::uint16_t remote_id, const InterpSample& s) {
    auto& dq = histories_[remote_id];
    dq.push_back(s);
    while (dq.size() > SNAPSHOT_HISTORY_SIZE) dq.pop_front();
}

bool Interpolator::render_position(std::uint16_t remote_id, std::uint64_t now_ms, Vec2& out_pos) const {
    auto it = histories_.find(remote_id);
    if (it == histories_.end() || it->second.empty()) return false;
    const auto& dq = it->second;

    // Target render time is "now - INTERP_DELAY_SEC".
    const std::uint64_t delay_ms = static_cast<std::uint64_t>(INTERP_DELAY_SEC * 1000.0);
    if (now_ms < delay_ms) { out_pos = dq.back().pos; return true; }
    const std::uint64_t target = now_ms - delay_ms;

    // Check staleness using newest sample.
    if (now_ms - dq.back().recv_wall_ms >
        static_cast<std::uint64_t>(ENTITY_STALE_SEC * 1000.0)) {
        return false;
    }

    // Find bracketing samples.
    for (std::size_t i = 1; i < dq.size(); ++i) {
        const auto& a = dq[i - 1];
        const auto& b = dq[i];
        if (a.recv_wall_ms <= target && target <= b.recv_wall_ms) {
            if (b.recv_wall_ms == a.recv_wall_ms) {
                out_pos = b.pos;
                return true;
            }
            float t = static_cast<float>(
                static_cast<double>(target - a.recv_wall_ms) /
                static_cast<double>(b.recv_wall_ms - a.recv_wall_ms));
            out_pos = lerp(a.pos, b.pos, t);
            return true;
        }
    }

    // Target is before our earliest sample: hold to earliest known.
    // Target is after newest: hold to newest (no extrapolation by design).
    if (target < dq.front().recv_wall_ms) { out_pos = dq.front().pos; return true; }
    out_pos = dq.back().pos;
    return true;
}

void Interpolator::prune(std::uint64_t now_ms) {
    const std::uint64_t stale_ms = static_cast<std::uint64_t>(ENTITY_STALE_SEC * 1000.0);
    for (auto it = histories_.begin(); it != histories_.end();) {
        if (it->second.empty() ||
            now_ms - it->second.back().recv_wall_ms > stale_ms) {
            it = histories_.erase(it);
        } else {
            ++it;
        }
    }
}

std::size_t Interpolator::buffer_depth(std::uint16_t remote_id) const {
    auto it = histories_.find(remote_id);
    return it == histories_.end() ? 0 : it->second.size();
}

} // namespace ll::cl
