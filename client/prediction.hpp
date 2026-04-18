#pragma once

#include "common/constants.hpp"
#include "common/types.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace ll::cl {

struct PredictedInput {
    std::uint32_t seq = 0;
    std::int8_t   mx  = 0;
    std::int8_t   my  = 0;
    double        dt  = 0.0;
};

// Ring buffer of the most recent INPUT_HISTORY_SIZE local inputs plus the helpers
// to (a) advance the local predicted state immediately and (b) reconcile against
// an authoritative snapshot (snap-to-authoritative + replay unacknowledged).
class Predictor {
public:
    // Record an input as it is produced; returns the seq assigned.
    std::uint32_t record_and_step(std::int8_t mx, std::int8_t my, double dt, Vec2& local_pos);

    // Snap the local state to `authoritative` then replay all locally-recorded
    // inputs whose seq > last_processed_input_seq.
    void reconcile(Vec2& local_pos, const PlayerState& authoritative);

    std::uint32_t next_seq() const { return next_seq_; }

    // For HUD / debug only.
    std::size_t history_size() const { return count_; }
    std::optional<PredictedInput> find_input_by_seq(std::uint32_t seq) const;

private:
    std::array<PredictedInput, INPUT_HISTORY_SIZE> ring_{};
    std::size_t   head_  = 0; // next write slot
    std::size_t   count_ = 0;
    std::uint32_t next_seq_ = 1;
};

} // namespace ll::cl
