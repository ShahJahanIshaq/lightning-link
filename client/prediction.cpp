#include "client/prediction.hpp"
#include "common/math_utils.hpp"

namespace ll::cl {

namespace {
Vec2 apply_move(Vec2 p, std::int8_t mx, std::int8_t my, double dt) {
    Vec2 dir = normalize_dir(mx, my);
    Vec2 vel = dir * PLAYER_SPEED;
    Vec2 next = p + vel * static_cast<float>(dt);
    return clamp_to_arena(next, ARENA_WIDTH, ARENA_HEIGHT);
}
} // namespace

std::uint32_t Predictor::record_and_step(std::int8_t mx, std::int8_t my, double dt, Vec2& local_pos) {
    PredictedInput pi{};
    pi.seq = next_seq_++;
    pi.mx  = mx;
    pi.my  = my;
    pi.dt  = dt;
    ring_[head_] = pi;
    head_ = (head_ + 1) % INPUT_HISTORY_SIZE;
    if (count_ < INPUT_HISTORY_SIZE) ++count_;

    local_pos = apply_move(local_pos, mx, my, dt);
    return pi.seq;
}

std::optional<PredictedInput> Predictor::find_input_by_seq(std::uint32_t seq) const {
    for (std::size_t i = 0; i < count_; ++i) {
        std::size_t idx = (head_ + INPUT_HISTORY_SIZE - 1 - i) % INPUT_HISTORY_SIZE;
        if (ring_[idx].seq == seq) return ring_[idx];
    }
    return std::nullopt;
}

void Predictor::reconcile(Vec2& local_pos, const PlayerState& authoritative) {
    // Snap to authoritative.
    local_pos = authoritative.pos;
    // Replay inputs with seq > ack.
    const std::uint32_t ack = authoritative.last_processed_input_seq;
    // Iterate chronologically. With a ring buffer, the oldest entry is at (head - count) % N.
    for (std::size_t i = 0; i < count_; ++i) {
        std::size_t idx = (head_ + INPUT_HISTORY_SIZE - count_ + i) % INPUT_HISTORY_SIZE;
        const PredictedInput& pi = ring_[idx];
        if (pi.seq > ack) {
            local_pos = apply_move(local_pos, pi.mx, pi.my, pi.dt);
        }
    }
}

} // namespace ll::cl
